#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

// ==================== SpectrumViewState::draw ====================

void SpectrumViewState::draw(juce::Graphics& g, juce::Rectangle<int> area,
    const vps::SharedDomeHub& hub, const vps::Dome3DView& domeView,
    int slotIndex, int compareSlot, bool showExpandHint, const vps::VPSTheme& theme)
{
    const auto* sel = hub.getSlot(slotIndex);
    if (sel == nullptr) return;

    const auto box = area.toFloat();
    g.setColour(juce::Colour::fromRGB(20, 20, 24));
    g.fillRoundedRectangle(box, 7.0f);
    g.setColour(theme.inkCol);
    g.drawRoundedRectangle(box, 7.0f, 1.15f);

    const auto plotInt = area.reduced(8, 6).withTrimmedBottom(13);
    const auto plot = plotInt.toFloat();
    const int64_t nowMs = juce::Time::currentTimeMillis();

    const int bands = std::clamp(sel->spectrumCount.load(std::memory_order_relaxed), 16, vps::kMaxSpectrumBands);
    lastBandCount = bands;

    g.setColour(juce::Colours::white.withAlpha(0.10f));
    for (int k = 1; k < 4; ++k) {
        const float gx = plot.getX() + plot.getWidth() * (static_cast<float>(k) / 4.0f);
        g.drawVerticalLine(juce::roundToInt(gx), plot.getY(), plot.getBottom());
    }

    std::array<float, vps::kMaxSpectrumBands> myBands{};
    std::array<float, vps::kMaxSpectrumBands> otherMax{};
    conflictSlotPerBand.fill(-1);

    for (int b = 0; b < bands; ++b)
        myBands[static_cast<size_t>(b)] = sel->spectrum[b].load(std::memory_order_relaxed);

    for (int i = 0; i < vps::kMaxTracks; ++i) {
        if (i == slotIndex || domeView.isTrackHidden(i))
            continue;
        const auto* other = hub.getSlot(i);
        if (other == nullptr || other->active.load(std::memory_order_acquire) == 0)
            continue;
        if (nowMs - other->lastHeartbeatMs.load(std::memory_order_relaxed) > 600)
            continue;
        if (other->depthZ.load(std::memory_order_relaxed) < 0.03f)
            continue;

        const int otherBands = std::clamp(other->spectrumCount.load(std::memory_order_relaxed), 16, vps::kMaxSpectrumBands);
        for (int b = 0; b < bands; ++b) {
            const int ob = (otherBands == bands) ? b
                : std::clamp(static_cast<int>((static_cast<float>(b) / (bands - 1)) * (otherBands - 1)), 0, otherBands - 1);
            const float v = other->spectrum[ob].load(std::memory_order_relaxed);
            if (v > otherMax[static_cast<size_t>(b)]) {
                otherMax[static_cast<size_t>(b)] = v;
                conflictSlotPerBand[static_cast<size_t>(b)] = i;
            }
        }
    }

    const float span = std::max(0.05f, zoomMax - zoomMin);
    auto bandToX = [&](int b) -> float {
        const float norm = static_cast<float>(b) / static_cast<float>(bands - 1);
        return plot.getX() + ((norm - zoomMin) / span) * plot.getWidth();
        };

    g.saveState();
    g.reduceClipRegion(plotInt);

    const float bandW = (plot.getWidth() / static_cast<float>(bands)) / span;
    for (int b = 0; b < bands; ++b) {
        const float overlap = std::min(myBands[static_cast<size_t>(b)], otherMax[static_cast<size_t>(b)]);
        if (overlap > 0.24f) {
            const float intensity = std::clamp((overlap - 0.24f) / 0.50f, 0.0f, 1.0f);
            const float bx = bandToX(b);
            juce::ColourGradient redGlow(
                juce::Colour::fromRGB(225, 32, 32).withAlpha(0.75f * intensity), bx, plot.getBottom(),
                juce::Colour::fromRGB(225, 32, 32).withAlpha(0.04f * intensity), bx, plot.getY(),
                false);
            g.setGradientFill(redGlow);
            g.fillRect(bx - bandW * 0.5f, plot.getY(), bandW * 1.1f, plot.getHeight());
        }
        else {
            conflictSlotPerBand[static_cast<size_t>(b)] = -1;
        }
    }

    if (compareSlot >= 0 && compareSlot != slotIndex) {
        if (const auto* cmp = hub.getSlot(compareSlot)) {
            if (cmp->active.load(std::memory_order_acquire) != 0) {
                const int cmpBands = std::clamp(cmp->spectrumCount.load(std::memory_order_relaxed), 16, vps::kMaxSpectrumBands);
                juce::Path cmpCurve, cmpFill;
                for (int b = 0; b < bands; ++b) {
                    const int cb = (cmpBands == bands) ? b
                        : std::clamp(static_cast<int>((static_cast<float>(b) / (bands - 1)) * (cmpBands - 1)), 0, cmpBands - 1);
                    const float x = bandToX(b);
                    const float y = plot.getBottom() - cmp->spectrum[cb].load(std::memory_order_relaxed) * plot.getHeight();
                    if (b == 0) {
                        cmpCurve.startNewSubPath(x, y);
                        cmpFill.startNewSubPath(x, plot.getBottom());
                        cmpFill.lineTo(x, y);
                    }
                    else {
                        cmpCurve.lineTo(x, y);
                        cmpFill.lineTo(x, y);
                    }
                }
                cmpFill.lineTo(bandToX(bands - 1), plot.getBottom());
                cmpFill.closeSubPath();

                const juce::Colour cmpCol(cmp->colorArgb.load(std::memory_order_relaxed));
                g.setColour(cmpCol.withAlpha(0.24f));
                g.fillPath(cmpFill);
                g.setColour(cmpCol.withAlpha(0.85f));
                g.strokePath(cmpCurve, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    juce::Path mainCurve, mainFill;
    for (int b = 0; b < bands; ++b) {
        const float x = bandToX(b);
        const float y = plot.getBottom() - myBands[static_cast<size_t>(b)] * plot.getHeight();
        if (b == 0) {
            mainCurve.startNewSubPath(x, y);
            mainFill.startNewSubPath(x, plot.getBottom());
            mainFill.lineTo(x, y);
        }
        else {
            mainCurve.lineTo(x, y);
            mainFill.lineTo(x, y);
        }
    }
    mainFill.lineTo(bandToX(bands - 1), plot.getBottom());
    mainFill.closeSubPath();

    const juce::Colour trackCol(sel->colorArgb.load(std::memory_order_relaxed));
    g.setColour(trackCol.withAlpha(0.16f));
    g.fillPath(mainFill);
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.strokePath(mainCurve, juce::PathStrokeType(1.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.restoreState();

    g.setColour(juce::Colours::white.withAlpha(0.52f));
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 8.5f, juce::Font::plain));
    const int ly = juce::roundToInt(plot.getBottom()) + 1;
    g.drawText(formatHz(normToHz(zoomMin)), juce::roundToInt(plot.getX()), ly, 36, 11, juce::Justification::centredLeft);
    g.drawText(formatHz(normToHz(0.5f * (zoomMin + zoomMax))), juce::roundToInt(plot.getCentreX() - 20), ly, 40, 11, juce::Justification::centred);
    g.drawText(formatHz(normToHz(zoomMax)), juce::roundToInt(plot.getRight() - 36), ly, 36, 11, juce::Justification::centredRight);

    if (showExpandHint) {
        const juce::Rectangle<float> btn((float)area.getRight() - 22.0f, (float)area.getY() + 4.0f, 18.0f, 16.0f);
        g.setColour(juce::Colours::white.withAlpha(0.14f));
        g.fillRoundedRectangle(btn, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.82f));
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 11.0f, juce::Font::bold));
        g.drawText("+", btn.toNearestInt(), juce::Justification::centred);
    }

    if (isHolding && lockedTip.isNotEmpty()) {
        const auto tipBox = area.reduced(6, 4).removeFromTop(18).withTrimmedRight(showExpandHint ? 22 : 0).toFloat();
        g.setColour(theme.accentCol.withAlpha(0.94f));
        g.fillRoundedRectangle(tipBox, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 10.0f, juce::Font::bold));
        g.drawText(lockedTip, tipBox.toNearestInt(), juce::Justification::centred, true);
    }
}

