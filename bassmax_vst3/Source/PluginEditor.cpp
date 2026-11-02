// PluginEditor.cpp — BassMax 界面实现
#include "PluginEditor.h"

BassMaxAudioProcessorEditor::BassMaxAudioProcessorEditor (BassMaxAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      bassAttach  (p.apvts, "bass",  bassSlider),
      driveAttach (p.apvts, "drive", driveSlider)
{
    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredLeft);
        l.attachToComponent (&s, true);
        addAndMakeVisible (l);
    };
    setup (bassSlider,  bassLabel,  "Bass (dB)");
    setup (driveSlider, driveLabel, "Drive");

    setSize (420, 170);
}

void BassMaxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawFittedText ("BassMax", getLocalBounds().removeFromTop (34),
                      juce::Justification::centred, 1);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText ("64-bit clean-room rewrite  |  bass shelf + limiter + exciter",
                      getLocalBounds().removeFromBottom (16),
                      juce::Justification::centred, 1);
}

void BassMaxAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16).withTrimmedTop (40).withTrimmedBottom (16);
    bassSlider.setBounds  (r.removeFromTop (44).withTrimmedLeft (80));
    r.removeFromTop (16);
    driveSlider.setBounds (r.removeFromTop (44).withTrimmedLeft (80));
}
