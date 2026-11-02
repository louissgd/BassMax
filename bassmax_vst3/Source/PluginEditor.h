// PluginEditor.h — BassMax 简易界面 (低音 / 驱动 两个滑块)
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BassMaxAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BassMaxAudioProcessorEditor (BassMaxAudioProcessor&);
    ~BassMaxAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    BassMaxAudioProcessor& proc;
    juce::Slider bassSlider, driveSlider;
    juce::Label  bassLabel, driveLabel;
    APVTS::SliderAttachment bassAttach, driveAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassMaxAudioProcessorEditor)
};
