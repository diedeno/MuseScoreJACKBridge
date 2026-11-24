
#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

class MuseScoreBridgeEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit MuseScoreBridgeEditor(
        MuseScoreBridgeProcessor& processor
    );

    ~MuseScoreBridgeEditor() override;

    void paint(juce::Graphics& graphics) override;

    void resized() override;

private:
    void timerCallback() override;

    MuseScoreBridgeProcessor& processor;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label infoLabel;
    juce::Label channelNameLabel;
    juce::TextEditor channelNameEditor;

    juce::TextButton reconnectButton{
        "Reconnect JACK"
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        MuseScoreBridgeEditor
    )
};