// ==================== ExpandedSpectrumPanel ====================

void ExpandedSpectrumPanel::paint(juce::Graphics& g) {
    if (selectedSlot < 0) return;
    plotBounds = getLocalBounds().reduced(8, 6).withTrimmedBottom(13);
    specState.draw(g, getLocalBounds(), hub, domeView, selectedSlot, compareSlot, false, theme);

    if (const auto* slot = hub.getSlot(selectedSlot)) {
        juce::String title = juce::String(slot->name);
        if (compareSlot >= 0 && compareSlot != selectedSlot) {
            if (const auto* cmp = hub.getSlot(compareSlot))
                title += "  vs  " + juce::String(cmp->name);
        }
        title += "   (Scroll: Zoom | Duplo clique: Reset)";

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 11.0f, juce::Font::bold));
        g.drawText(title, 12, 4, getWidth() - 48, 16, juce::Justification::centredLeft);
    }

    closeBtnBounds = { getWidth() - 24, 4, 18, 16 };
    g.setColour(juce::Colours::white.withAlpha(0.14f));
    g.fillRoundedRectangle(closeBtnBounds.toFloat(), 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 10.5f, juce::Font::bold));
    g.drawText("X", closeBtnBounds, juce::Justification::centred);
}

void ExpandedSpectrumPanel::mouseDown(const juce::MouseEvent& e) {
    if (closeBtnBounds.contains(e.getPosition())) {
        if (onCloseRequested) onCloseRequested();
        return;
    }
    if (plotBounds.contains(e.getPosition())) {
        const float relX = static_cast<float>(e.x - plotBounds.getX()) / static_cast<float>(std::max(1, plotBounds.getWidth()));
        specState.lockConflictAt(hub, relX);
        repaint();
    }
}

void ExpandedSpectrumPanel::mouseUp(const juce::MouseEvent&) {
    if (specState.isHolding) {
        specState.isHolding = false;
        specState.lockedTip.clear();
        repaint();
    }
}

void ExpandedSpectrumPanel::mouseDoubleClick(const juce::MouseEvent&) {
    specState.resetZoom();
    repaint();
}

void ExpandedSpectrumPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!plotBounds.isEmpty()) {
        const float relX = static_cast<float>(e.x - plotBounds.getX()) / static_cast<float>(std::max(1, plotBounds.getWidth()));
        specState.applyWheelZoom(relX, wheel.deltaY);
        repaint();
    }
}

// ==================== ChannelListComponent ====================

