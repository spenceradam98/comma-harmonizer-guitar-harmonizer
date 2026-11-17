// Comma engine: pitch tracking, scale-aware harmony voices and microtonal retuning.
// No JUCE dependency so it can be unit-tested on its own.
#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include "Dsp.h"
#include "Scales.h"

namespace cm
{
constexpr int kNumVoices = 4;

enum class VoiceMode { Interval = 0, Scale = 1 };
enum class RetuneMode { Off = 0, Nearest = 1, Remap = 2 };

struct VoiceParams
{
    bool on = false;
    VoiceMode mode = VoiceMode::Scale;
    int semitones = 0;     // Interval mode, -24..+24
    int degrees = 2;       // Scale mode: scale steps above (+) or below (-) the played note
    float cents = 0;       // fine detune, -100..+100
    float levelDb = -3;    // -60 = silent
    float pan = 0;         // -1..+1
    float delayMs = 0;     // 0..150, for doubling / slap
};

struct Params
{
    int key = 0;                    // root pitch class, 0 = C
    int scaleIndex = 0;             // builtinScales() index; == builtinScales().size() means the custom scale
    RetuneMode retune = RetuneMode::Off;
    float retuneAmount = 1.0f;      // 0..1
    float retuneSpeedMs = 20.0f;    // 0 = instant
    float glideMs = 30.0f;          // harmony interval changes
    float gateDb = -50.0f;          // tracking threshold
    float refHz = 440.0f;           // A4 reference
    float humanize = 0.0f;          // 0..1, per-voice pitch and timing drift
    float dryDb = 0.0f;             // -60 = off
    float wetDb = 0.0f;
    float outputDb = 0.0f;
    std::array<VoiceParams, kNumVoices> voices {};

    // MIDI "Play Harmony": voice i sounds held MIDI note i (lowest first) instead of its interval.
    // -1 = no note held for that voice, which silences it. Notes are mapped through the
    // tuning the same way frets are (Snap / Remap), so a keyboard plays the microtonal scale too.
    bool midiHarmony = false;
    std::array<int, kNumVoices> midiNotes { -1, -1, -1, -1 };
};

// What the UI shows. Written by the audio thread, read by the editor.
struct Status
{
    std::atomic<float> detectedHz { 0 };
    std::atomic<float> inputCents { 0 };     // cents above the root (in the root's octave 4)
    std::atomic<bool> voiced { false };
    std::atomic<int> degree { 0 };           // scale degree the input is heard as
    std::atomic<float> leadCents { 0 };      // retuned lead pitch, cents above the root
    std::array<std::atomic<float>, kNumVoices> voiceCents {};
    std::array<std::atomic<bool>, kNumVoices> voiceOn {};
    std::atomic<float> inLevel { 0 }, outLevel { 0 };
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p) { params = p; }
    const Params& getParams() const { return params; }

    // Thread-safe: may be called from the message thread at any time.
    void setCustomScale (const Scale& s);

    // in/out are channel arrays; mono or stereo. In-place is allowed.
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // The scale currently used by the audio thread's settings (builtin or custom).
    const Scale& scaleFor (int scaleIndex) const;

    double rootHz() const;
    const Status& getStatus() const { return status; }

    static constexpr int kControlInterval = 32;

private:
    void control();

    double sr = 48000;
    Params params;
    Status status;

    PitchDetector detector;
    PitchShifter lead;
    std::array<PitchShifter, kNumVoices> shifters;
    Smoother corrSmooth;
    std::array<Smoother, kNumVoices> intervalSmooth, driftSmooth, delayDriftSmooth;
    std::array<float, kNumVoices> ratio {}, delaySamples {}, driftTarget {}, delayDriftTarget {};
    std::array<float, kNumVoices> gainL {}, gainR {}, curL {}, curR {};
    float leadRatio = 1, dryGain = 1, curDry = 1, wetGain = 1, outGain = 1, curOut = 1;
    bool leadActive = false;
    int controlCount = 0, driftCount = 0;
    bool havePitch = false;
    double lastCents = 0;
    float lastPeriod = 0;
    Rng rng;
    float inPeak = 0, outPeak = 0;

    Scale customScale, pendingScale;
    std::mutex scaleMutex;
    std::atomic<bool> scalePending { false };
};

// Pure helper used by the engine and the tests: given the input pitch (cents above the
// root), the scale and settings, compute the lead correction and each voice's shift, in cents.
struct HarmonyResult
{
    double correction = 0;      // applied to the lead (dry) signal
    int degree = 0;             // degree the input is heard as
    std::array<double, kNumVoices> shift {};   // total shift per voice, relative to the input
    std::array<bool, kNumVoices> active { true, true, true, true };   // false = MIDI voice with no note
};

// Cents above the root for a MIDI note, mapped through the tuning by the retune mode.
double midiNoteCents (int note, double rootHz, double refHz, const Scale& scale, RetuneMode mode);
HarmonyResult computeHarmony (double inputCents, const Scale& scale, const Params& p, double rootHz = 261.6256);
} // namespace cm
