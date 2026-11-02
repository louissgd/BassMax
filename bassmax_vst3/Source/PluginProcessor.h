// PluginProcessor.h — BassMax VST3 (JUCE AudioProcessor)
// 包装经黑盒校准、验证的 bassmax::Core DSP 核心 (clean-room 重写)。
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "bassmax_core.hpp"

class BassMaxAudioProcessor : public juce::AudioProcessor
{
public:
    BassMaxAudioProcessor();
    ~BassMaxAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BassMax"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* bassParam  = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* presetParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateParams();

    bassmax::Core     core;
    bassmax::Params   params;
    double            currentSampleRate = 44100.0;
    std::vector<float> scratch;          // 交错立体声临时缓冲
    float lastBass = -999.0f, lastDrive = -999.0f, lastPreset = -999.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassMaxAudioProcessor)
};