void ChannelListComponent::syncActiveOrder() {
    const int64_t nowMs = juce::Time::currentTimeMillis();
    std::array<bool, vps::kMaxTracks> alive{};

    for (int i = 0; i < vps::kMaxTracks; ++i) {
        const auto* slot = hub.getSlot(i);
        if (slot != nullptr
            && slot->active.load(std::memory_order_acquire) != 0
            && (nowMs - slot->lastHeartbeatMs.load(std::memory_order_relaxed) <= 5000))
        {
            alive[static_cast<size_t>(i)] = true;
        }
    }

    displayOrder.erase(
        std::remove_if(displayOrder.begin(), displayOrder.end(),
            [&](int idx) { return !alive[static_cast<size_t>(idx)]; }),
        displayOrder.end());

    for (int i = 0; i < vps::kMaxTracks; ++i) {
        if (alive[static_cast<size_t>(i)]
            && std::find(displayOrder.begin(), displayOrder.end(), i) == displayOrder.end())
        {
            displayOrder.push_back(i);
        }
    }

    if (selectedSlot >= 0 && !alive[static_cast<size_t>(selectedSlot)]) {
        selectedSlot = -1;
        domeView.setSelectedTrack(-1);
        if (onSelectionChanged) onSelectionChanged(-1);
    }

    if (compareSlot >= 0 && !alive[static_cast<size_t>(compareSlot)]) {
        compareSlot = -1;
        if (onCompareChanged) onCompareChanged(-1);
    }
}

int ChannelListComponent::getTotalContentHeight() const noexcept {
    int h = static_cast<int>(displayOrder.size()) * kRowHeight;
    if (selectedSlot >= 0)
        h += kInlineSpecH + 6;
    return std::max(h, 80);
}

int ChannelListComponent::getRowIndexAtY(int y) const {
    int yCursor = 0;
    for (int r = 0; r < static_cast<int>(displayOrder.size()); ++r) {
        if (y >= yCursor && y < yCursor + kRowHeight)
            return r;
        yCursor += kRowHeight;
        if (displayOrder[static_cast<size_t>(r)] == selectedSlot)
            yCursor += kInlineSpecH + 6;
    }
    return -1;
}

void ChannelListComponent::paint(juce::Graphics& g) {
    const int64_t nowMs = juce::Time::currentTimeMillis();
    int yCursor = 0;
    activeSpectrumPlot = {};
    expandBtnBounds = {};

    for (int r = 0; r < static_cast<int>(displayOrder.size()); ++r) {
        const int i = displayOrder[static_cast<size_t>(r)];
        const auto* slot = hub.getSlot(i);
        if (slot == nullptr) continue;

        const juce::Rectangle<int> row(0, yCursor, getWidth(), kRowHeight);
        yCursor += kRowHeight;

        const bool isHidden = domeView.isTrackHidden(i);
        const bool isSelected = (i == selectedSlot);
        const bool isCompared = (i == compareSlot);
        const bool isDragged = (isDraggingRow && r == draggingRowIdx);
        const int64_t ageMs = nowMs - slot->lastHeartbeatMs.load(std::memory_order_relaxed);
        const float zNorm = slot->depthZ.load(std::memory_order_relaxed);
        const bool isAudible = (!isHidden && ageMs <= 600 && zNorm >= 0.03f);

        const juce::Colour col(slot->colorArgb.load(std::memory_order_relaxed));

        if (isDragged) {
            g.setColour(theme.accentCol.withAlpha(0.15f));
            g.fillRoundedRectangle(row.toFloat().reduced(0.0f, 2.0f), 4.0f);
        }
        else if (isSelected) {
            g.setColour(theme.inkCol.withAlpha(0.08f));
            g.fillRoundedRectangle(row.toFloat().reduced(0.0f, 2.0f), 4.0f);
        }
        else if (isCompared) {
            g.setColour(col.withAlpha(0.20f));
            g.fillRoundedRectangle(row.toFloat().reduced(0.0f, 2.0f), 4.0f);
        }

        const juce::Rectangle<float> dot(3.0f, (float)row.getY() + 7.0f, 11.0f, 11.0f);

        if (isHidden) {
            g.setColour(theme.mutedInkCol.withAlpha(0.55f));
            g.drawEllipse(dot, 1.1f);
            g.drawLine(dot.getX() + 1.5f, dot.getBottom() - 1.5f, dot.getRight() - 1.5f, dot.getY() + 1.5f, 1.1f);
        }
        else {
            g.setColour(isAudible ? col : col.withAlpha(0.28f));
            g.fillEllipse(dot);
            g.setColour(isSelected ? theme.accentCol : theme.inkCol.withAlpha(isAudible ? 0.85f : 0.30f));
            g.drawEllipse(dot, isSelected ? 1.5f : 1.0f);
        }

        const juce::String trackName(slot->name);
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 12.5f, (isSelected || isCompared) ? juce::Font::bold : juce::Font::plain));
        g.setColour(isHidden ? theme.mutedInkCol.withAlpha(0.45f) : (isAudible ? theme.inkCol : theme.mutedInkCol.withAlpha(0.75f)));
        g.drawText(trackName, 20, row.getY(), row.getWidth() - 74, 24, juce::Justification::centredLeft, true);

        const juce::String hzText = isAudible
            ? (juce::String(juce::roundToInt(slot->freqHz.load(std::memory_order_relaxed))) + " Hz")
            : "--";

        g.setColour(isAudible ? theme.mutedInkCol : theme.mutedInkCol.withAlpha(0.45f));
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 11.5f, juce::Font::plain));
        g.drawText(hzText, row.getRight() - 54, row.getY(), 52, 24, juce::Justification::centredRight);

        if (isSelected) {
            const juce::Rectangle<int> specArea(2, yCursor + 2, getWidth() - 4, kInlineSpecH);
            activeSpectrumPlot = specArea.reduced(8, 6).withTrimmedBottom(13);
            expandBtnBounds = { specArea.getRight() - 24, specArea.getY() + 2, 22, 20 };
            inlineSpecState.draw(g, specArea, hub, domeView, i, compareSlot, true, theme);
            yCursor += kInlineSpecH + 6;
        }
    }

    if (displayOrder.empty()) {
        g.setColour(theme.mutedInkCol);
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 12.0f, juce::Font::italic));
        g.drawFittedText("Insira o VPS Sender nos canais para visualizar os elementos aqui.",
            getLocalBounds().removeFromTop(70), juce::Justification::topLeft, 4);
    }
}

