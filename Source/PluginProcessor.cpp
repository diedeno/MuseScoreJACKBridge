#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // The instance that currently controls the JACK transport. MuseScore
    // loads every instance in one process, so this is shared by all of them.
    std::atomic<const void*> gTransportOwner{nullptr};
}

// --------------------------------------------------
// Connection worker
// --------------------------------------------------
//
// All JACK (re)connections happen on this one thread, so they never depend
// on the host running a message loop, and never run on the audio thread.
// It keeps the registered JACK name equal to the wanted one:
//
//  * a name change (editor, restored state) reconnects under the new name;
//  * the first connection waits kSettleMs, so a saved name that the host
//    delivers just after prepareToPlay() is used from the start and the
//    default name never shows up in the patchbay;
//  * if the JACK server goes away or is not running yet, it retries every
//    kRetryMs.

class MuseScoreBridgeProcessor::ConnectionWorker final : public juce::Thread
{
public:
    explicit ConnectionWorker(MuseScoreBridgeProcessor& p)
        : juce::Thread("JACK connection"), owner(p)
    {
    }

    void run() override
    {
        std::string lastTried;
        uint32_t lastAttempt = 0;

        while (!threadShouldExit())
        {
            wait(250);   // woken early by notify()

            if (threadShouldExit())
                break;

            if (!owner.connectWanted.load())
                continue;

            const auto now = juce::Time::getMillisecondCounter();

            if (now - owner.lastChangeMs.load() < kSettleMs)
                continue;

            const auto desired = owner.jackClientName().toStdString();

            if (owner.forceReconnect.exchange(false))
            {
                owner.jackClient.reconnect(desired);
                lastTried = desired;
                lastAttempt = now;
                continue;
            }

            if (owner.jackClient.isConnected()
                && owner.jackClient.getRequestedName() == desired)
            {
                continue;
            }

            // Not connected (or wrong name). Do not hammer a missing server,
            // but a changed name is applied immediately.
            if (desired == lastTried && now - lastAttempt < kRetryMs)
                continue;

            lastTried = desired;
            lastAttempt = now;

            const bool ok = owner.jackClient.connect(desired);

            juce::Logger::writeToLog(
                juce::String("JACK connect as '") + desired.c_str() + "': "
                + (ok ? "registered as '" + owner.getActualJackName() + "'"
                      : juce::String("failed")));
        }
    }

private:
    static constexpr uint32_t kSettleMs = 500;
    static constexpr uint32_t kRetryMs = 2000;

    MuseScoreBridgeProcessor& owner;
};

MuseScoreBridgeProcessor::MuseScoreBridgeProcessor()
    : AudioProcessor(
        BusesProperties()
            .withOutput(
                "Output",
                juce::AudioChannelSet::stereo(),
                true
            )
    ),
    jackClient(midiQueue)
{
    worker = std::make_unique<ConnectionWorker>(*this);
    worker->startThread();
}

MuseScoreBridgeProcessor::~MuseScoreBridgeProcessor()
{
    // Give up the transport if this instance held it.
    const void* self = this;
    gTransportOwner.compare_exchange_strong(self, nullptr);

    // Stop the worker before jackClient is destroyed.
    worker->stopThread(3000);
}

void MuseScoreBridgeProcessor::requestConnectionCheck(bool bypassSettleDelay)
{
    const auto now = juce::Time::getMillisecondCounter();

    // Pretend the last change was long ago to skip the settle delay.
    lastChangeMs.store(bypassSettleDelay ? now - 1000 : now);
    connectWanted.store(true);

    if (worker != nullptr)
        worker->notify();
}

// --------------------------------------------------
// Naming
// --------------------------------------------------

juce::String MuseScoreBridgeProcessor::jackClientName() const
{
    const juce::ScopedLock lock(nameLock);

    return "MuseScore-" + channelName;
}

void MuseScoreBridgeProcessor::setChannelName(const juce::String& name)
{
    const auto trimmed = name.trim();

    {
        const juce::ScopedLock lock(nameLock);

        if (trimmed.isNotEmpty())
            channelName = trimmed;
    }

    updateHostDisplay(
        juce::AudioProcessor::ChangeDetails()
            .withNonParameterStateChanged(true)
    );

    // The worker thread reconnects if the registered name differs.
    requestConnectionCheck();
}

juce::String MuseScoreBridgeProcessor::getChannelName() const
{
    const juce::ScopedLock lock(nameLock);

    return channelName;
}

juce::String MuseScoreBridgeProcessor::getActualJackName() const
{
    return juce::String::fromUTF8(jackClient.getActualName().c_str());
}

void MuseScoreBridgeProcessor::setTransportMaster(bool shouldControlTransport)
{
    if (shouldControlTransport)
    {
        // Last one to ask wins; the previous owner simply stops following.
        gTransportOwner.store(this);
    }
    else
    {
        const void* self = this;
        gTransportOwner.compare_exchange_strong(self, nullptr);
    }

    updateHostDisplay(
        juce::AudioProcessor::ChangeDetails()
            .withNonParameterStateChanged(true)
    );
}

