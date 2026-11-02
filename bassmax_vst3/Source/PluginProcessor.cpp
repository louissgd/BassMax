// PluginProcessor.cpp — BassMax VST3 实现
#include "PluginProcessor.h"
#include "PluginEditor.h"

BassMaxAudioProcessor::BassMaxAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    bassParam  = apvts.getRawParameterValue ("bass");
    driveParam = apvts.getRawParameterValue ("drive");
    presetParam = apvts.getRawParameterValue ("preset");
}

juce::AudioProcessorValueTreeState::ParameterLayout
BassMaxAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // 低音: 0..12 dB, 默认 4.5 (对应实测低架 +4.5dB@120Hz)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bass", 1 }, "Bass",
        juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 0.0f));
    // 驱动(压缩): 0..1, 默认 0 (对应原插件默认压缩度)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "drive", 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.1f), 0.0f));
    // 音效风格 (原版界面 6 个 EQ 预设)
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "preset", 1 }, "Style",
        juce::StringArray { "标准乐", "古典乐", "俱乐部", "流行乐", "爵士乐", "摇滚乐" }, 0));
    return layout;
}

void BassMaxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    core.reset ((float) currentSampleRate);
    lastBass = lastDrive = lastPreset = -999.0f;
    updateParams();
    scratch.assign ((size_t) juce::jmax (samplesPerBlock, 1024) * 2, 0.0f);
}

void BassMaxAudioProcessor::releaseResources() {}

void BassMaxAudioProcessor::reset()
{
    core.reset ((float) currentSampleRate);
    lastBass = lastDrive = lastPreset = -999.0f;
}

bool BassMaxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void BassMaxAudioProcessor::updateParams()
{
    if (bassParam)   params.bass   = bassParam->load();
    if (driveParam)  params.drive  = driveParam->load();
    if (presetParam) params.preset = (int) presetParam->load();
    if (params.bass != lastBass || params.drive != lastDrive || (float) params.preset != lastPreset)
    {
        core.setParams (params, (float) currentSampleRate);
        lastBass   = params.bass;
        lastDrive  = params.drive;
        lastPreset = (float) params.preset;
    }
}

void BassMaxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n  = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();
    if (n <= 0 || ch <= 0) return;

    updateParams();

    const size_t need = (size_t) n * 2;
    if (scratch.size() < need) scratch.resize (need);

    if (ch == 1)
    {
        const float* in = buffer.getReadPointer (0);
        for (int i = 0; i < n; ++i) { const float s = in[i]; scratch[2*i] = s; scratch[2*i+1] = s; }
        core.process (scratch.data(), n, (float) currentSampleRate);
        float* out = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) out[i] = scratch[2*i];
    }
    else
    {
        const float* inL = buffer.getReadPointer (0);
        const float* inR = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i) { scratch[2*i] = inL[i]; scratch[2*i+1] = inR[i]; }
        core.process (scratch.data(), n, (float) currentSampleRate);
        float* outL = buffer.getWritePointer (0);
        float* outR = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i) { outL[i] = scratch[2*i]; outR[i] = scratch[2*i+1]; }
        for (int c = 2; c < ch; ++c) buffer.clear (c, 0, n);
    }
}

juce::AudioProcessorEditor* BassMaxAudioProcessor::createEditor()
{
    return new BassMaxAudioProcessorEditor (*this);
}

void BassMaxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BassMaxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// VST3/宿主入口
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassMaxAudioProcessor();
}