void ChannelListComponent::mouseDown(const juce::MouseEvent& e) {
    if (!expandBtnBounds.isEmpty() && expandBtnBounds.contains(e.getPosition())) {
        if (onExpandToggleRequested) onExpandToggleRequested();
        return;
    }

    if (!activeSpectrumPlot.isEmpty() && activeSpectrumPlot.contains(e.getPosition())) {
        const float relX = static_cast<float>(e.x - activeSpectrumPlot.getX())
            / static_cast<float>(std::max(1, activeSpectrumPlot.getWidth()));
        inlineSpecState.lockConflictAt(hub, relX);
        repaint();
        return;
    }

    draggingRowIdx = getRowIndexAtY(e.y);
    dragStartY = e.y;
    isDraggingRow = false;
}

void ChannelListComponent::mouseDrag(const juce::MouseEvent& e) {
    if (inlineSpecState.isHolding) return;

    if (draggingRowIdx < 0 || e.x <= 18) return;
    if (!isDraggingRow && std::abs(e.y - dragStartY) > 6)
        isDraggingRow = true;

    if (isDraggingRow) {
        const int targetRow = getRowIndexAtY(e.y);
        if (targetRow >= 0 && targetRow != draggingRowIdx && targetRow < static_cast<int>(displayOrder.size())) {
            const int movingSlot = displayOrder[static_cast<size_t>(draggingRowIdx)];
            displayOrder.erase(displayOrder.begin() + draggingRowIdx);
            displayOrder.insert(displayOrder.begin() + targetRow, movingSlot);
            draggingRowIdx = targetRow;
            repaint();
        }
    }
}

void ChannelListComponent::mouseUp(const juce::MouseEvent& e) {
    if (inlineSpecState.isHolding) {
        inlineSpecState.isHolding = false;
        inlineSpecState.lockedTip.clear();
        repaint();
        return;
    }

    if (isDraggingRow) {
        isDraggingRow = false;
        draggingRowIdx = -1;
        repaint();
        return;
    }

    const int rowIdx = getRowIndexAtY(e.y);
    draggingRowIdx = -1;
    if (rowIdx < 0 || rowIdx >= static_cast<int>(displayOrder.size()))
        return;

    const int slotIdx = displayOrder[static_cast<size_t>(rowIdx)];
    if (e.x <= 18) {
        domeView.setTrackHidden(slotIdx, !domeView.isTrackHidden(slotIdx));
    }
    else if (e.mods.isCtrlDown() && selectedSlot >= 0 && slotIdx != selectedSlot) {
        compareSlot = (compareSlot == slotIdx) ? -1 : slotIdx;
        if (onCompareChanged) onCompareChanged(compareSlot);
    }
    else {
        selectedSlot = (selectedSlot == slotIdx) ? -1 : slotIdx;
        if (selectedSlot == compareSlot || selectedSlot < 0) {
            compareSlot = -1;
            if (onCompareChanged) onCompareChanged(-1);
        }
        domeView.setSelectedTrack(selectedSlot);
        if (onSelectionChanged) onSelectionChanged(selectedSlot);
        if (onLayoutChanged) onLayoutChanged();
    }
    repaint();
}

void ChannelListComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!activeSpectrumPlot.isEmpty() && activeSpectrumPlot.contains(e.getPosition())) {
        inlineSpecState.resetZoom();
        repaint();
    }
}

void ChannelListComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!activeSpectrumPlot.isEmpty() && activeSpectrumPlot.contains(e.getPosition())) {
        const float relX = static_cast<float>(e.x - activeSpectrumPlot.getX())
            / static_cast<float>(std::max(1, activeSpectrumPlot.getWidth()));
        inlineSpecState.applyWheelZoom(relX, wheel.deltaY);
        repaint();
        return;
    }
    juce::Component::mouseWheelMove(e, wheel);
}

// ==================== SettingsOverlay ====================

