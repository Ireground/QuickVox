#pragma once
#include <JuceHeader.h>
#include "VocoderEngine.h"

class QuickVoxProcessor : public juce::AudioProcessor
{
public:
    QuickVoxProcessor();
    ~QuickVoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "QuickVox"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    qv::Engine engine;
    std::atomic<float> uiScale { 1.0f };
    std::atomic<float> outLevel { 0.0f };
    std::atomic<int> heldNotes { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float getRaw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioBuffer<float> mono, wet;
    juce::SmoothedValue<float> mixSm, outSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuickVoxProcessor)
};