bool MuseScoreBridgeProcessor::isTransportMaster() const
{
    return gTransportOwner.load() == this;
}

void MuseScoreBridgeProcessor::setOutputChannel(int channel)
{
    const int clamped = juce::jlimit(0, 16, channel);

    if (clamped == outputChannel.load())
        return;

    outputChannel.store(clamped);

    // Notes that are still sounding would otherwise get their note-off on
    // the new channel and hang on the old one.
    jackClient.requestAllNotesOff();

    updateHostDisplay(
        juce::AudioProcessor::ChangeDetails()
            .withNonParameterStateChanged(true)
    );
}

// --------------------------------------------------
// Lifecycle
// --------------------------------------------------

void MuseScoreBridgeProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    hostSampleRate.store(sampleRate);

    // Force a resync on the next block.
    lastHostSamples = -1;
    lastHostPlaying = false;

    // Connects after a short settle delay, so a saved name that the host
    // restores right after this call is the one that gets registered.
    requestConnectionCheck();

    juce::Logger::writeToLog(
        "MuseScore sample rate: " + juce::String(sampleRate)
    );
}

void MuseScoreBridgeProcessor::releaseResources()
{
    // Keep JACK connected while the plugin instance remains loaded.
    // The destructor closes the client.
}

// --------------------------------------------------
// Audio
// --------------------------------------------------

void MuseScoreBridgeProcessor::processBlock(
    juce::AudioBuffer<float>& audio,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // This is a MIDI-only instrument.
    audio.clear();

    const bool connected = jackClient.isConnected();

    // ---- Transport following -------------------------------------------
    //
    // Only the host-thread side runs here. Everything is handed to the JACK
    // thread through atomics (JackClient::requestTransport), so no JACK call
    // is made from the audio thread.

    // Not following (or not connected): forget the last position, so taking
    // over the transport later starts with a clean resync.
    if (!connected || !isTransportMaster())
    {
        lastHostSamples = -1;
        lastHostPlaying = false;
    }

    const auto epoch = jackClient.getConnectionEpoch();

    if (epoch != lastEpoch)
    {
        // JACK was (re)connected: forget what we sent before.
        lastEpoch = epoch;
        lastHostSamples = -1;
        lastHostPlaying = false;
    }

    if (auto* host = getPlayHead())
    {
        if (auto position = host->getPosition())
        {
            const bool playing = position->getIsPlaying();

            const auto samplesOpt = position->getTimeInSamples();

            // juce::Optional uses hasValue(), not std::optional's has_value().
            if (connected && isTransportMaster() && samplesOpt.hasValue())
            {
                const int64_t hostSamples = *samplesOpt;
                const int numSamples = audio.getNumSamples();

                // Convert to the JACK graph's sample rate if they differ.
                const double hostRate = hostSampleRate.load();
                const double jackRate =
                    static_cast<double>(jackClient.getSampleRate());

                int64_t jackFrame = hostSamples;

                if (hostRate > 0.0 && jackRate > 0.0
                    && std::abs(hostRate - jackRate) > 0.5)
                {
                    jackFrame = static_cast<int64_t>(
                        std::llround(
                            static_cast<double>(hostSamples)
                            * jackRate / hostRate
                        )
                    );
                }

                if (jackFrame < 0)
                    jackFrame = 0;

                const bool firstBlock = lastHostSamples < 0;
                const bool playingChanged = playing != lastHostPlaying;

                // While playing, the position advances by one block per
                // call. While stopped, it should stay put. Anything else is
                // a seek or a loop jump.
                const int64_t expected =
                    (playing && lastHostPlaying)
                        ? lastHostSamples + numSamples
                        : lastHostSamples;

                // The reported position is not guaranteed to advance by
                // exactly one block per call (hosts round, MuseScore keeps
                // its own clock). Treat only a clear discontinuity as a
                // seek: more than two blocks or 0.1 s, whichever is larger.
                // A too-small limit would flush all notes on every block
                // and make everything sound staccato.
                const int64_t seekLimit = std::max<int64_t>(
                    static_cast<int64_t>(numSamples) * 2,
                    static_cast<int64_t>(hostRate * 0.1)
                );

                const bool jumped =
                    !firstBlock
                    && std::llabs(hostSamples - expected) > seekLimit;

                if (jumped)
                    seekCount.fetch_add(1, std::memory_order_relaxed);

                // Always locate before starting so JACK starts in the right
                // place on a rising edge.
                const bool locate =
                    firstBlock
                    || jumped
                    || (playingChanged && playing);

                if (locate || playingChanged)
                {
                    jackClient.requestTransport(
                        locate,
                        static_cast<uint64_t>(jackFrame),
                        playingChanged || firstBlock,
                        playing
                    );
                }

                // A stop or a seek can leave notes hanging in the receiver.
                if ((playingChanged && !playing) || jumped)
                    jackClient.requestAllNotesOff();

                lastHostSamples = hostSamples;
                lastHostPlaying = playing;
            }
        }
    }

    // ---- MIDI forwarding -----------------------------------------------

    if (connected)
    {
        for (const auto metadata : midiMessages)
        {
            const auto& message = metadata.getMessage();

            const int size = message.getRawDataSize();

            // Initial version supports short MIDI messages.
            // SysEx will be added in a later version.
            if (size < 1 || size > 3)
                continue;

            MidiPacket packet;

            packet.size = static_cast<uint8_t>(size);
            packet.sampleOffset = metadata.samplePosition;

            std::copy_n(
                message.getRawData(),
                size,
                packet.data
            );

            // Channel voice messages (0x80..0xEF) carry a channel in the
            // low nibble. Record what the host sent, then optionally
            // force a fixed channel. System messages (0xF0..) are untouched.
            const uint8_t status = packet.data[0];

            if (status >= 0x80 && status < 0xF0)
            {
                channelsSeen.fetch_or(
                    static_cast<uint16_t>(1u << (status & 0x0F)),
                    std::memory_order_relaxed
                );

                const int forced =
                    outputChannel.load(std::memory_order_relaxed);

                if (forced > 0)
                {
                    packet.data[0] = static_cast<uint8_t>(
                        (status & 0xF0) | (forced - 1)
                    );
                }
            }

            if (!midiQueue.push(packet))
            {
                droppedEvents.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }
            else
            {
                eventsReceived.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }
        }
    }

    // Prevent the plugin from passing input MIDI
    // back to its host as MIDI output.
    midiMessages.clear();
}