SettingsOverlay::SettingsOverlay() {
    auto setupSlider = [this](juce::Slider& s, double minV, double maxV, double val) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV, 0.01);
        s.setValue(val, juce::dontSendNotification);
        s.onValueChange = [this] { notify(); };
        addAndMakeVisible(s);
        };

    setupSlider(brightnessSlider, 0.25, 1.00, settings.bgBrightness);
    setupSlider(grainSlider, 0.00, 0.28, settings.grainAmount);
    setupSlider(scaleSlider, 0.65, 1.40, settings.sphereScaleMult);

    peakHoldToggle.setToggleState(settings.peakHold, juce::dontSendNotification);
    peakHoldToggle.setColour(juce::ToggleButton::textColourId, theme.inkCol);
    peakHoldToggle.setColour(juce::ToggleButton::tickColourId, theme.accentCol);
    peakHoldToggle.setColour(juce::ToggleButton::tickDisabledColourId, theme.inkCol);
    peakHoldToggle.onClick = [this] { notify(); };
    addAndMakeVisible(peakHoldToggle);

    themeCombo.addItem("Padrao", 1);
    themeCombo.addItem("Escuro", 2);
    themeCombo.addItem("Cinza", 3);
    themeCombo.addItem("Claro", 4);
    themeCombo.addItem("Warm", 5);
    themeCombo.addItem("Cold", 6);
    themeCombo.setSelectedId(1, juce::dontSendNotification);
    themeCombo.onChange = [this] {
        const int id = std::clamp(themeCombo.getSelectedId() - 1, 0, 5);
        if (onThemeChanged) onThemeChanged(id);
        };
    addAndMakeVisible(themeCombo);

    auto setupQBtn = [this](juce::TextButton& b, int q) {
        b.setClickingTogglesState(true);
        b.setRadioGroupId(103);
        b.onClick = [this, q] { if (onQualityChanged) onQualityChanged(q); };
        addAndMakeVisible(b);
        };
    setupQBtn(qLowBtn, 0);
    setupQBtn(qMidBtn, 1);
    setupQBtn(qHighBtn, 2);
    qMidBtn.setToggleState(true, juce::dontSendNotification);
}

void SettingsOverlay::notify() {
    settings.bgBrightness = static_cast<float>(brightnessSlider.getValue());
    settings.grainAmount = static_cast<float>(grainSlider.getValue());
    settings.sphereScaleMult = static_cast<float>(scaleSlider.getValue());
    settings.peakHold = peakHoldToggle.getToggleState();
    if (onSettingsChanged) onSettingsChanged(settings);
}

void SettingsOverlay::openColorPickerFor(int target) {
    activeColorTarget = target;
    const uint32_t initialArgb = (target == 0) ? topColor : ((target == 1) ? midColor : botColor);

    if (colorDialog != nullptr) {
        colorDialog->exitModalState(0);
        colorDialog.deleteAndZero();
    }

    auto picker = std::make_unique<juce::ColourSelector>(
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders);
    picker->setSize(280, 320);
    picker->setCurrentColour(juce::Colour(initialArgb));
    picker->addChangeListener(this);
    activePicker = picker.get();

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(picker.release());
    opts.dialogTitle = (target == 0) ? "Cor do Topo" : ((target == 1) ? "Cor do Meio" : "Cor da Base");
    opts.componentToCentreAround = this;
    opts.dialogBackgroundColour = theme.cardBgCol;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;

    colorDialog = opts.launchAsync();
}

void SettingsOverlay::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (activePicker != nullptr && source == activePicker) {
        const uint32_t c = activePicker->getCurrentColour().withAlpha(1.0f).getARGB();
        if (activeColorTarget == 0) topColor = c;
        else if (activeColorTarget == 1) midColor = c;
        else botColor = c;
        repaint();
        if (onGradientChanged)
            onGradientChanged(topColor, midColor, botColor);
    }
}

void SettingsOverlay::mouseDown(const juce::MouseEvent& e) {
    if (topSwatchBounds.contains(e.getPosition())) { openColorPickerFor(0); return; }
    if (midSwatchBounds.contains(e.getPosition())) { openColorPickerFor(1); return; }
    if (botSwatchBounds.contains(e.getPosition())) { openColorPickerFor(2); return; }
}

void SettingsOverlay::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(theme.cardBgCol);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(theme.inkCol);
    g.drawRoundedRectangle(b, 10.0f, 1.4f);

    g.setColour(theme.inkCol);
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 13.5f, juce::Font::bold));
    g.drawText("Configuracoes", 14, 10, getWidth() - 28, 20, juce::Justification::centredLeft);

    g.setColour(theme.mutedInkCol);
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 11.0f, juce::Font::bold));
    g.drawText("Tema", 14, 34, getWidth() - 28, 14, juce::Justification::centredLeft);
    g.drawText("Degrade (Topo/Meio/Base)", 14, 80, getWidth() - 28, 14, juce::Justification::centredLeft);
    g.drawText("Brilho do fundo", 14, 122, getWidth() - 28, 14, juce::Justification::centredLeft);
    g.drawText("Granulado", 14, 158, getWidth() - 28, 14, juce::Justification::centredLeft);
    g.drawText("Tamanho das esferas", 14, 194, getWidth() - 28, 14, juce::Justification::centredLeft);
    g.drawText("Qualidade do espectro", 14, 256, getWidth() - 28, 14, juce::Justification::centredLeft);

    auto drawSwatch = [&](juce::Rectangle<int> r, uint32_t argb, const juce::String& lbl) {
        const auto rf = r.toFloat();
        g.setColour(juce::Colour(argb));
        g.fillRoundedRectangle(rf, 5.0f);
        g.setColour(theme.inkCol);
        g.drawRoundedRectangle(rf, 5.0f, 1.15f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 9.5f, juce::Font::bold));
        g.drawText(lbl, r, juce::Justification::centred);
        };

    drawSwatch(topSwatchBounds, topColor, "Topo");
    drawSwatch(midSwatchBounds, midColor, "Meio");
    drawSwatch(botSwatchBounds, botColor, "Base");

    g.setColour(theme.bgCol);
    g.fillRoundedRectangle(qCapsuleBounds.toFloat(), 6.0f);
    g.setColour(theme.inkCol);
    g.drawRoundedRectangle(qCapsuleBounds.toFloat(), 6.0f, 1.1f);
}

