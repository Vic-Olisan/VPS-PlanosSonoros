#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "Model/SharedDomeHub.h"

class VPS_VisualizerAudioProcessor : public juce::AudioProcessor {
public:
    enum class MonitorMode {
        Stereo = 0,
        Mid = 1,
        Side = 2,
        Left = 3,
        Right = 4
    };

    VPS_VisualizerAudioProcessor();
    ~VPS_VisualizerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    const vps::SharedDomeHub& getHub() const noexcept { return hub; }

    void setMonitorMode(MonitorMode mode) noexcept { monitorMode.store(static_cast<int>(mode), std::memory_order_relaxed); }
    MonitorMode getMonitorMode() const noexcept { return static_cast<MonitorMode>(monitorMode.load(std::memory_order_relaxed)); }

    void setBypassed(bool shouldBypass) noexcept { bypassed.store(shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed.load(std::memory_order_relaxed); }

private:
    vps::SharedDomeHub hub;
    std::atomic<int>  monitorMode{ static_cast<int>(MonitorMode::Stereo) };
    std::atomic<bool> bypassed{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VPS_VisualizerAudioProcessor)
};