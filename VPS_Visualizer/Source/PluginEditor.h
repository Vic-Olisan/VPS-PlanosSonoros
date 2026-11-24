#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <functional>
#include "PluginProcessor.h"
#include "View/Dome3DView.h"

class VPSLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static constexpr const char* kFontName = "Sofia Pro";
    static constexpr const char* kLogoFont = "Open Sauce Sans";

    void setTheme(const vps::VPSTheme& t) noexcept { theme = t; }
    const vps::VPSTheme& getTheme() const noexcept { return theme; }

    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour&,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool isSegmented = (button.getRadioGroupId() == 101 || button.getRadioGroupId() == 102 || button.getRadioGroupId() == 103);
        const bool isStepper = (button.getButtonText() == "-" || button.getButtonText() == "+");
        const bool isToggle = button.getClickingTogglesState() && !isSegmented;

        if (isSegmented) {
            const bool active = button.getToggleState();
            if (active) {
                g.setColour(theme.accentCol);
                g.fillRoundedRectangle(bounds, 5.0f);
                g.setColour(theme.inkCol);
                g.drawRoundedRectangle(bounds, 5.0f, 1.1f);
            }
            else if (shouldDrawButtonAsHighlighted) {
                g.setColour(theme.hoverCol);
                g.fillRoundedRectangle(bounds, 5.0f);
            }
        }
        else if (isStepper) {
            g.setColour(shouldDrawButtonAsDown ? theme.accentCol
                : (shouldDrawButtonAsHighlighted ? theme.hoverCol : theme.cardBgCol));
            g.fillRoundedRectangle(bounds, 4.0f);
            g.setColour(theme.inkCol);
            g.drawRoundedRectangle(bounds, 4.0f, 1.1f);
        }
        else if (isToggle) {
            const bool active = button.getToggleState();
            g.setColour(active ? theme.accentCol : (shouldDrawButtonAsHighlighted ? theme.hoverCol : theme.cardBgCol));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(theme.inkCol);
            g.drawRoundedRectangle(bounds, 6.0f, 1.15f);
        }
        else {
            g.setColour(shouldDrawButtonAsDown ? theme.inkCol
                : (shouldDrawButtonAsHighlighted ? theme.hoverCol : theme.cardBgCol));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(theme.inkCol);
            g.drawRoundedRectangle(bounds, 6.0f, 1.15f);
        }
    }

    void drawButtonText(juce::Graphics& g,
        juce::TextButton& button,
        bool,
        bool shouldDrawButtonAsDown) override
    {
        const bool isSegmented = (button.getRadioGroupId() == 101 || button.getRadioGroupId() == 102 || button.getRadioGroupId() == 103);
        const bool isStepper = (button.getButtonText() == "-" || button.getButtonText() == "+");
        const bool isToggle = button.getClickingTogglesState() && !isSegmented;
        const bool active = button.getToggleState();
        const juce::Colour activeText = juce::Colours::white;

        if (button.getRadioGroupId() == 101) {
            g.setFont(juce::FontOptions(kFontName, 13.5f, active ? juce::Font::bold : juce::Font::plain));
            g.setColour(active ? activeText : theme.inkCol);
        }
        else if (button.getRadioGroupId() == 102 || button.getRadioGroupId() == 103) {
            g.setFont(juce::FontOptions(kFontName, 11.0f, active ? juce::Font::bold : juce::Font::plain));
            g.setColour(active ? activeText : theme.inkCol);
        }
        else if (isStepper) {
            g.setFont(juce::FontOptions(kFontName, 14.0f, juce::Font::bold));
            g.setColour(shouldDrawButtonAsDown ? activeText : theme.inkCol);
        }
        else if (isToggle) {
            g.setFont(juce::FontOptions(kFontName, 11.5f, active ? juce::Font::bold : juce::Font::plain));
            g.setColour(active ? activeText : theme.inkCol);
        }
        else {
            g.setFont(juce::FontOptions(kFontName, 12.0f, juce::Font::plain));
            g.setColour(shouldDrawButtonAsDown ? (theme.isDark ? theme.bgCol : activeText) : theme.inkCol);
        }

        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(3, 1), juce::Justification::centred, 1);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float, float,
        juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float)y + (float)height * 0.5f;
        const juce::Rectangle<float> track((float)x + 4.0f, cy - 2.5f, (float)width - 8.0f, 5.0f);
        g.setColour(theme.hoverCol);
        g.fillRoundedRectangle(track, 2.5f);

        const float fillW = std::max(0.0f, sliderPos - track.getX());
        g.setColour(theme.accentCol);
        g.fillRoundedRectangle(track.getX(), track.getY(), fillW, track.getHeight(), 2.5f);

        const juce::Rectangle<float> thumb(sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour(theme.cardBgCol);
        g.fillEllipse(thumb);
        g.setColour(theme.inkCol);
        g.drawEllipse(thumb, 1.25f);
    }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&,
        int x, int y, int width, int height,
        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
        bool isMouseOver, bool isMouseDown) override
    {
        const juce::Colour thumbCol = theme.inkCol
            .withAlpha(isMouseDown ? 0.65f : (isMouseOver ? 0.45f : 0.28f));

        g.setColour(thumbCol);
        if (isScrollbarVertical)
            g.fillRoundedRectangle((float)x + 2.0f, (float)thumbStartPosition, (float)width - 4.0f, (float)thumbSize, 3.0f);
        else
            g.fillRoundedRectangle((float)thumbStartPosition, (float)y + 2.0f, (float)thumbSize, (float)height - 4.0f, 3.0f);
    }

