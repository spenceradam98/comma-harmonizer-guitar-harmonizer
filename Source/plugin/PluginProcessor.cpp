#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String vid (int v, const char* name) { return "v" + juce::String (v + 1) + "_" + name; }
}

juce::AudioProcessorValueTreeState::ParameterLayout CommaProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    StringArray keys;
    for (int i = 0; i < 12; ++i) keys.add (cm::noteName (i));
    StringArray scales;
    for (auto& s : cm::builtinScales()) scales.add (s.getName());
    scales.add ("Custom (.scl)");

    auto ms = [] (float v, int) { return String (v, 0) + " ms"; };
    auto db = [] (float v, int) { return v <= -59.9f ? String ("Off") : String (v, 1) + " dB"; };
    auto pct = [] (float v, int) { return String (v, 0) + "%"; };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keys, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scales, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "retune", 1 }, "Retune",
                                                         StringArray { "Off", "Snap to Scale", "Remap Frets" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "midiMode", 1 }, "MIDI Mode",
                                                         StringArray { "MIDI Off", "MIDI Sets Key", "MIDI Plays Harmony" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "retuneAmt", 1 }, "Retune Amount",
                                                        NormalisableRange<float> (0, 100, 0.1f), 100.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "retuneSpeed", 1 }, "Retune Speed",
                                                        NormalisableRange<float> (0, 500, 0.1f, 0.4f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Harmony Glide",
                                                        NormalisableRange<float> (0, 500, 0.1f, 0.4f), 30.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, "Tracking Gate",
                                                        NormalisableRange<float> (-80, -20, 0.1f), -50.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "refHz", 1 }, "A4 Reference",
                                                        NormalisableRange<float> (415, 466, 0.1f), 440.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize",
                                                        NormalisableRange<float> (0, 100, 0.1f), 10.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    auto level = [&] (const char* id, const char* name, float def, float lo = -60, float hi = 6)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f, 2.0f), def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    };
    level ("dry", "Dry Level", 0);
    level ("wet", "Harmony Level", 0);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                        NormalisableRange<float> (-24, 12, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        const String n = "Voice " + String (v + 1) + " ";
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { vid (v, "on"), 1 }, n + "On", v == 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { vid (v, "mode"), 1 }, n + "Mode", StringArray { "Interval", "Scale" }, 1));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { vid (v, "semis"), 1 }, n + "Semitones", -24, 24, 7));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { vid (v, "degrees"), 1 }, n + "Scale Steps", -14, 14, v == 0 ? 2 : (v == 1 ? 4 : (v == 2 ? -3 : -7))));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { vid (v, "cents"), 1 }, n + "Detune",
                                                            NormalisableRange<float> (-100, 100, 0.1f), 0.0f,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return (x > 0 ? "+" : "") + String (x, 1) + " c"; })));
        level (vid (v, "level").toRawUTF8(), (n + "Level").toRawUTF8(), -3);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { vid (v, "pan"), 1 }, n + "Pan",
                                                            NormalisableRange<float> (-100, 100, 1), v == 0 ? 35.0f : (v == 1 ? -35.0f : 0.0f),
                                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) {
                                                                return std::abs (x) < 0.5f ? String ("C") : (x < 0 ? "L" : "R") + String (std::abs (x), 0); })));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { vid (v, "delay"), 1 }, n + "Delay",
                                                            NormalisableRange<float> (0, 150, 0.1f, 0.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    }
    return { p.begin(), p.end() };
}

CommaProcessor::CommaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Comma", createLayout())
{
    customScale = cm::builtinScales().front();
    customScale.setName ("No .scl loaded");
    for (auto& h : held) h.store (-1);
    setCurrentProgram (0);   // start on the first factory preset
}

bool CommaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    const bool okOut = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool okIn = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return okOut && okIn;
}

void CommaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
}

cm::Params CommaProcessor::readParams() const
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    cm::Params p;
    p.key = (int) v ("key");
    p.scaleIndex = (int) v ("scale");
    p.retune = (cm::RetuneMode) (int) v ("retune");
    p.retuneAmount = v ("retuneAmt") / 100.0f;
    p.retuneSpeedMs = v ("retuneSpeed");
    p.glideMs = v ("glide");
    p.gateDb = v ("gate");
    p.refHz = v ("refHz");
    p.humanize = v ("humanize") / 100.0f;
    p.dryDb = v ("dry");
    p.wetDb = v ("wet");
    p.outputDb = v ("output");
    const int midiMode = (int) v ("midiMode");
    if (midiMode == 1 && midiKey.load() >= 0) p.key = midiKey.load();
    if (midiMode == 2)
    {
        p.midiHarmony = true;
        p.midiNotes = getHeldNotes();
    }
    for (int i = 0; i < cm::kNumVoices; ++i)
    {
        auto& vp = p.voices[(size_t) i];
        vp.on = v (vid (i, "on")) > 0.5f;
        vp.mode = (cm::VoiceMode) (int) v (vid (i, "mode"));
        vp.semitones = (int) std::lround (v (vid (i, "semis")));
        vp.degrees = (int) std::lround (v (vid (i, "degrees")));
        vp.cents = v (vid (i, "cents"));
        vp.levelDb = v (vid (i, "level"));
        vp.pan = v (vid (i, "pan")) / 100.0f;
        vp.delayMs = v (vid (i, "delay"));
    }
    return p;
}