void SettingsOverlay::resized() {
    const int w = getWidth() - 28;
    themeCombo.setBounds(14, 50, w, 24);

    const int swGap = 8;
    const int swW = (w - swGap * 2) / 3;
    topSwatchBounds = { 14,                   96, swW, 20 };
    midSwatchBounds = { 14 + swW + swGap,     96, swW, 20 };
    botSwatchBounds = { 14 + (swW + swGap) * 2, 96, w - (swW + swGap) * 2, 20 };

    brightnessSlider.setBounds(14, 136, w, 18);
    grainSlider.setBounds(14, 172, w, 18);
    scaleSlider.setBounds(14, 208, w, 18);
    peakHoldToggle.setBounds(14, 230, w, 22);

    qCapsuleBounds = { 14, 272, w, 26 };
    const int bw = (w - 8) / 3;
    qLowBtn.setBounds(17, 275, bw, 20);
    qMidBtn.setBounds(17 + bw + 1, 275, bw, 20);
    qHighBtn.setBounds(17 + bw * 2 + 2, 275, w - bw * 2 - 8, 20);
}

// ==================== VPS_VisualizerAudioProcessorEditor ====================

VPS_VisualizerAudioProcessorEditor::VPS_VisualizerAudioProcessorEditor(VPS_VisualizerAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    domeView(p.getHub()),
    channelList(p.getHub(), domeView),
    expandedSpectrum(p.getHub(), domeView)
{
    setLookAndFeel(&lookAndFeel);

    lastAppliedThemeId = audioProcessor.getHub().getThemeId();
    currentTheme = vps::getThemeById(lastAppliedThemeId);
    audioProcessor.getHub().getGradientColors(lastTopCol, lastMidCol, lastBotCol);
    applyTheme(lastAppliedThemeId, false);
    domeView.setGradientColors(lastTopCol, lastMidCol, lastBotCol);
    settingsOverlay.setGradientColors(lastTopCol, lastMidCol, lastBotCol);

    addAndMakeVisible(domeView);

    channelList.onLayoutChanged = [this] { updateChannelListSize(); };
    channelList.onSelectionChanged = [this](int slot) {
        expandedSpectrum.setSelectedSlot(slot);
        if (slot < 0 && spectrumExpanded) {
            spectrumExpanded = false;
            resized();
        }
        };
    channelList.onCompareChanged = [this](int slot) {
        expandedSpectrum.setCompareSlot(slot);
        };
    channelList.onExpandToggleRequested = [this] {
        spectrumExpanded = !spectrumExpanded && (channelList.getSelectedSlot() >= 0);
        expandedSpectrum.setSelectedSlot(channelList.getSelectedSlot());
        resized();
        };
    channelViewport.setViewedComponent(&channelList, false);
    channelViewport.setScrollBarsShown(true, false, true, false);
    channelViewport.setScrollBarThickness(8);
    addAndMakeVisible(channelViewport);

    expandedSpectrum.onCloseRequested = [this] {
        spectrumExpanded = false;
        resized();
        };
    addChildComponent(expandedSpectrum);

    auto setupModeBtn = [this](juce::TextButton& btn, vps::Dome3DView::ViewMode mode) {
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(101);
        btn.onClick = [this, mode] { updateModeSelection(mode); };
        addAndMakeVisible(btn);
        };
    setupModeBtn(modeFrontBtn, vps::Dome3DView::ViewMode::Front);
    setupModeBtn(mode3DBtn, vps::Dome3DView::ViewMode::Dome3D);
    setupModeBtn(modeTopBtn, vps::Dome3DView::ViewMode::Top);
    modeFrontBtn.setToggleState(true, juce::dontSendNotification);

    auto setupMonBtn = [this](juce::TextButton& btn, VPS_VisualizerAudioProcessor::MonitorMode mode) {
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(102);
        btn.onClick = [this, mode] { updateMonitorSelection(mode); };
        addAndMakeVisible(btn);
        };
    setupMonBtn(monStereoBtn, VPS_VisualizerAudioProcessor::MonitorMode::Stereo);
    setupMonBtn(monMidBtn, VPS_VisualizerAudioProcessor::MonitorMode::Mid);
    setupMonBtn(monSideBtn, VPS_VisualizerAudioProcessor::MonitorMode::Side);
    setupMonBtn(monLeftBtn, VPS_VisualizerAudioProcessor::MonitorMode::Left);
    setupMonBtn(monRightBtn, VPS_VisualizerAudioProcessor::MonitorMode::Right);
    monStereoBtn.setToggleState(true, juce::dontSendNotification);

    layersMinusBtn.onClick = [this] { changeLayers(-1); };
    layersPlusBtn.onClick = [this] { changeLayers(+1); };
    addAndMakeVisible(layersMinusBtn);
    addAndMakeVisible(layersPlusBtn);

    layersLabel.setText("Camadas: 3", juce::dontSendNotification);
    layersLabel.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 12.5f, juce::Font::plain));
    layersLabel.setColour(juce::Label::textColourId, currentTheme.inkCol);
    layersLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(layersLabel);

    bypassBtn.setClickingTogglesState(true);
    bypassBtn.setToggleState(audioProcessor.isBypassed(), juce::dontSendNotification);
    bypassBtn.onClick = [this] {
        const bool b = bypassBtn.getToggleState();
        audioProcessor.setBypassed(b);
        domeView.setBypassed(b);
        };
    addAndMakeVisible(bypassBtn);

    configBtn.setClickingTogglesState(true);
    configBtn.onClick = [this] {
        settingsOverlay.setVisible(configBtn.getToggleState());
        settingsOverlay.toFront(true);
        };
    addAndMakeVisible(configBtn);

    resetCameraBtn.onClick = [this] { domeView.resetCamera(); };
    addAndMakeVisible(resetCameraBtn);

    settingsOverlay.onSettingsChanged = [this](const vps::VisualSettings& s) {
        domeView.setVisualSettings(s);
        };
    settingsOverlay.onQualityChanged = [this](int q) {
        audioProcessor.getHub().setSpectrumQuality(q);
        };
    settingsOverlay.onThemeChanged = [this](int id) {
        audioProcessor.getHub().setThemeId(id);
        applyTheme(id, true);
        };
    settingsOverlay.onGradientChanged = [this](uint32_t tCol, uint32_t mCol, uint32_t bCol) {
        lastTopCol = tCol; lastMidCol = mCol; lastBotCol = bCol;
        audioProcessor.getHub().setGradientColors(tCol, mCol, bCol);
        domeView.setGradientColors(tCol, mCol, bCol);
        };
    addChildComponent(settingsOverlay);

    setResizable(true, true);
    setResizeLimits(880, 460, 1920, 1200);
    setSize(1040, 600);
    startTimerHz(15);
}

