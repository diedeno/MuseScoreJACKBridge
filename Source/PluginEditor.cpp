
#include "PluginEditor.h"

MuseScoreBridgeEditor::MuseScoreBridgeEditor(
    MuseScoreBridgeProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    setSize(400, 260);

    titleLabel.setText(
        "MuseScore JACK Bridge",
        juce::dontSendNotification
    );

    titleLabel.setFont(
        juce::FontOptions(22.0f, juce::Font::bold)
    );

    titleLabel.setJustificationType(
        juce::Justification::centred
    );

    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(
        juce::Justification::centred
    );

    addAndMakeVisible(statusLabel);

    infoLabel.setText(
        "MIDI events are forwarded to JACK2.",
        juce::dontSendNotification
    );

    infoLabel.setJustificationType(
        juce::Justification::centred
    );

    addAndMakeVisible(infoLabel);
    
    channelNameLabel.setText(
        "JACK channel name:",
        juce::dontSendNotification
    );

    channelNameLabel.setJustificationType(
        juce::Justification::centredLeft
    );

    addAndMakeVisible(channelNameLabel);

    channelNameEditor.setText(
        processor.getChannelName(),
        juce::dontSendNotification
    );

    channelNameEditor.setTextToShowWhenEmpty(
        "Enter channel name",
        juce::Colours::grey
    );

    channelNameEditor.onReturnKey = [this]()
    {
        processor.setChannelName(
            channelNameEditor.getText()
        );
        
        processor.updateHostDisplay(
            juce::AudioProcessor::ChangeDetails()
                .withNonParameterStateChanged(true)
        );

        processor.applyChannelName();

        timerCallback();
    };

    addAndMakeVisible(channelNameEditor);

    reconnectButton.onClick = [this]()
    {
        processor.connectToJack();
        timerCallback();
    };

    addAndMakeVisible(reconnectButton);

    startTimerHz(2);

    timerCallback();
}

MuseScoreBridgeEditor::~MuseScoreBridgeEditor()
{
    stopTimer();
}

void MuseScoreBridgeEditor::paint(
    juce::Graphics& graphics)
{
    graphics.fillAll(
        juce::Colour(0xff20242a)
    );

    graphics.setColour(
        juce::Colour(0xff414a55)
    );

    graphics.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(8.0f),
        10.0f,
        1.0f
    );
}

void MuseScoreBridgeEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    titleLabel.setBounds(
        area.removeFromTop(45)
    );

    area.removeFromTop(10);

    channelNameLabel.setBounds(
        area.removeFromTop(22)
    );

    channelNameEditor.setBounds(
        area.removeFromTop(30)
    );

    area.removeFromTop(10);

    statusLabel.setBounds(
        area.removeFromTop(30)
    );

    infoLabel.setBounds(
        area.removeFromTop(30)
    );

    area.removeFromTop(10);

    reconnectButton.setBounds(
        area.removeFromTop(36).withSizeKeepingCentre(
            170,
            36
        )
    );
}

void MuseScoreBridgeEditor::timerCallback()
{
    if (processor.isJackConnected())
    {
        statusLabel.setText(
            "JACK: Connected",
            juce::dontSendNotification
        );

        statusLabel.setColour(
            juce::Label::textColourId,
            juce::Colours::lightgreen
        );
    }
    else
    {
        statusLabel.setText(
            "JACK: Disconnected",
            juce::dontSendNotification
        );

        statusLabel.setColour(
            juce::Label::textColourId,
            juce::Colours::orange
        );
    }

    infoLabel.setText(
        "Dropped MIDI events: "
            + juce::String(
                static_cast<juce::int64>(
                    processor.getDroppedEvents()
                )
            ),
        juce::dontSendNotification
    );
}

