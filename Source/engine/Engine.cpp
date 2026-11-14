#include "Engine.h"

#include <cmath>

namespace cm
{
namespace
{
float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
}

HarmonyResult computeHarmony (double c, const Scale& scale, const Params& p)
{
    HarmonyResult r;
    switch (p.retune)
    {
        case RetuneMode::Off:
            r.degree = scale.nearestDegree (c);
            r.correction = 0;
            break;
        case RetuneMode::Nearest:
            r.degree = scale.nearestDegree (c);
            r.correction = (scale.degreeCents (r.degree) - c) * p.retuneAmount;
            break;
        case RetuneMode::Remap:
        {
            // Each 12-TET semitone ("fret") above the root becomes the next step of the scale,
            // so a standard guitar plays the tuning chromatically. Bends and vibrato are kept.
            const int s = (int) std::lround (c / 100.0);
            r.degree = s;
            r.correction = (scale.degreeCents (s) - 100.0 * s) * p.retuneAmount;
            break;
        }
    }
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& vp = p.voices[(size_t) v];
        double interval;
        if (vp.mode == VoiceMode::Interval)
            interval = vp.semitones * 100.0;
        else
            interval = scale.degreeCents (r.degree + vp.degrees) - scale.degreeCents (r.degree);
        r.shift[(size_t) v] = r.correction + interval + vp.cents;
    }
    return r;
}

Engine::Engine()
{
    customScale = builtinScales().front();
    customScale.setName ("Custom (none loaded)");
    pendingScale = customScale;
    for (auto& a : status.voiceCents) a.store (0);
    for (auto& a : status.voiceOn) a.store (false);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    lead.prepare (sr);
    for (auto& s : shifters) s.prepare (sr);
    reset();
}

void Engine::reset()
{
    detector.reset();
    lead.reset();
    for (auto& s : shifters) s.reset();
    const double cr = sr / kControlInterval;
    corrSmooth = {};
    corrSmooth.setTime (params.retuneSpeedMs, cr);
    for (int v = 0; v < kNumVoices; ++v)
    {
        intervalSmooth[(size_t) v] = {};
        driftSmooth[(size_t) v] = {};
        delayDriftSmooth[(size_t) v] = {};
        driftSmooth[(size_t) v].setTime (400.0, cr);
        delayDriftSmooth[(size_t) v].setTime (600.0, cr);
        ratio[(size_t) v] = 1;
        curL[(size_t) v] = curR[(size_t) v] = 0;
    }
    havePitch = false;
    lastCents = 0;
    controlCount = 0;
    leadRatio = 1;
    curDry = dbToGain (params.dryDb);
    curOut = dbToGain (params.outputDb);
    // seed the harmony intervals so the first note doesn't glide in from unison
    const auto h = computeHarmony (0.0, scaleFor (params.scaleIndex), params);
    for (int v = 0; v < kNumVoices; ++v) intervalSmooth[(size_t) v].value = h.shift[(size_t) v];
}

void Engine::setCustomScale (const Scale& s)
{
    std::lock_guard<std::mutex> lock (scaleMutex);
    pendingScale = s;
    scalePending.store (true);
}

const Scale& Engine::scaleFor (int idx) const
{
    const auto& b = builtinScales();
    if (idx >= 0 && idx < (int) b.size()) return b[(size_t) idx];
    return customScale;
}

double Engine::rootHz() const
{
    // root in the octave starting at middle C
    return params.refHz * std::pow (2.0, (params.key - 9) / 12.0);
}

