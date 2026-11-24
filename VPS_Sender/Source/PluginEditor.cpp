#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

VPS_SenderAudioProcessorEditor::VPS_SenderAudioProcessorEditor(VPS_SenderAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);
    generatePaperTexture();

    const juce::Colour inkCol = juce::Colour::fromRGB(22, 22, 22);
    const juce::Colour cardBg = juce::Colour::fromRGB(249, 248, 244);
    const juce::Colour accentRed = juce::Colour::fromRGB(217, 74, 67);

    bypassBtn.setClickingTogglesState(true);
    bypassBtn.setToggleState(audioProcessor.isBypassed(), juce::dontSendNotification);
    bypassBtn.onClick = [this] {
        audioProcessor.setBypassed(bypassBtn.getToggleState());
        repaint();
        };
    addAndMakeVisible(bypassBtn);

    nameEditor.setText(audioProcessor.getTrackName(), juce::dontSendNotification);
    nameEditor.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 13.5f, juce::Font::plain));
    nameEditor.setJustification(juce::Justification::centredLeft);
    nameEditor.setIndents(10, 0);
    nameEditor.setColour(juce::TextEditor::backgroundColourId, cardBg);
    nameEditor.setColour(juce::TextEditor::textColourId, inkCol);
    nameEditor.setColour(juce::TextEditor::outlineColourId, inkCol);
    nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, accentRed);
    nameEditor.setColour(juce::TextEditor::highlightColourId, accentRed.withAlpha(0.22f));
    nameEditor.setColour(juce::TextEditor::highlightedTextColourId, inkCol);
    nameEditor.setColour(juce::CaretComponent::caretColourId, inkCol);
    nameEditor.applyColourToAllText(inkCol, true);
    nameEditor.onTextChange = [this] {
        audioProcessor.setTrackName(nameEditor.getText());
        };
    addAndMakeVisible(nameEditor);

    for (size_t i = 0; i < palette.size(); ++i) {
        auto& btn = colorButtons[i];
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(palette[i]));
        btn.onClick = [this, i] {
            audioProcessor.setTrackColorArgb(palette[i]);
            syncColorSelection();
            repaint();
            };
        addAndMakeVisible(btn);
    }

    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setRange(-60.0, 12.0, 0.1);
    gainSlider.setSkewFactorFromMidPoint(-6.0);
    gainSlider.setDoubleClickReturnValue(true, 0.0);
    gainSlider.setValue(audioProcessor.getUserGainDb(), juce::dontSendNotification);
    gainSlider.onValueChange = [this] {
        audioProcessor.setUserGainDb(static_cast<float>(gainSlider.getValue()));
        repaint();
        };
    addAndMakeVisible(gainSlider);

    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue(true, 0.0);
    panSlider.setValue(audioProcessor.getUserPan(), juce::dontSendNotification);
    panSlider.onValueChange = [this] {
        audioProcessor.setUserPan(static_cast<float>(panSlider.getValue()));
        repaint();
        };
    addAndMakeVisible(panSlider);

    syncColorSelection();
    setSize(392, 404);
    startTimerHz(15);
}

VPS_SenderAudioProcessorEditor::~VPS_SenderAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void VPS_SenderAudioProcessorEditor::generatePaperTexture() {
    constexpr int sz = 128;
    paperTexture = juce::Image(juce::Image::ARGB, sz, sz, true);
    juce::Random rng(42);

    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            const int delta = rng.nextInt(11) - 5;
            const auto r = static_cast<uint8_t>(std::clamp(245 + delta, 232, 252));
            const auto g = static_cast<uint8_t>(std::clamp(244 + delta, 231, 251));
            const auto b = static_cast<uint8_t>(std::clamp(238 + delta, 224, 246));
            paperTexture.setPixelAt(x, y, juce::Colour::fromRGB(r, g, b));
        }
    }
}

void VPS_SenderAudioProcessorEditor::syncColorSelection() {
    const uint32_t activeCol = audioProcessor.getTrackColorArgb();
    for (size_t i = 0; i < palette.size(); ++i)
        colorButtons[i].setToggleState(palette[i] == activeCol, juce::dontSendNotification);
}

