// Engine tests: run with ctest or directly. No framework, exits non-zero on failure.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Engine.h"
#include "Presets.h"

using namespace cm;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::vector<float> sine (double hz, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2 * kPi * hz * i / sr);
    return v;
}

// a guitar-ish tone: harmonics with decaying amplitude
static std::vector<float> pluck (double hz, double sr, int n)
{
    std::vector<float> v ((size_t) n, 0.0f);
    for (int h = 1; h <= 8; ++h)
        for (int i = 0; i < n; ++i)
            v[(size_t) i] += (0.4f / h) * (float) std::sin (2 * kPi * hz * h * i / sr);
    return v;
}

static float detect (const std::vector<float>& x, double sr, int skip)
{
    PitchDetector d;
    d.prepare (sr);
    d.setGateDb (-70);
    float f = 0;
    for (size_t i = (size_t) skip; i < x.size(); ++i)
        if (d.push (x[i]) && d.isVoiced()) f = d.getFrequency();
    return f;
}

static double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }

static void testDetector()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double hz : { 82.41, 110.0, 196.0, 329.63, 659.25, 1046.5 })
        {
            const float f = detect (pluck (hz, sr, (int) (sr * 0.3)), sr, 0);
            CHECK (std::abs (centsBetween (f, hz)) < 5.0, "detector %.1f Hz at %.0f -> %.2f Hz", hz, sr, f);
        }
    // silence is unvoiced
    PitchDetector d; d.prepare (48000); d.setGateDb (-50);
    bool anyVoiced = false;
    for (int i = 0; i < 48000; ++i) if (d.push (0.0f) && d.isVoiced()) anyVoiced = true;
    CHECK (! anyVoiced, "silence detected as voiced");
}

static void testShifter()
{
    const double sr = 48000;
    for (double semis : { -12.0, -5.0, 3.0, 4.0, 7.0, 12.0 })
    {
        const double hz = 196.0;
        const double ratio = std::pow (2.0, semis / 12.0);
        PitchShifter s; s.prepare (sr);
        s.setRatio ((float) ratio);
        s.setPeriod ((float) (sr / hz));
        auto x = pluck (hz, sr, (int) (sr * 0.6));
        for (auto& v : x) v = s.process (v);
        const float f = detect (x, sr, (int) (sr * 0.1));
        CHECK (std::abs (centsBetween (f, hz * ratio)) < 12.0, "shifter %+.0f st: expected %.1f got %.1f", semis, hz * ratio, f);
    }
    // unity ratio: a clean (single-head) delay, so energy matches the input (no comb filtering)
    PitchShifter s; s.prepare (sr); s.setRatio (1.0f);
    auto x = sine (1000.0, sr, (int) sr);
    double ein = 0, eout = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float y = s.process (x[i]);
        if (i > x.size() / 2) { ein += x[i] * x[i]; eout += y * y; }
    }
    CHECK (std::abs (10 * std::log10 (eout / ein)) < 0.5, "unity shifter level change %.2f dB", 10 * std::log10 (eout / ein));
}

static void testScales()
{
    const auto& b = builtinScales();
    CHECK (b.size() > 30, "only %d builtin scales", (int) b.size());
    for (auto& s : b) CHECK (s.isValid(), "invalid builtin %s", s.getName().c_str());

    const auto& major = b[(size_t) scaleIndexByName ("Major (Ionian)")];
    CHECK (major.degreeCents (0) == 0 && major.degreeCents (2) == 400 && major.degreeCents (7) == 1200, "major degrees");
    CHECK (major.degreeCents (-1) == -100, "degree -1 should be B below C");
    CHECK (major.nearestDegree (390) == 2, "nearest to 390c should be E");
    CHECK (major.nearestDegree (-90) == -1, "nearest to -90c should be B below");

    // diatonic thirds in C major: E (400) -> G (700) is a minor third, C -> E major third
    Params p; p.scaleIndex = scaleIndexByName ("Major (Ionian)");
    p.voices[0] = voiceScale (2, 0, 0);
    auto h = computeHarmony (400, major, p);
    CHECK (std::abs (h.shift[0] - 300) < 1e-9, "3rd above E in C major should be +300c, got %.1f", h.shift[0]);
    h = computeHarmony (0, major, p);
    CHECK (std::abs (h.shift[0] - 400) < 1e-9, "3rd above C should be +400c");
    h = computeHarmony (1100, major, p);   // B -> D
    CHECK (std::abs (h.shift[0] - 300) < 1e-9, "3rd above B should be +300c");

    // just intonation snap: a 12-TET E (400c) is pulled to 386.3c
    const auto& just = b[(size_t) scaleIndexByName ("Just Major (5-limit)")];
    p.retune = RetuneMode::Nearest; p.retuneAmount = 1;
    h = computeHarmony (400, just, p);
    CHECK (std::abs (h.correction + 13.686) < 0.01, "JI correction of E should be -13.69c, got %.3f", h.correction);

    // remap: on 24-EDO, fret 4 above the root sounds 200c (two quarter tones per... 4 x 50c)
    const auto& qt = b[(size_t) scaleIndexByName ("24-EDO Quarter Tones")];
    p.retune = RetuneMode::Remap;
    h = computeHarmony (400, qt, p);
    CHECK (std::abs (h.correction + 200) < 1e-9, "24-EDO remap of fret 4 should correct -200c, got %.1f", h.correction);
    h = computeHarmony (410, qt, p);   // bends survive
    CHECK (std::abs ((410 + h.correction) - 210) < 1e-9, "remap should keep 10c of bend");
}