std::array<int, cm::kNumVoices> CommaProcessor::getHeldNotes() const
{
    // the four most recently pressed notes, sorted low to high: voice 1 = lowest
    std::array<int, 16> all {};
    int n = 0;
    for (auto& h : held) { const int x = h.load(); if (x >= 0) all[(size_t) n++] = x; }
    const int first = std::max (0, n - cm::kNumVoices);
    std::array<int, cm::kNumVoices> out { -1, -1, -1, -1 };
    int m = 0;
    for (int i = first; i < n; ++i) out[(size_t) m++] = all[(size_t) i];
    std::sort (out.begin(), out.begin() + m);
    return out;
}

void CommaProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            midiKey.store (note % 12);
            // remove if already held, then append (press order is kept by compacting)
            std::array<int, 16> list {};
            int n = 0;
            for (auto& h : held) { const int x = h.load(); if (x >= 0 && x != note) list[(size_t) n++] = x; }
            if (n == 16) { std::copy (list.begin() + 1, list.end(), list.begin()); --n; }
            list[(size_t) n++] = note;
            for (int i = 0; i < 16; ++i) held[(size_t) i].store (i < n ? list[(size_t) i] : -1);
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            std::array<int, 16> list {};
            int n = 0;
            for (auto& h : held) { const int x = h.load(); if (x >= 0 && x != note) list[(size_t) n++] = x; }
            for (int i = 0; i < 16; ++i) held[(size_t) i].store (i < n ? list[(size_t) i] : -1);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& h : held) h.store (-1);
        }
    }
}

void CommaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    engine.setParams (readParams());
    engine.process (buffer.getArrayOfReadPointers(), inCh, buffer.getArrayOfWritePointers(), outCh, buffer.getNumSamples());
}

const cm::Scale& CommaProcessor::currentScale() const
{
    const int idx = (int) apvts.getRawParameterValue ("scale")->load();
    return idx < customScaleIndex() ? cm::builtinScales()[(size_t) idx] : customScale;
}

double CommaProcessor::rootHz() const
{
    const auto p = readParams();
    return p.refHz * std::pow (2.0, (p.key - 9) / 12.0);
}

bool CommaProcessor::applyScalaText (const juce::String& text, juce::String& error)
{
    cm::Scale s;
    std::string err;
    if (! cm::parseScala (text.toStdString(), s, err)) { error = err; return false; }
    customScale = s;
    customScaleText = text;
    engine.setCustomScale (s);
    return true;
}

juce::String CommaProcessor::loadScala (const juce::File& file)
{
    if (file.getSize() > 256 * 1024) return "File is too large to be a scale";
    juce::String error;
    if (! applyScalaText (file.loadFileAsString(), error)) return error;
    if (auto* prm = apvts.getParameter ("scale"))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) customScaleIndex()));
    return {};
}

int CommaProcessor::getNumPrograms() { return (int) cm::factoryPresets().size(); }

const juce::String CommaProcessor::getProgramName (int index)
{
    const auto& ps = cm::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) ps.size()) ? juce::String (ps[(size_t) index].name) : juce::String();
}

void CommaProcessor::setCurrentProgram (int index)
{
    const auto& ps = cm::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    const auto& p = ps[(size_t) index].p;
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("key", (float) p.key);
    set ("scale", (float) p.scaleIndex);
    set ("retune", (float) (int) p.retune);
    set ("retuneAmt", p.retuneAmount * 100.0f);
    set ("retuneSpeed", p.retuneSpeedMs);
    set ("glide", p.glideMs);
    set ("humanize", p.humanize * 100.0f);
    set ("dry", p.dryDb);
    set ("wet", p.wetDb);
    for (int i = 0; i < cm::kNumVoices; ++i)
    {
        const auto& vp = p.voices[(size_t) i];
        set (vid (i, "on"), vp.on ? 1.0f : 0.0f);
        set (vid (i, "mode"), (float) (int) vp.mode);
        set (vid (i, "semis"), (float) vp.semitones);
        set (vid (i, "degrees"), (float) vp.degrees);
        set (vid (i, "cents"), vp.cents);
        set (vid (i, "level"), vp.levelDb);
        set (vid (i, "pan"), vp.pan * 100.0f);
        set (vid (i, "delay"), vp.delayMs);
    }
}

void CommaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("theme", themeName, nullptr);
    state.setProperty ("customScl", customScaleText, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void CommaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            themeName = tree.getProperty ("theme", "Midnight").toString();
            const auto scl = tree.getProperty ("customScl", "").toString();
            juce::String err;
            if (scl.isNotEmpty()) applyScalaText (scl, err);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* CommaProcessor::createEditor() { return new CommaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CommaProcessor(); }
