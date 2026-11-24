#pragma once
#include <JuceHeader.h>
#include "Model/SharedDomeHub.h"
#include "Model/AudioPipeline.h"

class VPS_SenderAudioProcessor : public juce::AudioProcessor {
public:
    VPS_SenderAudioProcessor();
    ~VPS_SenderAudioProcessor() override;

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

    // Puxa nome e cor automaticamente da pista quando a DAW suporta (VST3 IInfoListener)
    void updateTrackProperties(const TrackProperties& properties) override;

    int getSlotIndex() const noexcept { return slotIndex; }
    vps::TrackSlot* getTrackSlot() const noexcept { return hub.getSlot(slotIndex); }

    juce::String getTrackName() const { return trackName; }
    void setTrackName(const juce::String& newName);

    uint32_t getTrackColorArgb() const noexcept { return trackColorArgb; }
    void setTrackColorArgb(uint32_t argb);
    void setUserGainDb(float db) noexcept { userGainDb.store(std::clamp(db, -60.0f, 12.0f), std::memory_order_relaxed); }
    float getUserGainDb() const noexcept { return userGainDb.load(std::memory_order_relaxed); }

    void setUserPan(float pan) noexcept { userPan.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed); }
    float getUserPan() const noexcept { return userPan.load(std::memory_order_relaxed); }

    void setBypassed(bool shouldBypass) noexcept { bypassed.store(shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> bypassed{ false };
    std::atomic<float> userGainDb{ 0.0f };
    std::atomic<float> userPan{ 0.0f };
    vps::SharedDomeHub hub;
    vps::AudioPipeline audioPipeline;
    int slotIndex = -1;

    juce::String trackName;
    uint32_t trackColorArgb = 0xFF38BDF8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VPS_SenderAudioProcessor)
};