void Engine::control()
{
    if (scalePending.load() && scaleMutex.try_lock())
    {
        customScale = pendingScale;
        scalePending.store (false);
        scaleMutex.unlock();
    }

    const double cr = sr / kControlInterval;
    corrSmooth.setTime (params.retuneSpeedMs, cr);
    for (auto& s : intervalSmooth) s.setTime (params.glideMs, cr);

    const auto& scale = scaleFor (params.scaleIndex);
    if (detector.isVoiced())
    {
        const double f = detector.getFrequency();
        lastCents = 1200.0 * std::log2 (f / rootHz());
        lastPeriod = (float) (sr / f);
        havePitch = true;
    }
    const auto h = computeHarmony (havePitch ? lastCents : 0.0, scale, params);

    // humanize: slow random drift per voice
    if (++driftCount >= (int) (cr * 0.25))
    {
        driftCount = 0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            driftTarget[(size_t) v] = rng.next();
            delayDriftTarget[(size_t) v] = 0.5f + 0.5f * rng.next();
        }
    }

    leadActive = params.retune != RetuneMode::Off;
    const double corr = corrSmooth.step (h.correction);
    leadRatio = (float) std::pow (2.0, corr / 1200.0);
    lead.setRatio (leadRatio);
    lead.setPeriod (havePitch ? lastPeriod : 0.0f);

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& vp = params.voices[(size_t) v];
        const double drift = driftSmooth[(size_t) v].step (driftTarget[(size_t) v]) * params.humanize * 12.0;
        const double dDrift = delayDriftSmooth[(size_t) v].step (delayDriftTarget[(size_t) v]) * params.humanize * 18.0;
        const double total = intervalSmooth[(size_t) v].step (h.shift[(size_t) v]) + drift;
        ratio[(size_t) v] = (float) std::pow (2.0, total / 1200.0);
        auto& sh = shifters[(size_t) v];
        sh.setRatio (ratio[(size_t) v]);
        sh.setPeriod (havePitch ? lastPeriod : 0.0f);
        sh.setDelaySamples ((float) ((vp.delayMs + dDrift) * 0.001 * sr));

        const float g = vp.on ? dbToGain (vp.levelDb) : 0.0f;
        const float a = (std::clamp (vp.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * (float) kPi;
        gainL[(size_t) v] = g * std::cos (a);
        gainR[(size_t) v] = g * std::sin (a);

        status.voiceCents[(size_t) v].store ((float) ((havePitch ? lastCents : 0.0) + total));
        status.voiceOn[(size_t) v].store (vp.on);
    }
    dryGain = dbToGain (params.dryDb);
    wetGain = dbToGain (params.wetDb);
    outGain = dbToGain (params.outputDb);

    status.voiced.store (detector.isVoiced());
    status.detectedHz.store (havePitch ? (float) (rootHz() * std::pow (2.0, lastCents / 1200.0)) : 0.0f);
    status.inputCents.store ((float) lastCents);
    { const int n = std::max (1, scale.n); status.degree.store (((h.degree % n) + n) % n); }
    status.leadCents.store ((float) (lastCents + corr));
}

void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    detector.setGateDb (params.gateDb);
    const float sm = 0.002f;   // per-sample gain smoothing
    const bool stereoIn = numIn > 1;

    for (int i = 0; i < n; ++i)
    {
        if (controlCount-- <= 0) { control(); controlCount = kControlInterval - 1; }

        const float l = numIn > 0 ? in[0][i] : 0.0f;
        const float r = stereoIn ? in[1][i] : l;
        const float mono = 0.5f * (l + r);
        inPeak = std::max (inPeak * 0.9997f, std::abs (mono));
        detector.push (mono);

        // dry / lead
        curDry += sm * (dryGain - curDry);
        float dl, dr;
        const float leadOut = lead.process (mono);
        if (leadActive) { dl = dr = leadOut * curDry; }
        else { dl = l * curDry; dr = r * curDry; }

        float wl = 0, wr = 0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            curL[(size_t) v] += sm * (gainL[(size_t) v] * wetGain - curL[(size_t) v]);
            curR[(size_t) v] += sm * (gainR[(size_t) v] * wetGain - curR[(size_t) v]);
            const float y = shifters[(size_t) v].process (mono);   // keep running so buffers stay warm
            wl += y * curL[(size_t) v];
            wr += y * curR[(size_t) v];
        }

        curOut += sm * (outGain - curOut);
        float ol = (dl + wl) * curOut, orr = (dr + wr) * curOut;
        outPeak = std::max (outPeak * 0.9997f, std::max (std::abs (ol), std::abs (orr)));
        if (numOut == 1) { out[0][i] = 0.5f * (ol + orr); }
        else if (numOut >= 2) { out[0][i] = ol; out[1][i] = orr; }
    }
    status.inLevel.store (inPeak);
    status.outLevel.store (outPeak);
}
} // namespace cm
