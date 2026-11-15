#include "PluginEditor.h"

namespace
{
    const juce::Colour cyan   { 0xff00e5ff };
    const juce::Colour pink   { 0xffff2fb9 };
    const juce::Colour bgTop  { 0xff0d0b1f };
    const juce::Colour bgBot  { 0xff2a1250 };
}

//==============================================================================
NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b1535));
    setColour (juce::ComboBox::outlineColourId, cyan.withAlpha (0.6f));
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::arrowColourId, cyan);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1535));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pink.withAlpha (0.6f));
    setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                        float pos, float startAngle, float endAngle,
                                        juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (10.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2a2450));
    g.strokePath (track, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (cyan.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (cyan);
    g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const float body = radius * 0.68f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2f6b), centre.x, centre.y - body,
                                             juce::Colour (0xff120e26), centre.x, centre.y + body, false));
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.8f, -body + 3.0f, 3.6f, body * 0.45f, 1.5f);
    g.setColour (pink);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

//==============================================================================
NeonTuneEditor::NeonTuneEditor (NeonTuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (560, 380);

    const juce::StringArray keys { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const juce::StringArray scales { "Chromatic","Major","Minor","Minor Pent.","Major Pent." };
    keyBox.addItemList (keys, 1);
    scaleBox.addItemList (scales, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name,
                             const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setupKnob (retuneSlider, retuneLabel, "RETUNE SPEED", " ms");
    setupKnob (amountSlider, amountLabel, "AMOUNT", "");
    setupKnob (mixSlider,    mixLabel,    "MIX", "");

    // show amount / mix as percentages
    for (auto* s : { &amountSlider, &mixSlider })
    {
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        s->valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    }

    keyAtt    = std::make_unique<ComboAtt>  (proc.apvts, "key",    keyBox);
    scaleAtt  = std::make_unique<ComboAtt>  (proc.apvts, "scale",  scaleBox);
    retuneAtt = std::make_unique<SliderAtt> (proc.apvts, "retune", retuneSlider);
    amountAtt = std::make_unique<SliderAtt> (proc.apvts, "amount", amountSlider);
    mixAtt    = std::make_unique<SliderAtt> (proc.apvts, "mix",    mixSlider);

    startTimerHz (30);
}

NeonTuneEditor::~NeonTuneEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void NeonTuneEditor::timerCallback()
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    const float d = proc.detectedMidi.load();
    const float c = proc.correctedMidi.load();

    if (d < 0.0f || c < 0.0f)
    {
        active = false;
        noteText = "--";
        displayDev += 0.2f * (0.0f - displayDev);
    }
    else
    {
        active = true;
        const int n = juce::roundToInt (c);
        noteText = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
        displayDev += 0.35f * ((d - c) - displayDev);
    }
    repaint (displayArea);
}

void NeonTuneEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0, 0, bgBot, 0, (float) getHeight(), false));
    g.fillAll();

    // title
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.setColour (cyan.withAlpha (0.35f));
    g.drawText ("NEONTUNE", 22, 10, 300, 40, juce::Justification::centredLeft);
    g.setColour (cyan);
    g.drawText ("NEONTUNE", 20, 8, 300, 40, juce::Justification::centredLeft);

    // pitch display
    auto panel = displayArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (panel, 14.0f);
    g.setColour (cyan.withAlpha (0.45f));
    g.drawRoundedRectangle (panel, 14.0f, 1.5f);

    g.setFont (juce::FontOptions (46.0f, juce::Font::bold));
    g.setColour (active ? juce::Colours::white : juce::Colours::white.withAlpha (0.3f));
    g.drawText (noteText, panel.removeFromLeft (panel.getWidth() * 0.35f).toNearestInt(),
                juce::Justification::centred);

    // cents meter (+/- 50 cents)
    auto meter = panel.reduced (20.0f, 0.0f);
    const float cy = meter.getCentreY();
    g.setColour (juce::Colour (0xff2a2450));
    g.fillRoundedRectangle (meter.getX(), cy - 3.0f, meter.getWidth(), 6.0f, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (meter.getCentreX() - 1.0f, cy - 16.0f, 2.0f, 32.0f);

    if (active)
    {
        const float norm = juce::jlimit (-1.0f, 1.0f, displayDev / 0.5f);
        const float nx = meter.getCentreX() + norm * meter.getWidth() * 0.5f;
        g.setColour (pink.withAlpha (0.35f));
        g.fillEllipse (nx - 13.0f, cy - 13.0f, 26.0f, 26.0f);
        g.setColour (pink);
        g.fillEllipse (nx - 7.0f, cy - 7.0f, 14.0f, 14.0f);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawText (juce::String (juce::roundToInt (displayDev * 100.0f)) + " cents",
                    meter.toNearestInt().withY (juce::roundToInt (cy + 18.0f)).withHeight (20),
                    juce::Justification::centred);
    }
}

void NeonTuneEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (44);   // title

    auto row = area.removeFromTop (30);
    keyBox.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    scaleBox.setBounds (row.removeFromLeft (160));

    area.removeFromTop (14);
    displayArea = area.removeFromTop (100);

    area.removeFromTop (14);
    const int w = area.getWidth() / 3;
    auto place = [&area, w] (juce::Slider& s, juce::Label& l)
    {
        auto col = area.removeFromLeft (w);
        l.setBounds (col.removeFromTop (22));
        s.setBounds (col);
    };
    place (retuneSlider, retuneLabel);
    place (amountSlider, amountLabel);
    place (mixSlider,    mixLabel);
}
