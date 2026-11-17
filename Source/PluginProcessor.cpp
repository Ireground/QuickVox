#include "PluginProcessor.h"
#include "PluginEditor.h"

QuickVoxProcessor::QuickVoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QuickVox", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout QuickVoxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                   StringArray { "Follow", "Note", "MIDI" }, 0));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "note", 1 }, "Note", 24, 72, 48));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chord", 1 }, "Chord",
                                                   StringArray { "Off", "Octave", "Fifth", "Major", "Minor" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "snap", 1 }, "Snap", true));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "bands", 1 }, "Bands", 8, 40, 24));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant",
                                                  NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "air", 1 }, "Air",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "response", 1 }, "Response",
                                                  NormalisableRange<float> (2.0f, 200.0f, 0.1f, 0.4f), 25.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thick", 1 }, "Thick",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                  NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    return l;
}

bool QuickVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void QuickVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mono.setSize (1, samplesPerBlock);
    wet.setSize (1, samplesPerBlock);
    engine.prepare (sampleRate);
    mixSm.reset (sampleRate, 0.05);
    outSm.reset (sampleRate, 0.05);
    mixSm.setCurrentAndTargetValue (getRaw ("mix") * 0.01f);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (getRaw ("output")));
}

void QuickVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = getTotalNumOutputChannels();
    for (int c = getTotalNumInputChannels(); c < nch; ++c) buffer.clear (c, 0, n);
    if (n == 0) return;

    if (mono.getNumSamples() < n) { mono.setSize (1, n, false, false, true); wet.setSize (1, n, false, false, true); }

    qv::Params p;
    p.mode       = (int) getRaw ("mode");
    p.note       = (int) getRaw ("note");
    p.chord      = (int) getRaw ("chord");
    p.snap       = getRaw ("snap") > 0.5f;
    p.bands      = (int) getRaw ("bands");
    p.formant    = getRaw ("formant");
    p.air        = getRaw ("air") * 0.01f;
    p.responseMs = getRaw ("response");
    p.thick      = getRaw ("thick") * 0.01f;
    engine.setParams (p);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())                              engine.noteOn (m.getNoteNumber());
        else if (m.isNoteOff())                        engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    heldNotes.store (engine.heldCount());

    // modulator = the track itself (summed to mono)
    auto* m = mono.getWritePointer (0);
    juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
    for (int c = 1; c < nch; ++c) juce::FloatVectorOperations::add (m, buffer.getReadPointer (c), n);
    if (nch > 1) juce::FloatVectorOperations::multiply (m, 1.0f / (float) nch, n);

    auto* w = wet.getWritePointer (0);
    engine.process (m, w, n);

    mixSm.setTargetValue (getRaw ("mix") * 0.01f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (getRaw ("output")));
    float peak = 0.0f;
    for (int s = 0; s < n; ++s)
    {
        const float mx = mixSm.getNextValue(), g = outSm.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[s] = (d[s] * (1.0f - mx) + w[s] * mx) * g;
            peak = std::max (peak, std::abs (d[s]));
        }
    }
    outLevel.store (std::max (peak, outLevel.load() * 0.9f));
}

juce::AudioProcessorEditor* QuickVoxProcessor::createEditor() { return new QuickVoxEditor (*this); }

void QuickVoxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void QuickVoxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            uiScale = (float) t.getProperty ("uiScale", 1.0f);
            apvts.replaceState (t);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new QuickVoxProcessor(); }