private:
    vps::VPSTheme theme = vps::getThemeById(0);
};

struct SpectrumViewState {
    float zoomMin = 0.0f;
    float zoomMax = 1.0f;
    bool  isHolding = false;
    juce::String lockedTip;
    std::array<int, vps::kMaxSpectrumBands> conflictSlotPerBand;
    int lastBandCount = 48;

    SpectrumViewState() { conflictSlotPerBand.fill(-1); }

    static float normToHz(float norm) noexcept {
        constexpr float fMin = 30.0f;
        constexpr float fMax = 18000.0f;
        return fMin * std::pow(fMax / fMin, std::clamp(norm, 0.0f, 1.0f));
    }

    static juce::String formatHz(float hz) {
        if (hz >= 1000.0f)
            return juce::String(hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k";
        return juce::String(juce::roundToInt(hz));
    }

    void applyWheelZoom(float relX, float deltaY) {
        const float span = zoomMax - zoomMin;
        const float factor = (deltaY > 0.0f) ? 0.78f : 1.28f;
        const float newSpan = std::clamp(span * factor, 0.08f, 1.0f);
        const float anchor = zoomMin + std::clamp(relX, 0.0f, 1.0f) * span;
        zoomMin = std::clamp(anchor - relX * newSpan, 0.0f, 1.0f - newSpan);
        zoomMax = zoomMin + newSpan;
    }

    void resetZoom() noexcept { zoomMin = 0.0f; zoomMax = 1.0f; }

    void lockConflictAt(const vps::SharedDomeHub& hub, float relX) {
        const float norm = zoomMin + std::clamp(relX, 0.0f, 1.0f) * (zoomMax - zoomMin);
        const int bands = std::clamp(lastBandCount, 16, vps::kMaxSpectrumBands);
        const int b = std::clamp(static_cast<int>(norm * (bands - 1) + 0.5f), 0, bands - 1);
        const float hz = normToHz(norm);

        int cSlot = conflictSlotPerBand[static_cast<size_t>(b)];
        if (cSlot < 0 && b > 0) cSlot = conflictSlotPerBand[static_cast<size_t>(b - 1)];
        if (cSlot < 0 && b + 1 < bands) cSlot = conflictSlotPerBand[static_cast<size_t>(b + 1)];

        isHolding = true;
        if (cSlot >= 0) {
            if (const auto* other = hub.getSlot(cSlot)) {
                lockedTip = formatHz(hz) + " Hz — Conflito: " + juce::String(other->name);
                return;
            }
        }
        lockedTip = formatHz(hz) + " Hz";
    }

    void draw(juce::Graphics& g, juce::Rectangle<int> area,
        const vps::SharedDomeHub& hub, const vps::Dome3DView& domeView,
        int slotIndex, int compareSlot, bool showExpandHint, const vps::VPSTheme& theme);
};

class ExpandedSpectrumPanel : public juce::Component {
public:
    ExpandedSpectrumPanel(const vps::SharedDomeHub& hubRef, const vps::Dome3DView& domeRef)
        : hub(hubRef), domeView(domeRef) {
    }

    void setSelectedSlot(int slot) noexcept { selectedSlot = slot; repaint(); }
    void setCompareSlot(int slot) noexcept { compareSlot = slot; repaint(); }
    int  getSelectedSlot() const noexcept { return selectedSlot; }
    void setTheme(const vps::VPSTheme& t) noexcept { theme = t; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void()> onCloseRequested;

private:
    const vps::SharedDomeHub& hub;
    const vps::Dome3DView& domeView;
    vps::VPSTheme theme = vps::getThemeById(0);
    int selectedSlot = -1;
    int compareSlot = -1;
    SpectrumViewState specState;
    juce::Rectangle<int> closeBtnBounds;
    juce::Rectangle<int> plotBounds;
};

class ChannelListComponent : public juce::Component {
public:
    ChannelListComponent(const vps::SharedDomeHub& hubRef, vps::Dome3DView& domeRef)
        : hub(hubRef), domeView(domeRef) {
    }