bool MuseScoreBridgeProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto output =
        layouts.getMainOutputChannelSet();

    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

// --------------------------------------------------
// Boilerplate
// --------------------------------------------------

juce::AudioProcessorEditor*
MuseScoreBridgeProcessor::createEditor()
{
    return new MuseScoreBridgeEditor(*this);
}

bool MuseScoreBridgeProcessor::hasEditor() const
{
    return true;
}

const juce::String
MuseScoreBridgeProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MuseScoreBridgeProcessor::acceptsMidi() const
{
    return true;
}

bool MuseScoreBridgeProcessor::producesMidi() const
{
    return false;
}

bool MuseScoreBridgeProcessor::isMidiEffect() const
{
    return false;
}

double MuseScoreBridgeProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MuseScoreBridgeProcessor::getNumPrograms()
{
    return 1;
}

int MuseScoreBridgeProcessor::getCurrentProgram()
{
    return 0;
}

void MuseScoreBridgeProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String
MuseScoreBridgeProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void MuseScoreBridgeProcessor::changeProgramName(
    int index,
    const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

// --------------------------------------------------
// State
// --------------------------------------------------

void MuseScoreBridgeProcessor::getStateInformation(
    juce::MemoryBlock& destination)
{
    juce::XmlElement xml("MuseScoreJACKBridgeState");

    xml.setAttribute("channelName", getChannelName());
    xml.setAttribute("transportMaster", isTransportMaster() ? 1 : 0);
    xml.setAttribute("outputChannel", getOutputChannel());

    copyXmlToBinary(xml, destination);
}

void MuseScoreBridgeProcessor::setStateInformation(
    const void* data,
    int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(
        getXmlFromBinary(data, sizeInBytes)
    );

    if (xml == nullptr || !xml->hasTagName("MuseScoreJACKBridgeState"))
        return;

    {
        const juce::ScopedLock lock(nameLock);

        const auto restored =
            xml->getStringAttribute("channelName", "Channel").trim();

        channelName = restored.isEmpty() ? "Channel" : restored;
    }

    outputChannel.store(
        juce::jlimit(0, 16, xml->getIntAttribute("outputChannel", 0))
    );

    // Off by default. If several saved instances ask for the transport, the
    // first one wins; the others stay off.
    if (xml->getBoolAttribute("transportMaster", false))
    {
        const void* expected = nullptr;
        gTransportOwner.compare_exchange_strong(expected, this);
    }

    juce::Logger::writeToLog(
        "State restored, channel name: " + getChannelName()
    );

    // Whatever order the host calls things in, the worker thread makes the
    // registered name match. It does nothing if the name is unchanged, so
    // patchbay connections survive a restore that changes nothing.
    requestConnectionCheck();
}

// --------------------------------------------------
// JACK
// --------------------------------------------------

void MuseScoreBridgeProcessor::connectToJack()
{
    requestConnectionCheck();
}

void MuseScoreBridgeProcessor::reconnectToJack()
{
    forceReconnect.store(true);
    requestConnectionCheck(true);
}

bool MuseScoreBridgeProcessor::isJackConnected() const
{
    return jackClient.isConnected();
}

uint64_t MuseScoreBridgeProcessor::getDroppedEvents() const
{
    return droppedEvents.load(std::memory_order_relaxed);
}

uint64_t MuseScoreBridgeProcessor::getJackWriteFailures() const
{
    return jackClient.getWriteFailures();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MuseScoreBridgeProcessor();
}
