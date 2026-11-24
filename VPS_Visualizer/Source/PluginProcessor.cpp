#include "PluginProcessor.h"
#include "PluginEditor.h"

VPS_VisualizerAudioProcessor::VPS_VisualizerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

VPS_VisualizerAudioProcessor::~VPS_VisualizerAudioProcessor() {}

const juce::String VPS_VisualizerAudioProcessor::getName() const { return JucePlugin_Name; }
bool VPS_VisualizerAudioProcessor::acceptsMidi() const { return false; }
bool VPS_VisualizerAudioProcessor::producesMidi() const { return false; }
bool VPS_VisualizerAudioProcessor::isMidiEffect() const { return false; }
double VPS_VisualizerAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int VPS_VisualizerAudioProcessor::getNumPrograms() { return 1; }
int VPS_VisualizerAudioProcessor::getCurrentProgram() { return 0; }
void VPS_VisualizerAudioProcessor::setCurrentProgram(int) {}
const juce::String VPS_VisualizerAudioProcessor::getProgramName(int) { return {}; }
void VPS_VisualizerAudioProcessor::changeProgramName(int, const juce::String&) {}

void VPS_VisualizerAudioProcessor::prepareToPlay(double, int) {}
void VPS_VisualizerAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VPS_VisualizerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}
#endif

void VPS_VisualizerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    if (isBypassed() || buffer.getNumChannels() < 2)
        return;

    const auto mode = getMonitorMode();
    if (mode == MonitorMode::Stereo)
        return;

    const int n = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    if (mode == MonitorMode::Mid) {
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (left[i] + right[i]);
            left[i] = m;
            right[i] = m;
        }
    }
    else if (mode == MonitorMode::Side) {
        for (int i = 0; i < n; ++i) {
            const float s = 0.5f * (left[i] - right[i]);
            left[i] = s;
            right[i] = -s;
        }
    }
    else if (mode == MonitorMode::Left) {
        // Isola apenas o lado esquerdo (muta o direito)
        juce::FloatVectorOperations::clear(right, n);
    }
    else if (mode == MonitorMode::Right) {
        // Isola apenas o lado direito (muta o esquerdo)
        juce::FloatVectorOperations::clear(left, n);
    }
}

bool VPS_VisualizerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* VPS_VisualizerAudioProcessor::createEditor() {
    return new VPS_VisualizerAudioProcessorEditor(*this);
}

void VPS_VisualizerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("VPSVisualizerState");
    state.setProperty("monitorMode", static_cast<int>(getMonitorMode()), nullptr);
    state.setProperty("bypassed", isBypassed(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void VPS_VisualizerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
            setMonitorMode(static_cast<MonitorMode>(static_cast<int>(state.getProperty("monitorMode", 0))));
            setBypassed(static_cast<bool>(state.getProperty("bypassed", false)));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VPS_VisualizerAudioProcessor();
}