void VPS_SenderAudioProcessorEditor::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    const juce::Colour inkCol = juce::Colour::fromRGB(22, 22, 22);
    const juce::Colour mutedInk = juce::Colour::fromRGB(110, 108, 102);
    const juce::Colour cardBg = juce::Colour::fromRGB(249, 248, 244);
    const juce::Colour accentRed = juce::Colour::fromRGB(217, 74, 67);
    const juce::Colour activeTrackCol(audioProcessor.getTrackColorArgb());

    if (paperTexture.isValid()) {
        g.setTiledImageFill(paperTexture, 0, 0, 1.0f);
        g.fillAll();
    }
    else {
        g.fillAll(juce::Colour::fromRGB(245, 244, 238));
    }

    // Logo VPS em Open Sauce Bold
    g.setColour(inkCol);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kLogoFont, 26.0f, juce::Font::bold));
    g.drawText("VPS", kOuterPad, 12, 68, 30, juce::Justification::centredLeft);

    g.setColour(mutedInk);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 13.0f, juce::Font::plain));
    g.drawText("Sender", kOuterPad + 64, 18, 60, 22, juce::Justification::centredLeft);

    // Capsula de status do Slot na direita
    const int slotIdx = audioProcessor.getSlotIndex();
    const juce::Rectangle<float> statusPill((float)bounds.getWidth() - kOuterPad - 88.0f, 15.0f, 88.0f, 24.0f);
    g.setColour(cardBg);
    g.fillRoundedRectangle(statusPill, 12.0f);
    g.setColour(inkCol);
    g.drawRoundedRectangle(statusPill, 12.0f, 1.15f);

    const juce::Colour ledCol = (slotIdx < 0 || audioProcessor.isBypassed())
        ? accentRed
        : (isReceivingAudio ? activeTrackCol : mutedInk.withAlpha(0.35f));

    const juce::Rectangle<float> ledDot(statusPill.getX() + 8.0f, statusPill.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour(ledCol);
    g.fillEllipse(ledDot);
    g.setColour(inkCol.withAlpha(0.85f));
    g.drawEllipse(ledDot, 0.9f);

    g.setColour(inkCol);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 10.5f, juce::Font::bold));
    const juce::String slotText = audioProcessor.isBypassed() ? "BYPASS"
        : ((slotIdx >= 0) ? ("SLOT #" + juce::String(slotIdx + 1)) : "OFFLINE");
    g.drawText(slotText, statusPill.withTrimmedLeft(20.0f).toNearestInt(), juce::Justification::centredLeft);

    g.setColour(inkCol.withAlpha(0.18f));
    g.drawHorizontalLine(kHeaderHeight, (float)kOuterPad, (float)(bounds.getWidth() - kOuterPad));

    // Secoes Nome e Cor
    g.setColour(mutedInk);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 11.0f, juce::Font::bold));
    g.drawText("NOME DO CANAL", kOuterPad, kHeaderHeight + 10, 160, 16, juce::Justification::centredLeft);
    g.drawText("COR DA ESFERA", kOuterPad, kHeaderHeight + 68, 160, 16, juce::Justification::centredLeft);

    const juce::Rectangle<float> previewDot((float)bounds.getWidth() - kOuterPad - 11.0f, (float)kHeaderHeight + 71.0f, 10.0f, 10.0f);
    g.setColour(activeTrackCol);
    g.fillEllipse(previewDot);
    g.setColour(inkCol.withAlpha(0.85f));
    g.drawEllipse(previewDot, 1.0f);

    // Controles de Mix (Ganho + Pan)
    const int mixY = kHeaderHeight + 162;
    const int contentW = bounds.getWidth() - kOuterPad * 2;
    const float gDb = audioProcessor.getUserGainDb();
    const float pVal = audioProcessor.getUserPan();

    g.setColour(mutedInk);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 11.0f, juce::Font::bold));
    g.drawText("GANHO", kOuterPad, mixY, 100, 16, juce::Justification::centredLeft);

    g.setColour(inkCol);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 11.5f, juce::Font::bold));
    const juce::String gStr = (gDb <= -59.5f) ? "-inf dB" : ((gDb > 0.05f ? "+" : "") + juce::String(gDb, 1) + " dB");
    g.drawText(gStr, kOuterPad, mixY, contentW, 16, juce::Justification::centredRight);

    g.setColour(mutedInk);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 11.0f, juce::Font::bold));
    g.drawText("PAN", kOuterPad, mixY + 46, 100, 16, juce::Justification::centredLeft);

    g.setColour(inkCol);
    g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 11.5f, juce::Font::bold));
    const int pPct = juce::roundToInt(std::abs(pVal) * 100.0f);
    const juce::String pStr = (pPct <= 2) ? "C" : ((pVal < 0.0f ? "L " : "R ") + juce::String(pPct) + "%");
    g.drawText(pStr, kOuterPad, mixY + 46, contentW, 16, juce::Justification::centredRight);

    // Card inferior de telemetria
    const juce::Rectangle<float> card((float)kOuterPad, (float)mixY + 98.0f, (float)contentW, 74.0f);
    g.setColour(cardBg);
    g.fillRoundedRectangle(card, 9.0f);
    g.setColour(inkCol);
    g.drawRoundedRectangle(card, 9.0f, 1.2f);

    const float colW = card.getWidth() / 3.0f;
    auto drawMetric = [&](int colIndex, const juce::String& label, const juce::String& value) {
        const int x = juce::roundToInt(card.getX() + colIndex * colW);
        const int y = juce::roundToInt(card.getY()) + 9;
        const int w = juce::roundToInt(colW);

        g.setColour(mutedInk);
        g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 10.0f, juce::Font::bold));
        g.drawText(label, x, y, w, 14, juce::Justification::centred);

        g.setColour(isReceivingAudio ? inkCol : mutedInk.withAlpha(0.55f));
        g.setFont(juce::FontOptions(VPSSenderLookAndFeel::kFontName, 13.5f, juce::Font::bold));
        g.drawText(value, x, y + 16, w, 18, juce::Justification::centred);
        };

    juce::String panStr = "C";
    if (isReceivingAudio) {
        const int panPct = juce::roundToInt(std::abs(currentPan) * 100.0f);
        if (panPct > 3)
            panStr = (currentPan < 0.0f ? "L " : "R ") + juce::String(panPct) + "%";
    }
    else {
        panStr = "--";
    }

    const juce::String freqStr = isReceivingAudio ? (juce::String(juce::roundToInt(currentFreq)) + " Hz") : "--";
    const juce::String dbStr = isReceivingAudio ? (juce::String(currentDb, 1) + " dB") : "-inf";

    drawMetric(0, "PAN", panStr);
    drawMetric(1, "FREQUENCIA", freqStr);
    drawMetric(2, "NIVEL", dbStr);

    const juce::Rectangle<float> meterBg(card.getX() + 14.0f, card.getBottom() - 14.0f, card.getWidth() - 28.0f, 6.0f);
    g.setColour(juce::Colour::fromRGB(233, 231, 222));
    g.fillRoundedRectangle(meterBg, 3.0f);

    if (isReceivingAudio && currentDepth > 0.01f) {
        const float fillW = meterBg.getWidth() * std::clamp(currentDepth, 0.0f, 1.0f);
        g.setColour(activeTrackCol);
        g.fillRoundedRectangle(meterBg.withWidth(fillW), 3.0f);
    }

    g.setColour(inkCol.withAlpha(0.75f));
    g.drawRoundedRectangle(meterBg, 3.0f, 0.9f);
}

