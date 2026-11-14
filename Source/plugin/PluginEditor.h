#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Themes.h"

class CommaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CommaEditor (CommaProcessor&);
    ~CommaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, bool bipolar = false);
    void placeKnob (Knob&, juce::Rectangle<int> area);
    void applyTheme (const juce::String& name);
    void rebuildPresetBox();
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void drawWheel (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);
    juce::String describeVoice (int v) const;
    static juce::String noteWithCents (double hz);
    static juce::String stepName (int degrees, int scaleSize);

    CommaProcessor& proc;
    CommaLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox presetBox, themeBox, keyBox, scaleBox, retuneBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, loadScl { "Load .scl" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttach, scaleAttach, retuneAttach;

    struct VoiceUi
    {
        juce::ToggleButton on;
        juce::ComboBox mode;
        Knob *semis = nullptr, *degrees = nullptr, *cents = nullptr, *level = nullptr, *pan = nullptr, *delay = nullptr;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach;
        juce::Rectangle<int> strip, infoArea;
    };
    std::array<VoiceUi, cm::kNumVoices> voices;

    Knob *amount = nullptr, *speed = nullptr, *glide = nullptr, *refHz = nullptr, *gate = nullptr, *humanize = nullptr,
         *dry = nullptr, *wet = nullptr, *output = nullptr;

    juce::Rectangle<int> header, pitchPanel, tuningPanel, mixPanel, wheelArea, readoutArea, meterArea, sclInfoArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image grainTexture;
    float inMeter = 0, outMeter = 0;
    juce::String sclStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommaEditor)
};
