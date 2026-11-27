
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

    ~MuseScoreBridgeProcessor() override;

    void prepareToPlay(
        double sampleRate,
        int samplesPerBlock
    ) override;

    void releaseResources() override;

    // Keep the double-precision overload visible (silences -Woverloaded-virtual).
    using juce::AudioProcessor::processBlock;

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

    // ---- Message-thread API (used by the editor) ----

    // Ask the background thread to make the registered JACK name match the
    // wanted one. Returns immediately; no-op if it already matches.
    void connectToJack();

    // Force close and reopen (the "Reconnect" button). Also asynchronous.
    void reconnectToJack();

    bool isJackConnected() const;

    uint64_t getDroppedEvents() const;

    // MIDI channel rewrite: 0 = pass through as sent by the host,
    // 1..16 = force every channel message onto that channel.
    void setOutputChannel(int channel);
    int getOutputChannel() const { return outputChannel.load(); }

    // Bit n set = channel n+1 has been seen coming from the host
    // (before any rewrite).
    uint16_t getChannelsSeenMask() const { return channelsSeen.load(); }

    // Number of seeks/jumps detected in the host position (diagnostics).
    uint32_t getSeekCount() const { return seekCount.load(); }
    uint64_t getJackWriteFailures() const;

    // Diagnostics for the window: events queued from the host, JACK process
    // cycles, and events written into the JACK port.
    uint64_t getEventsReceived() const { return eventsReceived.load(); }
    uint64_t getJackProcessCalls() const { return jackClient.getProcessCalls(); }
    uint64_t getJackEventsWritten() const { return jackClient.getEventsWritten(); }

    // Stores the name, tells the host the state changed, and reconnects if
    // the name differs from the one currently registered.
    void setChannelName(const juce::String& name);
    juce::String getChannelName() const;

    // The name JACK really assigned (may carry a "-01" suffix).
    juce::String getActualJackName() const;

    // Only one instance in the process can control the JACK transport.
    // Enabling it here takes it over from whichever instance had it.
    void setTransportMaster(bool shouldControlTransport);
    bool isTransportMaster() const;

private:
    class ConnectionWorker;

    juce::String jackClientName() const;
    void requestConnectionCheck(bool bypassSettleDelay = false);

    // Guards channelName, which is touched by the editor and the host.
    mutable juce::CriticalSection nameLock;
    juce::String channelName = "Channel";

    MidiQueue midiQueue;

    JackClient jackClient;

    std::atomic<uint64_t> droppedEvents{0};
    std::atomic<uint32_t> seekCount{0};
    std::atomic<uint64_t> eventsReceived{0};
    std::atomic<int> outputChannel{0};
    std::atomic<uint16_t> channelsSeen{0};
    std::atomic<double> hostSampleRate{0.0};

    // Connection management (see ConnectionWorker in the .cpp).
    std::atomic<uint32_t> lastChangeMs{0};
    std::atomic<bool> connectWanted{false};
    std::atomic<bool> forceReconnect{false};
    std::unique_ptr<ConnectionWorker> worker;

    // Audio-thread state for transport following.
    int64_t lastHostSamples = -1;
    bool lastHostPlaying = false;
    uint32_t lastEpoch = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        MuseScoreBridgeProcessor
    )
};