static void testScala()
{
    Scale s; std::string err;
    const char* scl = "! test.scl\n!\nMy meantone\n 3\n!\n 193.157 cents\n 5/4\n 2\n";
    CHECK (parseScala (scl, s, err), "scala parse: %s", err.c_str());
    CHECK (s.n == 3 && std::abs (s.steps[1] - 386.3137) < 0.001 && std::abs (s.period() - 1200) < 1e-9, "scala values");
    CHECK (s.getName() == "My meantone", "scala name '%s'", s.getName().c_str());
    CHECK (! parseScala ("bad\nx\n", s, err), "bad file should fail");
    CHECK (! parseScala ("desc\n2\n300.0\n200.0\n", s, err), "descending should fail");
}

static void testMidiHarmony()
{
    const auto& b = builtinScales();
    const auto& chrom = b[(size_t) scaleIndexByName ("Chromatic")];
    const double root = 261.6256;   // C4
    Params p; p.midiHarmony = true;
    p.voices[0] = voiceScale (2, 0, 0);
    p.voices[1] = voiceScale (2, 0, 0, 10);
    p.midiNotes = { 64, 67, -1, -1 };   // E4, G4
    // input A3 (-300c from C4): voice 1 must land on E4 (+400c) => shift +700
    auto h = computeHarmony (-300, chrom, p, root);
    CHECK (std::abs (h.shift[0] - 700) < 0.01, "MIDI voice 1 shift %.2f", h.shift[0]);
    CHECK (std::abs (h.shift[1] - 1010) < 0.01, "MIDI voice 2 shift incl. detune %.2f", h.shift[1]);
    CHECK (h.active[0] && h.active[1] && ! h.active[2] && ! h.active[3], "MIDI active flags");
    // through a tuning: E4 snapped to just major is 386.3c
    const auto& just = b[(size_t) scaleIndexByName ("Just Major (5-limit)")];
    CHECK (std::abs (midiNoteCents (64, root, 440, just, RetuneMode::Nearest) - 386.3137) < 0.01, "MIDI E4 in just major");
    // remap: on 24-EDO, E4 (4 keys up) becomes 200c
    const auto& qt = b[(size_t) scaleIndexByName ("24-EDO Quarter Tones")];
    CHECK (std::abs (midiNoteCents (64, root, 440, qt, RetuneMode::Remap) - 200) < 0.01, "MIDI remap 24-EDO");
}

static void testEngine()
{
    const double sr = 48000;
    Engine e;
    Params p; p.key = 7; p.scaleIndex = scaleIndexByName ("Major (Ionian)");
    p.voices[0] = voiceScale (2, 0, 0);   // 3rd above
    p.dryDb = -60;                         // wet only
    p.glideMs = 0;
    e.setParams (p);
    e.prepare (sr, 512);
    // play B3 (246.94 Hz) in G major -> 3rd above is D4 (293.66)
    auto x = pluck (246.94, sr, (int) (sr * 0.8));
    std::vector<float> outL (x.size()), outR (x.size());
    for (size_t i = 0; i < x.size(); i += 256)
    {
        const int n = (int) std::min<size_t> (256, x.size() - i);
        const float* in[] = { x.data() + i };
        float* out[] = { outL.data() + i, outR.data() + i };
        e.process (in, 1, out, 2, n);
    }
    const float f = detect (outL, sr, (int) (sr * 0.3));
    CHECK (std::abs (centsBetween (f, 293.66)) < 15.0, "engine 3rd above B in G major: expected D 293.7, got %.1f", f);
    CHECK (e.getStatus().degree.load() == 2, "B should be degree 2 of G major, got %d", e.getStatus().degree.load());

    // presets reference valid scales
    for (auto& pr : factoryPresets())
        CHECK (pr.p.scaleIndex >= 0 && pr.p.scaleIndex < (int) builtinScales().size(), "preset %s", pr.name);
    CHECK (factoryPresets().size() >= 20, "preset count");
}

int main()
{
    testDetector();
    testShifter();
    testScales();
    testScala();
    testMidiHarmony();
    testEngine();
    if (failures == 0) std::printf ("All engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
