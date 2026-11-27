
#include "PluginEditor.h"

MuseScoreBridgeEditor::MuseScoreBridgeEditor(
    MuseScoreBridgeProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    setSize(460, 410);

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

    midiChannelLabel.setText(
        "MIDI channel:",
        juce::dontSendNotification
    );

    midiChannelLabel.setJustificationType(
        juce::Justification::centredLeft
    );

    addAndMakeVisible(midiChannelLabel);

    // Item id 1 = pass through, id n+1 = force channel n.
    midiChannelBox.addItem("Pass through (as MuseScore sends)", 1);

    for (int channel = 1; channel <= 16; ++channel)
        midiChannelBox.addItem("Channel " + juce::String(channel), channel + 1);

    midiChannelBox.setSelectedId(
        processor.getOutputChannel() + 1,
        juce::dontSendNotification
    );

    midiChannelBox.onChange = [this]()
    {
        processor.setOutputChannel(midiChannelBox.getSelectedId() - 1);
    };

    addAndMakeVisible(midiChannelBox);

    channelsSeenLabel.setJustificationType(
        juce::Justification::centred
    );

    addAndMakeVisible(channelsSeenLabel);

    diagLabel.setJustificationType(
        juce::Justification::centred
    );

    addAndMakeVisible(diagLabel);

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

    {
        auto row = area.removeFromTop(28);

        midiChannelLabel.setBounds(row.removeFromLeft(110));
        midiChannelBox.setBounds(row);
    }

    area.removeFromTop(10);

    statusLabel.setBounds(
        area.removeFromTop(30)
    );

    infoLabel.setBounds(
        area.removeFromTop(30)
    );

    channelsSeenLabel.setBounds(
        area.removeFromTop(26)
    );

    diagLabel.setBounds(
        area.removeFromTop(26)
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

    // Keep the selector in step with a restored state.
    if (midiChannelBox.getSelectedId() != processor.getOutputChannel() + 1)
    {
        midiChannelBox.setSelectedId(
            processor.getOutputChannel() + 1,
            juce::dontSendNotification
        );
    }

    // Where do events get lost? Each number should keep growing while the
    // score plays: from MuseScore -> JACK cycles -> written to the port.
    diagLabel.setText(
        "From MuseScore: "
            + juce::String(static_cast<juce::int64>(processor.getEventsReceived()))
            + "  |  JACK cycles: "
            + juce::String(static_cast<juce::int64>(processor.getJackProcessCalls()))
            + "  |  Sent to JACK: "
            + juce::String(static_cast<juce::int64>(processor.getJackEventsWritten())),
        juce::dontSendNotification
    );

    // Which MIDI channels MuseScore has sent so far (before any rewrite).
    {
        const auto seen = processor.getChannelsSeenMask();

        juce::String list;

        for (int channel = 0; channel < 16; ++channel)
        {
            if ((seen & (1u << channel)) != 0)
            {
                if (list.isNotEmpty())
                    list << ", ";

                list << (channel + 1);
            }
        }

        channelsSeenLabel.setText(
            "Channels seen from MuseScore: "
                + (list.isEmpty() ? juce::String("none yet") : list),
            juce::dontSendNotification
        );
    }

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

