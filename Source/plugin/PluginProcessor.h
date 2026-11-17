#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

class CommaProcessor : public juce::AudioProcessor
{
public:
    CommaProcessor();
    ~CommaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI hooks (message thread) ----
    const cm::Status& getStatus() const { return engine.getStatus(); }
    cm::Params currentParams() const { return readParams(); }
    const cm::Scale& currentScale() const;
    double rootHz() const;

    // Loads a Scala .scl file as the custom scale and selects it. Returns an error message or "".
    juce::String loadScala (const juce::File& file);
    juce::String getCustomScaleName() const { return customScale.getName(); }

    int getMidiKey() const { return midiKey.load(); }   // -1 = none received
    std::array<int, cm::kNumVoices> getHeldNotes() const;

    juce::String getThemeName() const { return themeName; }
    void setThemeName (const juce::String& t) { themeName = t; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static int customScaleIndex() { return (int) cm::builtinScales().size(); }

private:
    cm::Params readParams() const;
    bool applyScalaText (const juce::String& text, juce::String& error);

    void handleMidi (const juce::MidiBuffer&);

    cm::Engine engine;
    std::atomic<int> midiKey { -1 };
    std::array<std::atomic<int>, 16> held {};   // held MIDI notes in press order, -1 = empty (audio thread writes)
    cm::Scale customScale;
    juce::String customScaleText;
    juce::String themeName { "Midnight" };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommaProcessor)
};
