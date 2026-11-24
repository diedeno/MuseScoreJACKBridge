
#pragma once

#include <JuceHeader.h>

#include <atomic>

#include "MidiQueue.h"
#include "JackClient.h"

class MuseScoreBridgeProcessor final
    : public juce::AudioProcessor
{
public:
    MuseScoreBridgeProcessor();

    ~MuseScoreBridgeProcessor() override = default;

    void prepareToPlay(
        double sampleRate,
        int samplesPerBlock
    ) override;

    void releaseResources() override;

    void processBlock(
        juce::AudioBuffer<float>&,
        juce::MidiBuffer&
    ) override;

    bool isBusesLayoutSupported(
        const BusesLayout& layouts
    ) const override;

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

    void changeProgramName(
        int index,
        const juce::String& newName
    ) override;

    void getStateInformation(
        juce::MemoryBlock& destination
    ) override;

    void setStateInformation(
        const void* data,
        int sizeInBytes
    ) override;

    bool connectToJack();

    bool isJackConnected() const;

    uint64_t getDroppedEvents() const;
    
    void setChannelName(const juce::String& name);
    juce::String getChannelName() const;
    
    bool applyChannelName();

private:
    juce::String channelName = "Channel";
    
    MidiQueue midiQueue;

    JackClient jackClient;

    std::atomic<uint64_t> droppedEvents{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        MuseScoreBridgeProcessor
    )
    // JACK Transport synchronisation
    int64_t lastMuseScorePosition = -1;
   
    
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

