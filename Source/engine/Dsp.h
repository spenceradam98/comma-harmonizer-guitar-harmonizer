// Pitch tracking and pitch shifting. Plain scalar C++ (no SIMD intrinsics, no -march).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cm
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------
// YIN monophonic pitch detector, tuned for guitar (about 60 Hz to 1.4 kHz).
// The input is low-passed and decimated to roughly 11-12 kHz, then analysed every hop.
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) (sampleRate / 16000.0));
        fs = sampleRate / decim;
        maxLag = (int) std::ceil (fs / kMinHz) + 2;
        minLag = std::max (2, (int) std::floor (fs / kMaxHz));
        window = std::max (maxLag, (int) (fs * 0.024));
        bufLen = window + maxLag + 2;
        buf.assign ((size_t) bufLen, 0.0f);
        lin.assign ((size_t) bufLen, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        hop = std::max (16, (int) (fs * 0.004));
        // two cascaded one-pole low-passes at ~1.8 kHz as anti-alias before decimating
        lpCoef = (float) (1.0 - std::exp (-2.0 * kPi * 1800.0 / sampleRate));
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lp1 = lp2 = 0;
        decimCount = 0; writePos = 0; sinceHop = 0; filled = 0;
        freq = 0; voiced = false; level = 0; clarity = 0;
    }

    void setGateDb (float db) { gate = std::pow (10.0f, db / 20.0f); }

    // Feed one input sample; returns true when a new analysis frame was produced.
    bool push (float x)
    {
        lp1 += lpCoef * (x - lp1);
        lp2 += lpCoef * (lp1 - lp2);
        if (++decimCount < decim) return false;
        decimCount = 0;
        buf[(size_t) writePos] = lp2;
        writePos = (writePos + 1) % bufLen;
        if (filled < bufLen) ++filled;
        if (++sinceHop < hop || filled < bufLen) return false;
        sinceHop = 0;
        analyse();
        return true;
    }

    float getFrequency() const { return freq; }   // Hz, 0 if unvoiced
    bool isVoiced() const { return voiced; }
    float getLevel() const { return level; }      // RMS of the analysis window
    float getClarity() const { return clarity; }  // 1 - aperiodicity, 0..1

    static constexpr double kMinHz = 60.0, kMaxHz = 1400.0;

private:
    float at (int i) const { return buf[(size_t) ((writePos + i) % bufLen)]; }   // 0 = oldest

    void analyse()
    {
        for (int j = 0; j < bufLen; ++j) lin[(size_t) j] = at (j);
        const float* x = lin.data();
        double e = 0;
        for (int j = 0; j < window; ++j) e += x[j] * x[j];
        level = (float) std::sqrt (e / window);
        if (level < gate) { voiced = false; clarity = 0; return; }

        // difference function
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0;
            for (int j = 0; j < window; ++j)
            {
                const float a = x[j] - x[j + tau];
                d += a * a;
            }
            diff[(size_t) tau] = (float) d;
        }
        // cumulative mean normalised difference
        diff[0] = 1.0f;
        double running = 0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
        }
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < kThreshold)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) { voiced = false; clarity = 0; return; }

        // parabolic interpolation
        double t = best;
        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) t += 0.5 * (a - c) / den;
        clarity = (float) std::clamp (1.0 - b, 0.0, 1.0);
        freq = (float) (fs / t);
        voiced = freq >= kMinHz && freq <= kMaxHz;
    }

    static constexpr float kThreshold = 0.15f;
    std::vector<float> buf, lin, diff;
    double fs = 11025;
    int decim = 4, maxLag = 200, minLag = 8, window = 260, bufLen = 462, hop = 44;
    int decimCount = 0, writePos = 0, sinceHop = 0, filled = 0;
    float lpCoef = 0.2f, lp1 = 0, lp2 = 0, gate = 0.001f;
    float freq = 0, level = 0, clarity = 0;
    bool voiced = false;
};

// ---------------------------------------------------------------------------------------
// Two-tap delay-line pitch shifter (rotating read heads with sin^2 crossfades).
// When the input period is known the grain length is set to a whole number of periods,
// so the two heads stay phase-aligned and the usual beating/flanging is much reduced.
// Also provides a plain delay (for doubling and slap) on the same buffer.
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        int size = 1;
        while (size < (int) (sampleRate * 0.35)) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        grain = grainTarget = (float) (sampleRate * 0.030);
        delay = delayTarget = 0;
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0; phase = 0;
    }

    void setRatio (float r) { ratio = std::clamp (r, 0.25f, 4.0f); }
    void setPeriod (float periodSamples)   // 0 = unknown
    {
        const float minGrain = (float) (sr * 0.022);
        if (periodSamples > 1.0f)
            grainTarget = 2.0f * periodSamples * std::ceil (minGrain / (2.0f * periodSamples));   // heads half a grain apart stay a whole number of periods apart
        else
            grainTarget = (float) (sr * 0.030);
        grainTarget = std::min (grainTarget, (float) (sr * 0.060));
    }
    void setDelaySamples (float d) { delayTarget = std::clamp (d, 0.0f, (float) (sr * 0.2)); }

    float process (float x)
    {
        buf[(size_t) w] = x;
        grain += 0.0015f * (grainTarget - grain);
        delay += 0.0008f * (delayTarget - delay);

        const float d1 = delay + 2.0f + phase * grain;
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float d2 = delay + 2.0f + p2 * grain;
        const float s1 = std::sin ((float) kPi * phase);
        const float g1 = s1 * s1;
        const float y = g1 * read (d1) + (1.0f - g1) * read (d2);

        float inc = (1.0f - ratio) / grain;
        // At (near) unity ratio, ease the heads to phase 0 where only one head sounds,
        // so a static unison voice is a clean delay instead of a two-head comb filter.
        if (std::abs (ratio - 1.0f) < 0.0005f && phase > 0.0f)
            inc += (phase < 0.5f ? -1.0f : 1.0f) * std::min (0.0015f / grain, phase < 0.5f ? phase : 1.0f - phase);
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
        w = (w + 1) & mask;
        return y;
    }

    // latency of the shifted signal at ratio 1, in samples (average head delay)
    float nominalLatency() const { return 2.0f + 0.5f * grain; }

private:
    float read (float d) const
    {
        float pos = (float) w - d;
        while (pos < 0) pos += (float) (mask + 1);
        const int i = (int) pos;
        const float f = pos - (float) i;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)],
                    x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        // 4-point Hermite
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    std::vector<float> buf;
    int mask = 0, w = 0;
    double sr = 48000;
    float ratio = 1, phase = 0, grain = 1440, grainTarget = 1440, delay = 0, delayTarget = 0;
};

// One-pole smoother in the log/cents domain.
struct Smoother
{
    double value = 0, coef = 1;
    void setTime (double ms, double controlRate)
    {
        coef = ms <= 0.01 ? 1.0 : 1.0 - std::exp (-1.0 / (ms * 0.001 * controlRate));
    }
    double step (double target) { value += coef * (target - value); return value; }
};

// Small deterministic random generator for humanize drift.
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / (float) 0xffffff * 2.0f - 1.0f; }
};
} // namespace cm
