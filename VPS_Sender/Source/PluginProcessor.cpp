#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <optional>

VPS_SenderAudioProcessor::VPS_SenderAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    slotIndex = hub.claimSlot(juce::Time::currentTimeMillis());
    if (slotIndex >= 0) {
        trackName = "Pista " + juce::String(slotIndex + 1);
        trackColorArgb = vps::SharedDomeHub::defaultColorForSlot(slotIndex);
        hub.setSlotName(slotIndex, trackName.toRawUTF8());
        if (auto* slot = hub.getSlot(slotIndex))
            slot->colorArgb.store(trackColorArgb, std::memory_order_relaxed);
    }
    else {
        trackName = "Sem slot";
    }
}

VPS_SenderAudioProcessor::~VPS_SenderAudioProcessor() {
    hub.releaseSlot(slotIndex);
}

void VPS_SenderAudioProcessor::setTrackName(const juce::String& newName) {
    trackName = newName.trim().isEmpty() ? ("Pista " + juce::String(slotIndex + 1)) : newName.substring(0, 30);
    hub.setSlotName(slotIndex, trackName.toRawUTF8());
}

void VPS_SenderAudioProcessor::setTrackColorArgb(uint32_t argb) {
    trackColorArgb = argb;
    if (auto* slot = hub.getSlot(slotIndex))
        slot->colorArgb.store(argb, std::memory_order_relaxed);
}

const juce::String VPS_SenderAudioProcessor::getName() const { return JucePlugin_Name; }
bool VPS_SenderAudioProcessor::acceptsMidi() const { return false; }
bool VPS_SenderAudioProcessor::producesMidi() const { return false; }
bool VPS_SenderAudioProcessor::isMidiEffect() const { return false; }
double VPS_SenderAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int VPS_SenderAudioProcessor::getNumPrograms() { return 1; }
int VPS_SenderAudioProcessor::getCurrentProgram() { return 0; }
void VPS_SenderAudioProcessor::setCurrentProgram(int) {}
const juce::String VPS_SenderAudioProcessor::getProgramName(int) { return {}; }
void VPS_SenderAudioProcessor::changeProgramName(int, const juce::String&) {}

void VPS_SenderAudioProcessor::prepareToPlay(double sampleRate, int) {
    audioPipeline.prepare(sampleRate);
}

void VPS_SenderAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VPS_SenderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}
#endif

void VPS_SenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    if (isBypassed())
        return;

    audioPipeline.processBlock(buffer, hub.getSlot(slotIndex), getUserGainDb(), getUserPan());
}

bool VPS_SenderAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* VPS_SenderAudioProcessor::createEditor() {
    return new VPS_SenderAudioProcessorEditor(*this);
}

void VPS_SenderAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("VPSSenderState");
    state.setProperty("trackName", trackName, nullptr);
    state.setProperty("trackColor", static_cast<int>(trackColorArgb), nullptr);
    state.setProperty("userGainDb", getUserGainDb(), nullptr);
    state.setProperty("userPan", getUserPan(), nullptr);
    state.setProperty("bypassed", isBypassed(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void VPS_SenderAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
            setTrackName(state.getProperty("trackName", trackName).toString());
            setTrackColorArgb(static_cast<uint32_t>(static_cast<int>(state.getProperty("trackColor", static_cast<int>(trackColorArgb)))));
            setUserGainDb(static_cast<float>(state.getProperty("userGainDb", 0.0f)));
            setUserPan(static_cast<float>(state.getProperty("userPan", 0.0f)));
            setBypassed(static_cast<bool>(state.getProperty("bypassed", false)));
        }
    }
}

namespace {
    template <typename T>
    auto extractHostTrackColor(const T& props, int) -> decltype(props.colourARGB, std::optional<uint32_t>{}) {
        if (props.colourARGB.has_value() && ((*props.colourARGB >> 24) & 0xFFu) != 0u)
            return static_cast<uint32_t>(*props.colourARGB);
        return std::nullopt;
    }

    template <typename T>
    auto extractHostTrackColor(const T& props, long) -> decltype(props.colour, std::optional<uint32_t>{}) {
        if (props.colour.has_value() && !props.colour->isTransparent())
            return static_cast<uint32_t>(props.colour->getARGB());
        return std::nullopt;
    }
}

void VPS_SenderAudioProcessor::updateTrackProperties(const TrackProperties& properties) {
    if (properties.name.has_value() && properties.name->isNotEmpty())
        setTrackName(*properties.name);

    if (const auto argb = extractHostTrackColor(properties, 0); argb.has_value())
        setTrackColorArgb(*argb);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VPS_SenderAudioProcessor();
}