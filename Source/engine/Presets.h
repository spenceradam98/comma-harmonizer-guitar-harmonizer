// Factory presets. Scale names are looked up in builtinScales() so the list can be reordered.
#pragma once

#include <cstring>
#include "Engine.h"

namespace cm
{
struct Preset { const char* name; const char* category; Params p; };

inline int scaleIndexByName (const char* name)
{
    const auto& s = builtinScales();
    for (int i = 0; i < (int) s.size(); ++i)
        if (s[(size_t) i].getName() == name) return i;
    return 0;
}

inline VoiceParams voiceScale (int degrees, float levelDb, float pan, float cents = 0, float delayMs = 0)
{
    VoiceParams v; v.on = true; v.mode = VoiceMode::Scale; v.degrees = degrees;
    v.levelDb = levelDb; v.pan = pan; v.cents = cents; v.delayMs = delayMs; return v;
}

inline VoiceParams voiceInterval (int semis, float levelDb, float pan, float cents = 0, float delayMs = 0)
{
    VoiceParams v; v.on = true; v.mode = VoiceMode::Interval; v.semitones = semis;
    v.levelDb = levelDb; v.pan = pan; v.cents = cents; v.delayMs = delayMs; return v;
}

inline Params makePreset (int key, const char* scale, RetuneMode rt, std::initializer_list<VoiceParams> voices,
                          float dryDb = 0, float wetDb = 0, float humanize = 0.1f, float glideMs = 30)
{
    Params p;
    p.key = key; p.scaleIndex = scaleIndexByName (scale); p.retune = rt;
    p.dryDb = dryDb; p.wetDb = wetDb; p.humanize = humanize; p.glideMs = glideMs;
    int i = 0;
    for (auto& v : voices) if (i < kNumVoices) p.voices[(size_t) i++] = v;
    return p;
}

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        using R = RetuneMode;
        std::vector<Preset> v;
        // ---- Harmony (12-TET) ----
        v.push_back ({ "Twin Leads in 3rds (E minor)", "Harmony",
                       makePreset (4, "Natural Minor", R::Off, { voiceScale (2, -2, 0.45f) }, 0, 0, 0.1f) });
        v.push_back ({ "Twin Leads in 3rds (G major)", "Harmony",
                       makePreset (7, "Major (Ionian)", R::Off, { voiceScale (2, -2, 0.45f) }) });
        v.push_back ({ "Southern Rock 3rds + 5ths", "Harmony",
                       makePreset (9, "Mixolydian", R::Off, { voiceScale (2, -3, -0.6f), voiceScale (4, -5, 0.6f) }) });
        v.push_back ({ "Neo-Classical 6ths Below", "Harmony",
                       makePreset (9, "Harmonic Minor", R::Off, { voiceScale (-2, -2, -0.4f) }) });
        v.push_back ({ "Triad Stack", "Harmony",
                       makePreset (0, "Major (Ionian)", R::Off, { voiceScale (2, -4, -0.5f), voiceScale (4, -5, 0.5f), voiceScale (-7, -7, 0) }) });
        v.push_back ({ "Pentatonic 4ths", "Harmony",
                       makePreset (4, "Minor Pentatonic", R::Off, { voiceScale (2, -3, 0.5f) }) });
        // ---- Fixed intervals ----
        v.push_back ({ "Power Fifths", "Intervals",
                       makePreset (0, "Chromatic", R::Off, { voiceInterval (7, -3, 0), voiceInterval (-12, -6, 0) }) });
        v.push_back ({ "Octave Up 12-String", "Intervals",
                       makePreset (0, "Chromatic", R::Off, { voiceInterval (12, -6, -0.5f, 4, 6), voiceInterval (0, -6, 0.5f, -6, 12) }, 0, 0, 0.25f) });
        v.push_back ({ "Sub Octave Bass", "Intervals",
                       makePreset (0, "Chromatic", R::Off, { voiceInterval (-12, 0, 0) }) });
        v.push_back ({ "Wide Detune Doubler", "Intervals",
                       makePreset (0, "Chromatic", R::Off, { voiceInterval (0, -3, -0.9f, -9, 18), voiceInterval (0, -3, 0.9f, 9, 27) }, 0, 0, 0.4f) });
        v.push_back ({ "Shimmer Fifth + Octave", "Intervals",
                       makePreset (0, "Chromatic", R::Off, { voiceInterval (7, -8, -0.6f, 0, 60), voiceInterval (12, -9, 0.6f, 0, 110) }, 0, 0, 0.3f) });
        // ---- Microtonal simulation ----
        v.push_back ({ "Quarter-Tone Guitar (Remap)", "Microtonal",
                       makePreset (4, "24-EDO Quarter Tones", R::Remap, {}) });
        v.push_back ({ "Snap to Just Major", "Microtonal",
                       makePreset (0, "Just Major (5-limit)", R::Nearest, { voiceScale (2, -4, 0.5f) }) });
        v.push_back ({ "Pure Thirds (5-limit chromatic)", "Microtonal",
                       makePreset (0, "Just Chromatic (5-limit)", R::Nearest, { voiceInterval (4, -4, 0.5f, -14) }) });
        v.push_back ({ "Septimal Blues", "Microtonal",
                       makePreset (4, "Septimal Blues (7-limit)", R::Nearest, { voiceScale (2, -5, 0.4f) }) });
        v.push_back ({ "Maqam Rast Lead", "Microtonal",
                       makePreset (2, "Maqam Rast", R::Nearest, {}) });
        v.push_back ({ "Bayati Harmony", "Microtonal",
                       makePreset (2, "Maqam Bayati", R::Nearest, { voiceScale (-3, -5, 0.4f) }) });
        v.push_back ({ "19-EDO Fretboard", "Microtonal",
                       makePreset (4, "19-EDO", R::Remap, {}) });
        v.push_back ({ "31-EDO Meantone Snap", "Microtonal",
                       makePreset (0, "31-EDO", R::Nearest, { voiceScale (10, -4, 0.5f) }) });
        v.push_back ({ "Bohlen-Pierce Strange", "Microtonal",
                       makePreset (0, "Bohlen-Pierce (13 of 3:1)", R::Remap, { voiceScale (6, -5, 0.5f) }) });
        v.push_back ({ "Slendro Gamelan Guitar", "Microtonal",
                       makePreset (2, "Slendro (approx.)", R::Nearest, { voiceScale (2, -5, -0.5f), voiceScale (5, -8, 0.5f) }) });
        v.push_back ({ "Harmonic Series Choir", "Microtonal",
                       makePreset (0, "Harmonic Series 8-16", R::Nearest, { voiceScale (2, -5, -0.6f), voiceScale (4, -6, 0.6f), voiceScale (-8, -7, 0) }, 0, 0, 0.2f) });
        v.push_back ({ "Carlos Alpha Glide", "Microtonal",
                       makePreset (0, "Carlos Alpha (78c steps)", R::Remap, {}) });
        return v;
    }();
    return list;
}
} // namespace cm
