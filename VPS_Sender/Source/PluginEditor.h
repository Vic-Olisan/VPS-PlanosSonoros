#pragma once
#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

class VPSSenderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static constexpr const char* kFontName = "Sofia Pro";
    static constexpr const char* kLogoFont = "Open Sauce";

    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour&,
        bool shouldDrawButtonAsHighlighted,
        bool) override
    {
        const juce::Colour inkCol = juce::Colour::fromRGB(22, 22, 22);
        const juce::Colour paperCol = juce::Colour::fromRGB(248, 247, 242);
        const juce::Colour hoverCol = juce::Colour::fromRGB(233, 231, 222);
        const juce::Colour accentRed = juce::Colour::fromRGB(217, 74, 67);

        // Botao Bypass (pilula)
        if (button.getButtonText().isNotEmpty()) {
            const auto b = button.getLocalBounds().toFloat().reduced(0.5f);
            const bool active = button.getToggleState();
            g.setColour(active ? accentRed : (shouldDrawButtonAsHighlighted ? hoverCol : paperCol));
            g.fillRoundedRectangle(b, 6.0f);
            g.setColour(inkCol);
            g.drawRoundedRectangle(b, 6.0f, 1.15f);
            return;
        }

        // Bolinhas da paleta de cores
        const auto bounds = button.getLocalBounds().toFloat().reduced(2.5f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto circle = bounds.withSizeKeepingCentre(diameter, diameter);

        const auto swatchCol = button.findColour(juce::TextButton::buttonColourId);
        const bool selected = button.getToggleState();

        if (selected) {
            g.setColour(inkCol);
            g.drawEllipse(circle.expanded(2.0f), 1.6f);
        }
        else if (shouldDrawButtonAsHighlighted) {
            g.setColour(inkCol.withAlpha(0.45f));
            g.drawEllipse(circle.expanded(1.5f), 1.1f);
        }

        const auto inner = circle.reduced(selected ? 1.5f : 0.5f);
        g.setColour(swatchCol);
        g.fillEllipse(inner);
        g.setColour(inkCol.withAlpha(0.82f));
        g.drawEllipse(inner, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override {
        if (button.getButtonText().isEmpty())
            return;

        const bool active = button.getToggleState();
        g.setFont(juce::FontOptions(kFontName, 11.0f, active ? juce::Font::bold : juce::Font::plain));
        g.setColour(active ? juce::Colours::white : juce::Colour::fromRGB(22, 22, 22));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 1), juce::Justification::centred, 1);
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& te) override {
        g.setColour(te.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, 7.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& te) override {
        const bool focused = te.hasKeyboardFocus(true);
        g.setColour(te.findColour(focused ? juce::TextEditor::focusedOutlineColourId
            : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0.75f, 0.75f, (float)width - 1.5f, (float)height - 1.5f, 7.0f, focused ? 1.5f : 1.15f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float, float,
        juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const juce::Colour inkCol = juce::Colour::fromRGB(22, 22, 22);
        const juce::Colour trackBg = juce::Colour::fromRGB(233, 231, 222);
        const juce::Colour accentRed = juce::Colour::fromRGB(217, 74, 67);

        const float cy = (float)y + (float)height * 0.5f;
        const juce::Rectangle<float> track((float)x + 6.0f, cy - 3.0f, (float)width - 12.0f, 6.0f);

        g.setColour(trackBg);
        g.fillRoundedRectangle(track, 3.0f);

        const bool isBipolar = (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 && slider.getMinimum() == -slider.getMaximum());
        if (isBipolar) {
            const float cx = track.getCentreX();
            g.setColour(accentRed);
            g.fillRoundedRectangle(std::min(cx, sliderPos), track.getY(), std::abs(sliderPos - cx), track.getHeight(), 3.0f);
        }
        else {
            g.setColour(accentRed);
            g.fillRoundedRectangle(track.getX(), track.getY(), std::max(0.0f, sliderPos - track.getX()), track.getHeight(), 3.0f);
        }

        g.setColour(inkCol.withAlpha(0.8f));
        g.drawRoundedRectangle(track, 3.0f, 1.0f);

        const juce::Rectangle<float> thumb(sliderPos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        g.setColour(juce::Colour::fromRGB(249, 248, 244));
        g.fillEllipse(thumb);
        g.setColour(inkCol);
        g.drawEllipse(thumb, 1.4f);
    }
};

class VPS_SenderAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer {
public:
    explicit VPS_SenderAudioProcessorEditor(VPS_SenderAudioProcessor&);
    ~VPS_SenderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncColorSelection();
    void generatePaperTexture();

    VPS_SenderAudioProcessor& audioProcessor;
    VPSSenderLookAndFeel lookAndFeel;
    juce::Image paperTexture;

    juce::TextEditor nameEditor;
    juce::TextButton bypassBtn{ "Bypass" };
    juce::Slider gainSlider;
    juce::Slider panSlider;

    // 16 cores (2 fileiras de 8)
    static constexpr std::array<uint32_t, 16> palette = {
        0xFF38BDF8, 0xFF0EA5E9, 0xFF6366F1, 0xFFA78BFA,
        0xFFEC4899, 0xFFF472B6, 0xFFEF4444, 0xFFF87171,
        0xFFFB923C, 0xFFF59E0B, 0xFFFACC15, 0xFFA3E635,
        0xFF4ADE80, 0xFF10B981, 0xFF2DD4BF, 0xFFE2E8F0
    };
    std::array<juce::TextButton, 16> colorButtons;

    static constexpr int kOuterPad = 18;
    static constexpr int kHeaderHeight = 52;

    bool isReceivingAudio = false;
    float currentPan = 0.0f;
    float currentFreq = 0.0f;
    float currentDb = -72.0f;
    float currentDepth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VPS_SenderAudioProcessorEditor)
};