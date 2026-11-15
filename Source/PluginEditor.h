#pragma once
#include "PluginProcessor.h"

// Neon-style rotary knobs
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

class NeonTuneEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit NeonTuneEditor (NeonTuneProcessor&);
    ~NeonTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NeonTuneProcessor& proc;
    NeonLookAndFeel laf;

    juce::ComboBox keyBox, scaleBox;
    juce::Slider retuneSlider, amountSlider, mixSlider;
    juce::Label retuneLabel, amountLabel, mixLabel;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAtt>  keyAtt, scaleAtt;
    std::unique_ptr<SliderAtt> retuneAtt, amountAtt, mixAtt;

    juce::Rectangle<int> displayArea;
    juce::String noteText { "--" };
    float displayDev = 0.0f;     // smoothed (detected - target), in semitones
    bool  active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonTuneEditor)
};
