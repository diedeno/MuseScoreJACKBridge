#include <algorithm>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
}

void MuseScoreBridgeProcessor::setChannelName(
    const juce::String& name)
{
    channelName = name.trim();
}

juce::String MuseScoreBridgeProcessor::getChannelName() const
{
    return channelName;
}

void MuseScoreBridgeProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    
    
    connectToJack();
    juce::Logger::writeToLog(
    "MuseScore sample rate: " + juce::String(sampleRate)
    + " | JACK sample rate: "
    + juce::String(jackClient.getSampleRate())
);
}

void MuseScoreBridgeProcessor::releaseResources()
{
    // Keep JACK connected while the plugin instance
    // remains loaded. The destructor closes the client.
}


    
void MuseScoreBridgeProcessor::processBlock(
    juce::AudioBuffer<float>& audio,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    static std::atomic<int> counter{0};

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (++counter % 100 == 0)
            {
                juce::String info = "MuseScore: ";

                info += position->getIsPlaying()
                    ? "PLAYING"
                    : "STOPPED";

                if (auto ppq = position->getPpqPosition())
                    info += " | PPQ: " + juce::String(*ppq, 3);

                if (auto bpm = position->getBpm())
                    info += " | BPM: " + juce::String(*bpm, 2);

                if (auto ts = position->getTimeSignature())
                {
                    info += " | Meter: "
                        + juce::String(ts->numerator)
                        + "/"
                        + juce::String(ts->denominator);
                }

                // New test: sample position
                if (auto samples = position->getTimeInSamples())
                    info += " | Samples: " + juce::String(*samples);

                juce::Logger::writeToLog(info);
            }
        }
    }

    // Existing MIDI forwarding code continues here...
    
    // --------------------------------------------------
// JACK Transport position synchronisation
// --------------------------------------------------

if (auto* playHead = getPlayHead())
{
    if (auto position = playHead->getPosition())
    {
        if (auto samples = position->getTimeInSamples())
        {
            const int64_t currentPosition = *samples;

            // First position received: initialise JACK.
            if (lastMuseScorePosition < 0)
            {
                jackClient.transportLocate(
                    static_cast<uint64_t>(currentPosition)
                );
            }
            // Detect a discontinuity (seek or loop).
            else
            {
                const int64_t expectedPosition =
                    lastMuseScorePosition
                    + audio.getNumSamples();

                if (std::abs(currentPosition - expectedPosition) > 2)
                {
                    jackClient.transportLocate(
                        static_cast<uint64_t>(currentPosition)
                    );
                }
            }

            lastMuseScorePosition = currentPosition;
        }
    }
}

    // This is a MIDI-only instrument.
    audio.clear();

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

        packet.sampleOffset =
            metadata.samplePosition;

        std::copy_n(
            message.getRawData(),
            size,
            packet.data
        );

        if (!midiQueue.push(packet))
        {
            droppedEvents.fetch_add(
                1,
                std::memory_order_relaxed
            );
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

void MuseScoreBridgeProcessor::getStateInformation(
    juce::MemoryBlock& destination)
{
    juce::XmlElement xml("MuseScoreJACKBridgeState");

    xml.setAttribute("channelName", channelName);

    copyXmlToBinary(xml, destination);
}

void MuseScoreBridgeProcessor::setStateInformation(
    const void* data,
    int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(
        getXmlFromBinary(data, sizeInBytes)
    );

    if (xml && xml->hasTagName("MuseScoreJACKBridgeState"))
    {
        setChannelName(
            xml->getStringAttribute(
                "channelName",
                "Channel"
            )
        );
    }
    
    if (jackClient.isConnected())
    applyChannelName();
}

bool MuseScoreBridgeProcessor::connectToJack()
{
    const auto jackName =
        "MuseScore-" + channelName;
    
    juce::Logger::writeToLog(
        "Connecting JACK client as: " + jackName
    );    

    return jackClient.start(
        jackName.toRawUTF8()
    );
}

bool MuseScoreBridgeProcessor::applyChannelName()
{
    const auto jackName =
        "MuseScore-" + channelName;

    return jackClient.restart(
        jackName.toRawUTF8()
    );
}

bool MuseScoreBridgeProcessor::isJackConnected() const
{
    return jackClient.isConnected();
}

uint64_t MuseScoreBridgeProcessor::getDroppedEvents() const
{
    return droppedEvents.load(std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MuseScoreBridgeProcessor();
}

