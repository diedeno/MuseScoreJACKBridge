
#include "PluginEditor.h"

MuseScoreBridgeEditor::MuseScoreBridgeEditor(
    MuseScoreBridgeProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    setSize(400, 300);

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

    // Commit on Return, Tab/click-away and window close, not just Return.
    channelNameEditor.onReturnKey = [this]() { commitName(); };
    channelNameEditor.onFocusLost = [this]() { commitName(); };

    addAndMakeVisible(channelNameEditor);

    transportToggle.setToggleState(
        processor.isTransportMaster(),
        juce::dontSendNotification
    );

    transportToggle.onClick = [this]()
    {
        processor.setTransportMaster(transportToggle.getToggleState());
    };

    addAndMakeVisible(transportToggle);

    reconnectButton.onClick = [this]()
    {
        processor.reconnectToJack();
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

void MuseScoreBridgeEditor::commitName()
{
    const auto text = channelNameEditor.getText().trim();

    if (text.isEmpty() || text == processor.getChannelName())
        return;

    processor.setChannelName(text);
    timerCallback();
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

    transportToggle.setBounds(
        area.removeFromTop(26)
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
    // Another instance may have taken over the transport.
    transportToggle.setToggleState(
        processor.isTransportMaster(),
        juce::dontSendNotification
    );

    if (processor.isJackConnected())
    {
        // Show the name JACK really registered; it can differ from the
        // requested one (e.g. a "-01" suffix when the name is taken).
        statusLabel.setText(
            "JACK: " + processor.getActualJackName(),
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
                    + processor.getJackWriteFailures()
                )
            )
            + "   |   Seeks detected: "
            + juce::String(processor.getSeekCount()),
        juce::dontSendNotification
    );
}

