#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class QuickVoxEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit QuickVoxEditor (QuickVoxProcessor&);
    ~QuickVoxEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    enum Kind { None, ModeBtn, ChordBtn, SnapBtn, NoteBox, Slider, SizeBtn };
    struct Target
    {
        Kind kind = None; int index = -1;
        bool operator== (const Target& o) const { return kind == o.kind && index == o.index; }
    };
    struct SliderDef
    {
        const char* id; const char* label; juce::Rectangle<float> row; float labelW, valueW; juce::Colour col;
    };

    Target findTarget (juce::Point<float>) const;
    juce::RangedAudioParameter* param (const char* id) const { return proc.apvts.getParameter (id); }
    float value (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setNorm (const char* id, float n);
    void setValue (const char* id, float v) { if (auto* p = param (id)) setNorm (id, p->convertTo0to1 (v)); }
    void clickSet (const char* id, float v);
    juce::String formatValue (int slider) const;
    juce::Rectangle<float> trackRect (const SliderDef&) const;
    static juce::String noteName (int midi);
    void openSizeMenu();

    float uiScale() const { return (float) getWidth() / (float) W; }
    juce::Point<float> pos (const juce::MouseEvent& e) const { return e.position / uiScale(); }

    void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour dot) const;
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour fill,
                   juce::Colour textCol, float fontSize = 11.0f) const;

    static constexpr int W = 760, H = 392;

    QuickVoxProcessor& proc;

    juce::Rectangle<float> pHeader, pCarrier, pSound, pBands, pOut, noteBox, bSnap, bSize, meterArea, levelBar;
    std::array<juce::Rectangle<float>, 3> modeBtns;
    std::array<juce::Rectangle<float>, 5> chordBtns;
    std::vector<SliderDef> sliders;

    std::array<float, qv::Engine::MaxBands> bandDisp {};
    float levelDisp = 0.0f;

    Target hover, active;
    float dragStartX = 0, dragStartY = 0, dragStartVal = 0;
    bool dragFine = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuickVoxEditor)
};