VPS_VisualizerAudioProcessorEditor::~VPS_VisualizerAudioProcessorEditor() {
    channelViewport.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
}

void VPS_VisualizerAudioProcessorEditor::applyTheme(int id, bool resetGradientToTheme) {
    lastAppliedThemeId = id;
    currentTheme = vps::getThemeById(id);
    lookAndFeel.setTheme(currentTheme);
    channelList.setTheme(currentTheme);
    expandedSpectrum.setTheme(currentTheme);
    settingsOverlay.setTheme(currentTheme);
    domeView.setChromeColors(currentTheme);
    layersLabel.setColour(juce::Label::textColourId, currentTheme.inkCol);

    if (resetGradientToTheme) {
        lastTopCol = currentTheme.defaultTopArgb;
        lastMidCol = currentTheme.defaultMidArgb;
        lastBotCol = currentTheme.defaultBotArgb;
        audioProcessor.getHub().setGradientColors(lastTopCol, lastMidCol, lastBotCol);
        domeView.setGradientColors(lastTopCol, lastMidCol, lastBotCol);
        settingsOverlay.setGradientColors(lastTopCol, lastMidCol, lastBotCol);
    }

    generatePaperTexture();
    repaint();
}

void VPS_VisualizerAudioProcessorEditor::generatePaperTexture() {
    constexpr int sz = 128;
    paperTexture = juce::Image(juce::Image::ARGB, sz, sz, true);
    juce::Random rng(42);

    const int baseR = currentTheme.bgCol.getRed();
    const int baseG = currentTheme.bgCol.getGreen();
    const int baseB = currentTheme.bgCol.getBlue();

    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            const int delta = rng.nextInt(11) - 5;
            paperTexture.setPixelAt(x, y, juce::Colour::fromRGB(
                static_cast<uint8_t>(std::clamp(baseR + delta, 0, 255)),
                static_cast<uint8_t>(std::clamp(baseG + delta, 0, 255)),
                static_cast<uint8_t>(std::clamp(baseB + delta, 0, 255))));
        }
    }
}

void VPS_VisualizerAudioProcessorEditor::changeLayers(int delta) {
    numLayers = std::clamp(numLayers + delta, 2, 8);
    domeView.setNumLayers(numLayers);
    layersLabel.setText("Camadas: " + juce::String(numLayers), juce::dontSendNotification);
}

void VPS_VisualizerAudioProcessorEditor::updateModeSelection(vps::Dome3DView::ViewMode mode) {
    domeView.setViewMode(mode);
}

void VPS_VisualizerAudioProcessorEditor::updateMonitorSelection(VPS_VisualizerAudioProcessor::MonitorMode mode) {
    audioProcessor.setMonitorMode(mode);
    domeView.setMonitorMode(static_cast<int>(mode));
}

void VPS_VisualizerAudioProcessorEditor::updateChannelListSize() {
    const int listW = std::max(10, channelViewport.getWidth() - 10);
    const int listH = std::max(channelViewport.getHeight(), channelList.getTotalContentHeight());
    channelList.setSize(listW, listH);
}

void VPS_VisualizerAudioProcessorEditor::paint(juce::Graphics& g) {
    if (paperTexture.isValid()) {
        g.setTiledImageFill(paperTexture, 0, 0, 1.0f);
        g.fillAll();
    }
    else {
        g.fillAll(currentTheme.bgCol);
    }

    g.setColour(currentTheme.inkCol);
    g.setFont(juce::FontOptions()
        .withName(VPSLookAndFeel::kLogoFont)
        .withStyle("Black")
        .withHeight(36.0f));
    g.drawText("VPS", kOuterPad, 6, 95, 42, juce::Justification::centredLeft);

    g.setColour(currentTheme.cardBgCol);
    g.fillRoundedRectangle(modeCapsuleBounds.toFloat(), 8.0f);
    g.setColour(currentTheme.inkCol);
    g.drawRoundedRectangle(modeCapsuleBounds.toFloat(), 8.0f, 1.25f);

    g.setColour(currentTheme.cardBgCol);
    g.fillRoundedRectangle(monitorCapsuleBounds.toFloat(), 8.0f);
    g.setColour(currentTheme.inkCol);
    g.drawRoundedRectangle(monitorCapsuleBounds.toFloat(), 8.0f, 1.25f);

    const auto cardFloat = sideCardBounds.toFloat();
    g.setColour(currentTheme.cardBgCol);
    g.fillRoundedRectangle(cardFloat, 10.0f);
    g.setColour(currentTheme.inkCol);
    g.drawRoundedRectangle(cardFloat, 10.0f, 1.25f);

    auto headerArea = sideCardBounds.reduced(14, 12).removeFromTop(24);
    g.setColour(currentTheme.inkCol);
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 15.0f, juce::Font::bold));
    g.drawText("Canais", headerArea.withTrimmedRight(64), juce::Justification::centredLeft);

    g.setColour(currentTheme.mutedInkCol);
    g.setFont(juce::FontOptions(VPSLookAndFeel::kFontName, 11.5f, juce::Font::plain));
    g.drawText(juce::String(channelList.getActiveCount()) + " ativos", headerArea, juce::Justification::centredRight);

    g.setColour(currentTheme.inkCol.withAlpha(0.25f));
    g.drawHorizontalLine(headerArea.getBottom() + 4, (float)headerArea.getX(), (float)headerArea.getRight());
}