void VPS_SenderAudioProcessorEditor::resized() {
    const int contentW = getWidth() - kOuterPad * 2;

    bypassBtn.setBounds(getWidth() - kOuterPad - 88 - 8 - 58, 15, 58, 24);
    nameEditor.setBounds(kOuterPad, kHeaderHeight + 28, contentW, 30);

    // 2 fileiras de 8 cores
    const int cols = 8;
    const int btnSize = 26;
    const int spacing = (contentW - cols * btnSize) / (cols - 1);
    const int yColors = kHeaderHeight + 88;

    for (int i = 0; i < static_cast<int>(colorButtons.size()); ++i) {
        const int r = i / cols;
        const int c = i % cols;
        colorButtons[static_cast<size_t>(i)].setBounds(
            kOuterPad + c * (btnSize + spacing),
            yColors + r * (btnSize + 8),
            btnSize, btnSize);
    }

    const int mixY = kHeaderHeight + 162;
    gainSlider.setBounds(kOuterPad, mixY + 18, contentW, 22);
    panSlider.setBounds(kOuterPad, mixY + 64, contentW, 22);
}

void VPS_SenderAudioProcessorEditor::timerCallback() {
    if (!nameEditor.hasKeyboardFocus(true)) {
        const auto currentName = audioProcessor.getTrackName();
        if (nameEditor.getText() != currentName) {
            nameEditor.setText(currentName, juce::dontSendNotification);
            nameEditor.applyColourToAllText(juce::Colour::fromRGB(22, 22, 22), true);
        }
    }
    syncColorSelection();

    if (audioProcessor.isBypassed()) {
        isReceivingAudio = false;
        currentDepth = 0.0f;
    }
    else if (const auto* slot = audioProcessor.getTrackSlot()) {
        const int64_t ageMs = juce::Time::currentTimeMillis() - slot->lastHeartbeatMs.load(std::memory_order_relaxed);
        const float depth = slot->depthZ.load(std::memory_order_relaxed);

        isReceivingAudio = (ageMs <= 600 && depth >= 0.03f);
        currentPan = slot->panX.load(std::memory_order_relaxed);
        currentFreq = slot->freqHz.load(std::memory_order_relaxed);
        currentDb = slot->rmsDb.load(std::memory_order_relaxed);
        currentDepth = depth;
    }
    else {
        isReceivingAudio = false;
        currentDepth = 0.0f;
    }

    repaint();
}