    void syncActiveOrder();
    int getTotalContentHeight() const noexcept;
    int getActiveCount() const noexcept { return static_cast<int>(displayOrder.size()); }
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void setTheme(const vps::VPSTheme& t) noexcept { theme = t; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void()> onLayoutChanged;
    std::function<void(int)> onSelectionChanged;
    std::function<void(int)> onCompareChanged;
    std::function<void()> onExpandToggleRequested;

    static constexpr int kRowHeight = 26;
    static constexpr int kInlineSpecH = 112;

private:
    int getRowIndexAtY(int y) const;

    const vps::SharedDomeHub& hub;
    vps::Dome3DView& domeView;
    vps::VPSTheme theme = vps::getThemeById(0);

    std::vector<int> displayOrder;
    int selectedSlot = -1;
    int compareSlot = -1;

    int draggingRowIdx = -1;
    int dragStartY = 0;
    bool isDraggingRow = false;

    juce::Rectangle<int> activeSpectrumPlot;
    juce::Rectangle<int> expandBtnBounds;
    SpectrumViewState inlineSpecState;
};

class SettingsOverlay : public juce::Component,
    private juce::ChangeListener {
public:
    SettingsOverlay();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setTheme(const vps::VPSTheme& t) noexcept { theme = t; repaint(); }
    void setGradientColors(uint32_t topArgb, uint32_t midArgb, uint32_t botArgb) noexcept {
        topColor = topArgb; midColor = midArgb; botColor = botArgb; repaint();
    }

    std::function<void(const vps::VisualSettings&)> onSettingsChanged;
    std::function<void(int)> onQualityChanged;
    std::function<void(int)> onThemeChanged;
    std::function<void(uint32_t, uint32_t, uint32_t)> onGradientChanged;

private:
    void notify();
    void openColorPickerFor(int target);
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    vps::VPSTheme theme = vps::getThemeById(0);
    juce::Slider brightnessSlider, grainSlider, scaleSlider;
    juce::ToggleButton peakHoldToggle{ "Peak Hold (travar canal lider)" };
    juce::ComboBox themeCombo;
    juce::TextButton qLowBtn{ "Baixa" };
    juce::TextButton qMidBtn{ "Media" };
    juce::TextButton qHighBtn{ "Alta" };

    juce::Rectangle<int> qCapsuleBounds;
    juce::Rectangle<int> topSwatchBounds, midSwatchBounds, botSwatchBounds;

    uint32_t topColor = 0xFFDF337A;
    uint32_t midColor = 0xFF8F40A8;
    uint32_t botColor = 0xFF0F5294;

    vps::VisualSettings settings;

    juce::Component::SafePointer<juce::DialogWindow> colorDialog;
    juce::ColourSelector* activePicker = nullptr;
    int activeColorTarget = 0;
};

class VPS_VisualizerAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer {
public:
    explicit VPS_VisualizerAudioProcessorEditor(VPS_VisualizerAudioProcessor&);
    ~VPS_VisualizerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTheme(int id, bool resetGradientToTheme);
    void updateModeSelection(vps::Dome3DView::ViewMode mode);
    void updateMonitorSelection(VPS_VisualizerAudioProcessor::MonitorMode mode);
    void changeLayers(int delta);
    void generatePaperTexture();
    void updateChannelListSize();

    VPS_VisualizerAudioProcessor& audioProcessor;
    VPSLookAndFeel lookAndFeel;
    vps::VPSTheme currentTheme = vps::getThemeById(0);
    int lastAppliedThemeId = 0;
    uint32_t lastTopCol = 0, lastMidCol = 0, lastBotCol = 0;

    vps::Dome3DView domeView;

    juce::Viewport channelViewport;
    ChannelListComponent channelList;
    ExpandedSpectrumPanel expandedSpectrum;
    SettingsOverlay settingsOverlay;

    juce::Image paperTexture;

    juce::TextButton modeFrontBtn{ "Frontal" };
    juce::TextButton mode3DBtn{ "3D" };
    juce::TextButton modeTopBtn{ "Superior" };

    juce::TextButton monStereoBtn{ "ST" };
    juce::TextButton monMidBtn{ "M" };
    juce::TextButton monSideBtn{ "S" };
    juce::TextButton monLeftBtn{ "L" };
    juce::TextButton monRightBtn{ "R" };

    juce::TextButton layersMinusBtn{ "-" };
    juce::TextButton layersPlusBtn{ "+" };
    juce::Label layersLabel;

    juce::TextButton bypassBtn{ "Bypass" };
    juce::TextButton configBtn{ "Config" };
    juce::TextButton resetCameraBtn{ "Reset" };

    juce::Rectangle<int> modeCapsuleBounds;
    juce::Rectangle<int> monitorCapsuleBounds;
    juce::Rectangle<int> canvasFrameBounds;
    juce::Rectangle<int> sideCardBounds;

    static constexpr int kOuterPad = 18;
    static constexpr int kHeaderHeight = 56;
    static constexpr int kSidePanelW = 244;

    int numLayers = 3;
    bool spectrumExpanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VPS_VisualizerAudioProcessorEditor)
};