void VPS_VisualizerAudioProcessorEditor::resized() {
    const int w = getWidth();
    const int h = getHeight();

    const int contentTop = kHeaderHeight + 6;
    const int sideX = w - kOuterPad - kSidePanelW;
    const int canvasW = sideX - kOuterPad - 16;

    const int capX = kOuterPad + 92;
    const int capW = std::clamp(canvasW - 256, 220, 320);
    const int capH = 32;
    const int capY = 13;
    modeCapsuleBounds = { capX, capY, capW, capH };

    const int innerW = capW - 12;
    const int btnW = innerW / 3;
    modeFrontBtn.setBounds(capX + 4, capY + 4, btnW - 2, capH - 8);
    mode3DBtn.setBounds(capX + 4 + btnW + 2, capY + 4, btnW - 2, capH - 8);
    modeTopBtn.setBounds(capX + 4 + btnW * 2 + 4, capY + 4, innerW - btnW * 2 - 2, capH - 8);

    const int monW = 166;
    const int monX = capX + capW + 12;
    monitorCapsuleBounds = { monX, capY, monW, capH };
    const int mBtnW = (monW - 12) / 5;
    monStereoBtn.setBounds(monX + 4 + mBtnW * 0, capY + 4, mBtnW, capH - 8);
    monMidBtn.setBounds(monX + 4 + mBtnW * 1, capY + 4, mBtnW, capH - 8);
    monSideBtn.setBounds(monX + 4 + mBtnW * 2, capY + 4, mBtnW, capH - 8);
    monLeftBtn.setBounds(monX + 4 + mBtnW * 3, capY + 4, mBtnW, capH - 8);
    monRightBtn.setBounds(monX + 4 + mBtnW * 4, capY + 4, mBtnW + 2, capH - 8);

    const int ctrlY = 16;
    const int resetW = 54;
    const int cfgW = 56;
    const int bypW = 56;

    int rx = w - kOuterPad;
    rx -= resetW;   resetCameraBtn.setBounds(rx, ctrlY, resetW, 26);
    rx -= 6 + cfgW; configBtn.setBounds(rx, ctrlY, cfgW, 26);
    rx -= 6 + bypW; bypassBtn.setBounds(rx, ctrlY, bypW, 26);

    rx -= 10 + 22; layersPlusBtn.setBounds(rx, ctrlY + 1, 22, 24);
    rx -= 76;      layersLabel.setBounds(rx, ctrlY + 1, 76, 24);
    rx -= 22;      layersMinusBtn.setBounds(rx, ctrlY + 1, 22, 24);

    const int totalCanvasH = h - contentTop - kOuterPad;
    if (spectrumExpanded && channelList.getSelectedSlot() >= 0) {
        const int expH = std::clamp(totalCanvasH / 3, 140, 210);
        const int domeH = totalCanvasH - expH - 10;
        canvasFrameBounds = { kOuterPad, contentTop, canvasW, domeH };
        domeView.setBounds(canvasFrameBounds);
        expandedSpectrum.setBounds(kOuterPad, contentTop + domeH + 10, canvasW, expH);
        expandedSpectrum.setVisible(true);
    }
    else {
        canvasFrameBounds = { kOuterPad, contentTop, canvasW, totalCanvasH };
        domeView.setBounds(canvasFrameBounds);
        expandedSpectrum.setVisible(false);
    }

    sideCardBounds = { sideX, contentTop, kSidePanelW, totalCanvasH };
    auto vpBounds = sideCardBounds.reduced(12, 12).withTrimmedTop(36);
    channelViewport.setBounds(vpBounds);
    updateChannelListSize();

    settingsOverlay.setBounds(w - kOuterPad - 246, contentTop + 4, 246, 312);
}

void VPS_VisualizerAudioProcessorEditor::timerCallback() {
    const int hubTheme = audioProcessor.getHub().getThemeId();
    uint32_t tCol, mCol, bCol;
    audioProcessor.getHub().getGradientColors(tCol, mCol, bCol);

    if (hubTheme != lastAppliedThemeId)
        applyTheme(hubTheme, false);

    if (tCol != lastTopCol || mCol != lastMidCol || bCol != lastBotCol) {
        lastTopCol = tCol; lastMidCol = mCol; lastBotCol = bCol;
        domeView.setGradientColors(tCol, mCol, bCol);
        settingsOverlay.setGradientColors(tCol, mCol, bCol);
    }

    const int prevCount = channelList.getActiveCount();
    channelList.syncActiveOrder();

    if (channelList.getActiveCount() != prevCount) {
        updateChannelListSize();
        repaint(sideCardBounds.withHeight(44));
    }

    channelList.repaint();
    if (expandedSpectrum.isVisible())
        expandedSpectrum.repaint();
}