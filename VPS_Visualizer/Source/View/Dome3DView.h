#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include "../Model/SharedDomeHub.h"

namespace vps {

    struct VisualSettings {
        float bgBrightness = 0.72f;
        float grainAmount = 0.14f;
        float sphereScaleMult = 1.0f;
        bool  peakHold = true;
    };

    struct VPSTheme {
        juce::String name;
        juce::Colour bgCol;
        juce::Colour cardBgCol;
        juce::Colour inkCol;
        juce::Colour mutedInkCol;
        juce::Colour hoverCol;
        juce::Colour accentCol;
        uint32_t     defaultTopArgb;
        uint32_t     defaultMidArgb;
        uint32_t     defaultBotArgb;
        bool         isDark;
    };

    inline VPSTheme getThemeById(int id) {
        switch (std::clamp(id, 0, 5)) {
        case 1: // Escuro
            return {
                "Escuro",
                juce::Colour::fromRGB(18, 19, 22),
                juce::Colour::fromRGB(26, 28, 32),
                juce::Colour::fromRGB(238, 238, 242),
                juce::Colour::fromRGB(145, 148, 156),
                juce::Colour::fromRGB(38, 41, 47),
                juce::Colour::fromRGB(224, 82, 74),
                0xFF3B82F6, 0xFF6366F1, 0xFF1E3A8A, true
            };
        case 2: // Cinza
            return {
                "Cinza",
                juce::Colour::fromRGB(132, 134, 138),
                juce::Colour::fromRGB(152, 154, 158),
                juce::Colour::fromRGB(20, 20, 22),
                juce::Colour::fromRGB(58, 60, 65),
                juce::Colour::fromRGB(172, 174, 178),
                juce::Colour::fromRGB(205, 65, 58),
                0xFF94A3B8, 0xFF64748B, 0xFF334155, false
            };
        case 3: // Claro
            return {
                "Claro",
                juce::Colour::fromRGB(252, 252, 253),
                juce::Colour::fromRGB(255, 255, 255),
                juce::Colour::fromRGB(24, 24, 27),
                juce::Colour::fromRGB(113, 113, 122),
                juce::Colour::fromRGB(238, 240, 244),
                juce::Colour::fromRGB(217, 74, 67),
                0xFF38BDF8, 0xFF818CF8, 0xFF1D4ED8, false
            };
        case 4: // Warm
            return {
                "Warm",
                juce::Colour::fromRGB(244, 232, 214),
                juce::Colour::fromRGB(252, 244, 231),
                juce::Colour::fromRGB(43, 27, 18),
                juce::Colour::fromRGB(138, 104, 82),
                juce::Colour::fromRGB(232, 214, 188),
                juce::Colour::fromRGB(217, 74, 67),
                0xFFF97316, 0xFFEC4899, 0xFF9A3412, false
            };
        case 5: // Cold
            return {
                "Cold",
                juce::Colour::fromRGB(224, 236, 245),
                juce::Colour::fromRGB(238, 246, 252),
                juce::Colour::fromRGB(16, 33, 48),
                juce::Colour::fromRGB(82, 116, 142),
                juce::Colour::fromRGB(204, 223, 237),
                juce::Colour::fromRGB(217, 74, 67),
                0xFF22D3EE, 0xFF3B82F6, 0xFF1E3A8A, false
            };
        case 0:
        default: // Padrao (Papel)
            return {
                "Padrao",
                juce::Colour::fromRGB(245, 244, 238),
                juce::Colour::fromRGB(249, 248, 244),
                juce::Colour::fromRGB(22, 22, 22),
                juce::Colour::fromRGB(110, 108, 102),
                juce::Colour::fromRGB(233, 231, 222),
                juce::Colour::fromRGB(217, 74, 67),
                0xFFDF337A, 0xFF8F40A8, 0xFF0F5294, false
            };
        }
    }

    class Dome3DView : public juce::Component,
        private juce::OpenGLRenderer,
        private juce::Timer {
    public:
        enum class ViewMode {
            Front = 0,
            Dome3D = 1,
            Top = 2
        };

        explicit Dome3DView(const SharedDomeHub& hubRef)
            : hub(hubRef)
        {
            for (auto& f : hiddenFlags)
                f.store(false, std::memory_order_relaxed);

            uint32_t tCol, mCol, bCol;
            hub.getGradientColors(tCol, mCol, bCol);
            setGradientColors(tCol, mCol, bCol);
            setChromeColors(getThemeById(hub.getThemeId()));

            openGLContext.setPreferredVersion({ 3, 2 });
            openGLContext.setRenderer(this);
            openGLContext.setComponentPaintingEnabled(true);
            openGLContext.attachTo(*this);
            openGLContext.setContinuousRepainting(false);

            startTimerHz(24);
        }

        ~Dome3DView() override {
            stopTimer();
            openGLContext.detach();
        }

        void setGradientColors(uint32_t topArgb, uint32_t midArgb, uint32_t botArgb) noexcept {
            bgTopAtomic.store((topArgb & 0x00FFFFFFu) ? (0xFF000000u | topArgb) : 0xFFDF337Au, std::memory_order_relaxed);
            bgMidAtomic.store((midArgb & 0x00FFFFFFu) ? (0xFF000000u | midArgb) : 0xFF8F40A8u, std::memory_order_relaxed);
            bgBotAtomic.store((botArgb & 0x00FFFFFFu) ? (0xFF000000u | botArgb) : 0xFF0F5294u, std::memory_order_relaxed);
            repaint();
            openGLContext.triggerRepaint();
        }

        void setChromeColors(const VPSTheme& theme) noexcept {
            chromePaperAtomic.store(theme.bgCol.getARGB(), std::memory_order_relaxed);
            chromeInkAtomic.store(theme.inkCol.getARGB(), std::memory_order_relaxed);
            repaint();
            openGLContext.triggerRepaint();
        }

        void setViewMode(ViewMode mode) {
            viewModeAtomic.store(static_cast<int>(mode), std::memory_order_relaxed);
            if (mode != ViewMode::Front) {
                if (startLayerAtomic.exchange(0, std::memory_order_relaxed) != 0)
                    layersDirty.store(true, std::memory_order_release);
            }
            repaint();
            openGLContext.triggerRepaint();
        }

        ViewMode getViewMode() const noexcept {
            return static_cast<ViewMode>(viewModeAtomic.load(std::memory_order_relaxed));
        }

        void setNumLayers(int layers) {
            const int clamped = std::clamp(layers, 2, 8);
            if (numLayersAtomic.exchange(clamped, std::memory_order_relaxed) != clamped) {
                startLayerAtomic.store(0, std::memory_order_relaxed);
                layersDirty.store(true, std::memory_order_release);
                repaint();
                openGLContext.triggerRepaint();
            }
        }

        int getNumLayers() const noexcept {
            return numLayersAtomic.load(std::memory_order_relaxed);
        }

        void setMonitorMode(int mode) noexcept {
            monitorModeAtomic.store(mode, std::memory_order_relaxed);
            repaint();
        }

        void setBypassed(bool b) noexcept {
            bypassedAtomic.store(b, std::memory_order_relaxed);
            repaint();
        }

        void setSelectedTrack(int slotIndex) noexcept {
            selectedTrackAtomic.store(slotIndex, std::memory_order_relaxed);
        }

        void setTrackHidden(int index, bool hidden) {
            if (index >= 0 && index < kMaxTracks)
                hiddenFlags[static_cast<size_t>(index)].store(hidden, std::memory_order_relaxed);
        }

        bool isTrackHidden(int index) const noexcept {
            if (index < 0 || index >= kMaxTracks) return false;
            return hiddenFlags[static_cast<size_t>(index)].load(std::memory_order_relaxed);
        }

        void setVisualSettings(const VisualSettings& s) {
            bgBrightnessAtomic.store(std::clamp(s.bgBrightness, 0.25f, 1.0f), std::memory_order_relaxed);
            grainAmountAtomic.store(std::clamp(s.grainAmount, 0.0f, 0.30f), std::memory_order_relaxed);
            sphereScaleAtomic.store(std::clamp(s.sphereScaleMult, 0.60f, 1.50f), std::memory_order_relaxed);
            peakHoldAtomic.store(s.peakHold, std::memory_order_relaxed);
            openGLContext.triggerRepaint();
        }

        void resetCamera() {
            freeYaw = 0.0f;
            freePitch = 0.28f;
            targetDistance = 3.15f;
            panOffsetX = 0.0f;
            panOffsetY = 0.0f;
            if (startLayerAtomic.exchange(0, std::memory_order_relaxed) != 0)
                layersDirty.store(true, std::memory_order_release);
            resetRefRequested.store(true, std::memory_order_release);
            repaint();
            openGLContext.triggerRepaint();
        }

        void paint(juce::Graphics& g) override {
            const auto w = static_cast<float>(getWidth());
            const auto h = static_cast<float>(getHeight());
            if (w < 10.0f || h < 10.0f) return;

            if (bypassedAtomic.load(std::memory_order_relaxed)) {
                g.setColour(juce::Colours::black.withAlpha(0.50f));
                g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 11.0f);
                g.setColour(juce::Colours::white.withAlpha(0.90f));
                g.setFont(juce::FontOptions("Sofia Pro", 16.0f, juce::Font::bold));
                g.drawText("BYPASS ATIVO", getLocalBounds(), juce::Justification::centred);
                return;
            }

            const int mon = monitorModeAtomic.load(std::memory_order_relaxed);
            if (mon != 0) {
                const juce::Colour maskCol = juce::Colours::black.withAlpha(0.56f);
                const juce::Colour lineCol = juce::Colours::white.withAlpha(0.45f);
                const float cx = w * 0.5f;

                if (mon == 1) {
                    const float midHalf = w * 0.14f;
                    g.setColour(maskCol);
                    g.fillRect(2.0f, 2.0f, cx - midHalf - 2.0f, h - 4.0f);
                    g.fillRect(cx + midHalf, 2.0f, w - (cx + midHalf) - 2.0f, h - 4.0f);
                    g.setColour(lineCol);
                    g.drawVerticalLine(juce::roundToInt(cx - midHalf), 10.0f, h - 10.0f);
                    g.drawVerticalLine(juce::roundToInt(cx + midHalf), 10.0f, h - 10.0f);
                }
                else if (mon == 2) {
                    const float midHalf = w * 0.14f;
                    g.setColour(maskCol);
                    g.fillRect(cx - midHalf, 2.0f, midHalf * 2.0f, h - 4.0f);
                    g.setColour(lineCol);
                    g.drawVerticalLine(juce::roundToInt(cx - midHalf), 10.0f, h - 10.0f);
                    g.drawVerticalLine(juce::roundToInt(cx + midHalf), 10.0f, h - 10.0f);
                }
                else if (mon == 3) {
                    g.setColour(maskCol);
                    g.fillRect(cx, 2.0f, w - cx - 2.0f, h - 4.0f);
                    g.setColour(lineCol);
                    g.drawVerticalLine(juce::roundToInt(cx), 10.0f, h - 10.0f);
                }
                else if (mon == 4) {
                    g.setColour(maskCol);
                    g.fillRect(2.0f, 2.0f, cx - 2.0f, h - 4.0f);
                    g.setColour(lineCol);
                    g.drawVerticalLine(juce::roundToInt(cx), 10.0f, h - 10.0f);
                }
            }

            const int layers = getNumLayers();
            const ViewMode mode = getViewMode();

            if (mode == ViewMode::Top) {
                const float outerR = 0.96f;
                const float topY = h * 0.5f * (1.0f - (outerR - currentPanY) / 1.12f);
                const float botY = h * 0.5f * (1.0f - (-outerR - currentPanY) / 1.12f);
                const float span = botY - topY;
                const float rx = 14.0f;

                g.setColour(juce::Colours::white.withAlpha(0.32f));
                g.drawVerticalLine(juce::roundToInt(rx + 28.0f), topY, botY);

                g.setFont(juce::FontOptions("Sofia Pro", 10.5f, juce::Font::bold));
                for (int i = 0; i < layers; ++i) {
                    const float y0 = topY + (span * i) / layers;
                    const float y1 = topY + (span * (i + 1)) / layers;
                    const float ym = 0.5f * (y0 + y1);

                    g.setColour(juce::Colours::white.withAlpha(0.35f));
                    g.drawHorizontalLine(juce::roundToInt(y0), rx + 22.0f, rx + 28.0f);

                    g.setColour(juce::Colours::white.withAlpha(0.78f));
                    g.drawText("C" + juce::String(layers - i),
                        juce::roundToInt(rx), juce::roundToInt(ym - 8.0f), 22, 16,
                        juce::Justification::centredRight);
                }
                g.setColour(juce::Colours::white.withAlpha(0.35f));
                g.drawHorizontalLine(juce::roundToInt(botY), rx + 22.0f, rx + 28.0f);
                floorBarBounds = {};
            }
            else if (mode == ViewMode::Front) {
                const int startL = std::clamp(startLayerAtomic.load(std::memory_order_relaxed), 0, layers - 1);
                const float segW = 36.0f;
                const float barW = segW * layers;
                const float barH = 22.0f;
                const float barX = (w - barW) * 0.5f;
                const float barY = h - barH - 12.0f;
                floorBarBounds = { juce::roundToInt(barX), juce::roundToInt(barY), juce::roundToInt(barW), juce::roundToInt(barH) };

                g.setFont(juce::FontOptions("Sofia Pro", 10.5f, juce::Font::bold));
                for (int i = 0; i < layers; ++i) {
                    const juce::Rectangle<float> seg(barX + i * segW + 2.0f, barY, segW - 4.0f, barH);
                    const bool isStart = (i == startL);
                    const bool inRange = (i >= startL);

                    g.setColour(isStart ? juce::Colour::fromRGB(217, 74, 67).withAlpha(0.92f)
                        : (inRange ? juce::Colours::black.withAlpha(0.55f)
                            : juce::Colours::black.withAlpha(0.25f)));
                    g.fillRoundedRectangle(seg, 5.0f);

                    g.setColour(juce::Colours::white.withAlpha(isStart ? 0.95f : (inRange ? 0.45f : 0.18f)));
                    g.drawRoundedRectangle(seg, 5.0f, 1.0f);

                    g.setColour(juce::Colours::white.withAlpha(isStart ? 1.0f : (inRange ? 0.80f : 0.35f)));
                    g.drawText("C" + juce::String(i + 1), seg.toNearestInt(), juce::Justification::centred);
                }
            }
            else {
                floorBarBounds = {};
            }

            const int hoverIdx = hoveredTrackIndex;
            if (hoverIdx >= 0) {
                const auto* slot = hub.getSlot(hoverIdx);
                if (slot != nullptr && slot->active.load(std::memory_order_acquire) != 0) {
                    const juce::String label = juce::String(slot->name) + "  ("
                        + juce::String(juce::roundToInt(slot->freqHz.load(std::memory_order_relaxed))) + " Hz)";
                    const juce::Font f(juce::FontOptions("Sofia Pro", 12.0f, juce::Font::bold));
                    const int tw = juce::roundToInt(juce::GlyphArrangement::getStringWidth(f, label)) + 18;
                    const int th = 24;

                    int tx = juce::roundToInt(hoveredScreenPos.x) - tw / 2;
                    int ty = juce::roundToInt(hoveredScreenPos.y) - th - 14;
                    tx = std::clamp(tx, 10, getWidth() - tw - 10);
                    ty = std::clamp(ty, 10, getHeight() - th - 10);

                    const juce::Rectangle<float> pill((float)tx, (float)ty, (float)tw, (float)th);
                    g.setColour(juce::Colour::fromRGB(20, 20, 22).withAlpha(0.92f));
                    g.fillRoundedRectangle(pill, 6.0f);
                    g.setColour(juce::Colours::white.withAlpha(0.80f));
                    g.drawRoundedRectangle(pill, 6.0f, 1.0f);

                    g.setFont(f);
                    g.setColour(juce::Colours::white);
                    g.drawText(label, pill.toNearestInt(), juce::Justification::centred);
                }
            }
        }

        void mouseMove(const juce::MouseEvent& e) override {
            updateHoverAt(e.position);
            repaint();
        }

        void mouseExit(const juce::MouseEvent&) override {
            hoveredTrackIndex = -1;
            repaint();
        }

        void mouseDown(const juce::MouseEvent& e) override {
            lastMousePos = e.getPosition();
            ctrlDragging = e.mods.isCtrlDown();
            ctrlMoved = false;

            if (!ctrlDragging && getViewMode() == ViewMode::Front && floorBarBounds.contains(e.getPosition())) {
                const int layers = getNumLayers();
                const float relX = static_cast<float>(e.x - floorBarBounds.getX())
                    / static_cast<float>(std::max(1, floorBarBounds.getWidth()));
                const int clickedLayer = std::clamp(static_cast<int>(relX * layers), 0, layers - 1);
                const int cur = startLayerAtomic.load(std::memory_order_relaxed);
                const int next = (cur == clickedLayer) ? 0 : clickedLayer;
                startLayerAtomic.store(next, std::memory_order_relaxed);
                layersDirty.store(true, std::memory_order_release);
                repaint();
                return;
            }

            updateHoverAt(e.position);
            repaint();
        }

        void mouseDrag(const juce::MouseEvent& e) override {
            auto delta = e.getPosition() - lastMousePos;
            lastMousePos = e.getPosition();

            if (e.mods.isCtrlDown() || ctrlDragging) {
                ctrlMoved = true;
                const float h = std::max(1.0f, static_cast<float>(getHeight()));
                const float scale = (getViewMode() == ViewMode::Top)
                    ? (2.24f / h)
                    : (currentDist * 0.72f / h);

                panOffsetX = std::clamp(panOffsetX + delta.x * scale, -1.6f, 1.6f);
                panOffsetY = std::clamp(panOffsetY - delta.y * scale, -1.6f, 1.6f);
                return;
            }

            if (getViewMode() != ViewMode::Dome3D)
                return;

            freeYaw += delta.x * 0.01f;
            freePitch = std::clamp(freePitch + delta.y * 0.01f, -0.05f, 1.45f);
        }

        void mouseUp(const juce::MouseEvent& e) override {
            if (ctrlDragging && !ctrlMoved && getWidth() > 0 && getHeight() > 0) {
                const float nx = (e.position.x / static_cast<float>(getWidth())) * 2.0f - 1.0f;
                const float ny = 1.0f - (e.position.y / static_cast<float>(getHeight())) * 2.0f;
                const float aspect = static_cast<float>(getWidth()) / static_cast<float>(getHeight());

                if (getViewMode() == ViewMode::Top) {
                    panOffsetX = std::clamp(panOffsetX - nx * 1.12f * aspect, -1.6f, 1.6f);
                    panOffsetY = std::clamp(panOffsetY - ny * 1.12f, -1.6f, 1.6f);
                }
                else {
                    const float spanY = currentDist * 0.68f;
                    panOffsetX = std::clamp(panOffsetX - nx * spanY * aspect, -1.6f, 1.6f);
                    panOffsetY = std::clamp(panOffsetY - ny * spanY, -1.6f, 1.6f);
                }
            }

            ctrlDragging = false;
            ctrlMoved = false;
        }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
            if (getViewMode() != ViewMode::Dome3D)
                return;

            targetDistance = std::clamp(targetDistance - wheel.deltaY * 1.2f, 2.2f, 6.5f);
        }

    private:
        static constexpr float kDomeRadius = 1.45f;
        static constexpr float kFrontHalfW = 1.42f;
        static constexpr float kFrontHeight = 1.44f;
        static constexpr float kFrontZBack = -1.05f;
        static constexpr float kFrontZFront = 0.95f;

        struct Vertex {
            float x, y, z;
            float nx, ny, nz;
        };

        struct VisualTrackState {
            bool initialized = false;
            float anchorX = 0.0f;
            float anchorY = 0.3f;
            float anchorZ = 0.0f;
            float anchorWidth = 0.0f;
            float anchorDiff = 0.0f;
            float fastZ = 0.0f;
            float pulse = 0.0f;
            float visibility = 0.0f;
        };

        struct DrawableTrack {
            int index;
            float x, y, z;
            float sx, sy, sz;
            float sortDepth;
            float r, g, b, alpha;
            float diffusion;
            float highlight;
        };

        struct ProjectedTrack {
            int index;
            float px, py;
            float radiusPx;
        };

        struct StaticMesh {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLsizei vertexCount = 0;

            void upload(const std::vector<Vertex>& verts) {
                using namespace juce::gl;
                release();
                if (verts.empty()) return;

                vertexCount = static_cast<GLsizei>(verts.size());
                glGenVertexArrays(1, &vao);
                glGenBuffers(1, &vbo);

                glBindVertexArray(vao);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                    verts.data(),
                    GL_STATIC_DRAW);

                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                    reinterpret_cast<const void*>(offsetof(Vertex, x)));
                glEnableVertexAttribArray(0);

                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                    reinterpret_cast<const void*>(offsetof(Vertex, nx)));
                glEnableVertexAttribArray(1);

                glBindVertexArray(0);
            }

            void release() {
                using namespace juce::gl;
                if (vbo != 0) { glDeleteBuffers(1, &vbo); vbo = 0; }
                if (vao != 0) { glDeleteVertexArrays(1, &vao); vao = 0; }
                vertexCount = 0;
            }
        };

        static std::array<float, 3> argbToRgb3(uint32_t argb) noexcept {
            const juce::Colour c(argb);
            return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
        }

        void updateHoverAt(juce::Point<float> pos) {
            const juce::SpinLock::ScopedLockType sl(projLock);
            int bestIdx = -1;
            float bestDist = 1e9f;
            juce::Point<float> bestPos = pos;

            for (const auto& pt : projectedTracks) {
                const float dx = pos.x - pt.px;
                const float dy = pos.y - pt.py;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float hitR = std::max(22.0f, pt.radiusPx);
                if (d <= hitR && d < bestDist) {
                    bestDist = d;
                    bestIdx = pt.index;
                    bestPos = { pt.px, pt.py };
                }
            }
            hoveredTrackIndex = bestIdx;
            hoveredScreenPos = bestPos;
        }

        void timerCallback() override {
            repaint();
            openGLContext.triggerRepaint();
        }

        void newOpenGLContextCreated() override {
            using namespace juce::gl;

            const char* vertexShader = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            layout (location = 1) in vec3 aNormal;
            uniform mat4 uProj;
            uniform mat4 uCamTrans;
            uniform mat4 uCamRot;
            uniform mat4 uModel;
            out vec3 vNormal;
            void main() {
                gl_Position = uProj * uCamTrans * uCamRot * uModel * vec4(aPos, 1.0);
                vNormal = mat3(uCamRot) * aNormal;
            }
        )";

            const char* fragmentShader = R"(
            #version 330 core
            uniform vec4 uColor;
            uniform float uUseLighting;
            uniform float uDiffusion;
            uniform float uHighlight;
            uniform float uGrain;
            in vec3 vNormal;
            out vec4 FragColor;

            void main() {
                if (uUseLighting > 0.5) {
                    vec3 n = normalize(vNormal);
                    float ndv = max(n.z, 0.0);

                    float g = clamp(dot(n, vec3(-0.44, 0.71, 0.55)) * 0.5 + 0.5, 0.0, 1.0);
                    vec3 shadowCol = uColor.rgb * vec3(0.24, 0.20, 0.32);
                    vec3 highCol   = mix(uColor.rgb, vec3(1.0, 0.96, 0.90), 0.56);

                    vec3 col = (g < 0.55)
                        ? mix(shadowCol, uColor.rgb, g * 1.818)
                        : mix(uColor.rgb, highCol, (g - 0.55) * 2.222);

                    float inv = 1.0 - ndv;
                    col += mix(uColor.rgb, vec3(1.0), 0.40) * (inv * inv * 0.28);

                    if (uHighlight > 0.5) {
                        float ring = smoothstep(0.38, 0.78, inv);
                        col = mix(col, vec3(1.0, 0.99, 0.95), ring * 0.95);
                    }

                    float grain = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * uGrain;
                    col = clamp(col + vec3(grain), 0.0, 1.0);

                    float edgeFade   = mix(1.0, smoothstep(0.0, 0.65, ndv) * 0.78 + 0.22, uDiffusion * 0.80);
                    float finalAlpha = uColor.a * (uHighlight > 0.5 ? 1.0 : edgeFade);

                    FragColor = vec4(col, finalAlpha);
                } else {
                    FragColor = uColor;
                }
            }
        )";

            shader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
            if (shader->addVertexShader(vertexShader) &&
                shader->addFragmentShader(fragmentShader) &&
                shader->link())
            {
                uProj = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uProj");
                uCamTrans = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uCamTrans");
                uCamRot = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uCamRot");
                uModel = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uModel");
                uColor = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uColor");
                uUseLighting = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uUseLighting");
                uDiffusion = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uDiffusion");
                uHighlight = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uHighlight");
                uGrain = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "uGrain");
            }

            const char* quadVert = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            out vec2 vUv;
            void main() {
                vUv = aPos.xy * 0.5 + 0.5;
                gl_Position = vec4(aPos.xy, 0.0, 1.0);
            }
        )";

            const char* bgFrag = R"(
            #version 330 core
            uniform float uBrightness;
            uniform float uBgGrain;
            uniform vec3  uTopCol;
            uniform vec3  uMidCol;
            uniform vec3  uBotCol;
            in vec2 vUv;
            out vec4 FragColor;

            void main() {
                vec2 uv = vUv;
                float x2 = uv.x * uv.x;
                float wave = 0.35 - 0.12 * sin(uv.x * 3.14159) + 0.75 * x2;
                float d = uv.y - wave;

                vec3 col;
                if (d >= 0.0) {
                    col = mix(uMidCol, uTopCol, smoothstep(0.0, 0.75, d));
                } else {
                    col = mix(uMidCol, uBotCol, smoothstep(0.0, 0.65, -d));
                }

                float grain = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * uBgGrain;
                col = clamp(col + vec3(grain), 0.0, 1.0);

                vec2 c = uv - 0.5;
                float vig = 1.0 - 0.18 * dot(c, c);
                FragColor = vec4(clamp(col * uBrightness * vig, 0.0, 1.0), 1.0);
            }
        )";

            bgShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
            if (bgShader->addVertexShader(quadVert) &&
                bgShader->addFragmentShader(bgFrag) &&
                bgShader->link())
            {
                uBgBrightness = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*bgShader, "uBrightness");
                uBgGrain = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*bgShader, "uBgGrain");
                uTopCol = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*bgShader, "uTopCol");
                uMidCol = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*bgShader, "uMidCol");
                uBotCol = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*bgShader, "uBotCol");
            }
            else {
                bgShader.reset();
            }

            const char* borderFrag = R"(
            #version 330 core
            uniform vec2  uSize;
            uniform float uRadius;
            uniform float uStroke;
            uniform vec3  uPaperCol;
            uniform vec3  uInkCol;
            in vec2 vUv;
            out vec4 FragColor;

            float sdRoundedBox(vec2 p, vec2 b, float r) {
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }

            void main() {
                vec2 p = (vUv - 0.5) * uSize;
                vec2 halfSize = uSize * 0.5 - vec2(uStroke * 0.5);
                float d = sdRoundedBox(p, halfSize, uRadius);

                float aa = 1.0;
                float outerMask  = smoothstep(-aa, aa, d);
                float strokeMask = smoothstep(-uStroke - aa, -uStroke + aa, d) * (1.0 - outerMask);
                float alpha = clamp(outerMask + strokeMask, 0.0, 1.0);

                if (alpha < 0.002)
                    discard;

                vec3 col = mix(uInkCol, uPaperCol, outerMask / alpha);
                FragColor = vec4(col, alpha);
            }
        )";

            borderShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
            if (borderShader->addVertexShader(quadVert) &&
                borderShader->addFragmentShader(borderFrag) &&
                borderShader->link())
            {
                uBorderSize = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*borderShader, "uSize");
                uBorderRadius = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*borderShader, "uRadius");
                uBorderStroke = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*borderShader, "uStroke");
                uPaperCol = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*borderShader, "uPaperCol");
                uInkCol = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*borderShader, "uInkCol");
            }
            else {
                borderShader.reset();
            }

            bgQuadMesh.upload({
                { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
                { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
                { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
                { 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f }
                });

            domeMesh.upload(buildDomeGeometry());
            axesMesh.upload(buildAxesGeometry());
            centerAxisMesh.upload(buildCenterAxisGeometry());
            topCircleFrameMesh.upload(buildTopCircleFrame());
            sphereMesh.upload(buildSphereGeometry(0.11f, 12, 12));
            stemMesh.upload({
                { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f }
                });

            rebuildFrontMeshes(getNumLayers(), startLayerAtomic.load(std::memory_order_relaxed));
            layersDirty.store(false, std::memory_order_release);
        }

        void openGLContextClosing() override {
            bgQuadMesh.release();
            domeMesh.release();
            axesMesh.release();
            centerAxisMesh.release();
            sphereMesh.release();
            stemMesh.release();
            frontFrameMesh.release();
            frontLayersMesh.release();
            topCircleFrameMesh.release();
            domeLayerSlicesMesh.release();
            topCircleLayersMesh.release();

            uProj.reset(); uCamTrans.reset(); uCamRot.reset(); uModel.reset();
            uColor.reset(); uUseLighting.reset(); uDiffusion.reset(); uHighlight.reset(); uGrain.reset();
            uBgBrightness.reset(); uBgGrain.reset(); uTopCol.reset(); uMidCol.reset(); uBotCol.reset();
            uBorderSize.reset(); uBorderRadius.reset(); uBorderStroke.reset(); uPaperCol.reset(); uInkCol.reset();
            shader.reset(); bgShader.reset(); borderShader.reset();
        }

        void rebuildFrontMeshes(int layers, int startLayer) {
            const int clampedStart = std::clamp(startLayer, 0, layers - 1);
            const float zFrontStart = kFrontZFront - (static_cast<float>(clampedStart) / static_cast<float>(layers)) * (kFrontZFront - kFrontZBack);

            frontFrameMesh.upload(buildFrontOuterFrame(zFrontStart));
            frontLayersMesh.upload(buildFrontLayerFrames(layers, clampedStart));
            domeLayerSlicesMesh.upload(buildDomeLayerSlices(layers));
            topCircleLayersMesh.upload(buildTopCircleLayerLines(layers));
        }

        void renderOpenGL() override {
            using namespace juce::gl;
            if (shader == nullptr || uProj == nullptr) return;

            if (layersDirty.exchange(false, std::memory_order_acq_rel))
                rebuildFrontMeshes(getNumLayers(), startLayerAtomic.load(std::memory_order_relaxed));

            const ViewMode mode = getViewMode();
            const float scale = (float)openGLContext.getRenderingScale();
            const int w = juce::roundToInt(getWidth() * scale);
            const int h = juce::roundToInt(getHeight() * scale);
            glViewport(0, 0, w, h);

            const auto t = argbToRgb3(bgTopAtomic.load(std::memory_order_relaxed));
            const auto m = argbToRgb3(bgMidAtomic.load(std::memory_order_relaxed));
            const auto b = argbToRgb3(bgBotAtomic.load(std::memory_order_relaxed));
            const float bright = bgBrightnessAtomic.load(std::memory_order_relaxed);
            const float grain = grainAmountAtomic.load(std::memory_order_relaxed);

            glClearColor(m[0] * bright * 0.5f, m[1] * bright * 0.5f, m[2] * bright * 0.5f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);

            if (bgShader != nullptr && bgQuadMesh.vao != 0) {
                glDisable(GL_DEPTH_TEST);
                bgShader->use();
                if (uBgBrightness) uBgBrightness->set(bright);
                if (uBgGrain)      uBgGrain->set(grain);
                if (uTopCol)       uTopCol->set(t[0], t[1], t[2]);
                if (uMidCol)       uMidCol->set(m[0], m[1], m[2]);
                if (uBotCol)       uBotCol->set(b[0], b[1], b[2]);

                glBindVertexArray(bgQuadMesh.vao);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, bgQuadMesh.vertexCount);
                glBindVertexArray(0);
            }

            glEnable(GL_DEPTH_TEST);
            shader->use();
            if (uGrain) uGrain->set(grain);

            currentPanX += (panOffsetX - currentPanX) * 0.24f;
            currentPanY += (panOffsetY - currentPanY) * 0.24f;

            const float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
            const auto identity = juce::Matrix3D<float>();

            juce::Matrix3D<float> activeProj = identity;
            juce::Matrix3D<float> activeCamTrans = identity;
            juce::Matrix3D<float> activeCamRot = identity;

            if (mode == ViewMode::Top) {
                activeProj = makeOrtho(
                    -1.12f * aspect - currentPanX, 1.12f * aspect - currentPanX,
                    -1.12f - currentPanY, 1.12f - currentPanY,
                    -5.0f, 5.0f);
                uProj->setMatrix4(activeProj.mat, 1, GL_FALSE);
                uCamTrans->setMatrix4(identity.mat, 1, GL_FALSE);
                uCamRot->setMatrix4(identity.mat, 1, GL_FALSE);

                glDisable(GL_DEPTH_TEST);
                drawMesh(topCircleFrameMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.42f, false);
                drawMesh(topCircleLayersMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.50f, false);

                renderTracks(mode, activeProj, activeCamTrans, activeCamRot);
                glEnable(GL_DEPTH_TEST);
            }
            else {
                const int layers = getNumLayers();
                const int startL = std::clamp(startLayerAtomic.load(std::memory_order_relaxed), 0, layers - 1);
                const float zShift = (static_cast<float>(startL) / static_cast<float>(layers)) * (kFrontZFront - kFrontZBack);

                float goalDist = (mode == ViewMode::Front) ? (2.42f - zShift) : targetDistance;
                float goalYaw = (mode == ViewMode::Front) ? 0.0f : freeYaw;
                float goalPitch = (mode == ViewMode::Front) ? 0.0f : freePitch;
                float goalCamY = (mode == ViewMode::Front) ? (-kFrontHeight * 0.5f) : -0.48f;

                currentYaw += (goalYaw - currentYaw) * 0.22f;
                currentPitch += (goalPitch - currentPitch) * 0.22f;
                currentCamY += (goalCamY - currentCamY) * 0.22f;
                currentDist += (goalDist - currentDist) * 0.22f;

                activeProj = juce::Matrix3D<float>::fromFrustum(-0.34f * aspect, 0.34f * aspect, -0.34f, 0.34f, 0.5f, 25.0f);
                activeCamTrans = juce::Matrix3D<float>::fromTranslation({ currentPanX, currentCamY + currentPanY, -currentDist });
                activeCamRot = juce::Matrix3D<float>::rotation({ currentPitch, currentYaw, 0.0f });

                uProj->setMatrix4(activeProj.mat, 1, GL_FALSE);
                uCamTrans->setMatrix4(activeCamTrans.mat, 1, GL_FALSE);
                uCamRot->setMatrix4(activeCamRot.mat, 1, GL_FALSE);

                if (mode == ViewMode::Front) {
                    drawMesh(frontFrameMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.38f, false);
                    drawMesh(frontLayersMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.55f, false);
                }
                else {
                    drawMesh(domeMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.22f, false);
                    drawMesh(domeLayerSlicesMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.48f, false);
                    drawMesh(axesMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.32f, false);
                    drawMesh(centerAxisMesh, identity, GL_LINES, 1.0f, 1.0f, 1.0f, 0.42f, false);
                }

                renderTracks(mode, activeProj, activeCamTrans, activeCamRot);
            }

            glDisable(GL_CULL_FACE);
            if (borderShader != nullptr && uBorderSize != nullptr && bgQuadMesh.vao != 0) {
                glDisable(GL_DEPTH_TEST);
                borderShader->use();
                uBorderSize->set(static_cast<float>(w), static_cast<float>(h));
                uBorderRadius->set(12.0f * scale);
                uBorderStroke->set(1.4f * scale);

                const auto pCol = argbToRgb3(chromePaperAtomic.load(std::memory_order_relaxed));
                const auto iCol = argbToRgb3(chromeInkAtomic.load(std::memory_order_relaxed));
                if (uPaperCol) uPaperCol->set(pCol[0], pCol[1], pCol[2]);
                if (uInkCol)   uInkCol->set(iCol[0], iCol[1], iCol[2]);

                glBindVertexArray(bgQuadMesh.vao);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, bgQuadMesh.vertexCount);
                glEnable(GL_DEPTH_TEST);
            }

            glBindVertexArray(0);
        }

        static std::pair<float, float> projectToScreen(float x, float y, float z,
            const juce::Matrix3D<float>& proj,
            const juce::Matrix3D<float>& camTrans,
            const juce::Matrix3D<float>& camRot,
            float compW, float compH)
        {
            auto mul = [](const juce::Matrix3D<float>& m, float vx, float vy, float vz, float vw) {
                return std::array<float, 4>{
                    m.mat[0] * vx + m.mat[4] * vy + m.mat[8] * vz + m.mat[12] * vw,
                        m.mat[1] * vx + m.mat[5] * vy + m.mat[9] * vz + m.mat[13] * vw,
                        m.mat[2] * vx + m.mat[6] * vy + m.mat[10] * vz + m.mat[14] * vw,
                        m.mat[3] * vx + m.mat[7] * vy + m.mat[11] * vz + m.mat[15] * vw
                };
                };
            auto v1 = mul(camRot, x, y, z, 1.0f);
            auto v2 = mul(camTrans, v1[0], v1[1], v1[2], v1[3]);
            auto v3 = mul(proj, v2[0], v2[1], v2[2], v2[3]);
            const float invW = (std::abs(v3[3]) > 1e-5f) ? (1.0f / v3[3]) : 1.0f;
            const float ndcX = v3[0] * invW;
            const float ndcY = v3[1] * invW;
            return { (ndcX * 0.5f + 0.5f) * compW, (0.5f - ndcY * 0.5f) * compH };
        }

        void renderTracks(ViewMode mode,
            const juce::Matrix3D<float>& proj,
            const juce::Matrix3D<float>& camTrans,
            const juce::Matrix3D<float>& camRot)
        {
            using namespace juce::gl;
            const int64_t nowMs = juce::Time::currentTimeMillis();
            const int selectedIdx = selectedTrackAtomic.load(std::memory_order_relaxed);
            const int layers = getNumLayers();
            const int startLayer = (mode == ViewMode::Front)
                ? std::clamp(startLayerAtomic.load(std::memory_order_relaxed), 0, layers - 1)
                : 0;
            const float scaleMult = sphereScaleAtomic.load(std::memory_order_relaxed);
            const bool usePeakHold = peakHoldAtomic.load(std::memory_order_relaxed);

            if (resetRefRequested.exchange(false, std::memory_order_acq_rel))
                referenceDepth = 0.45f;

            float frameMaxDepth = 0.0f;
            for (int i = 0; i < kMaxTracks; ++i) {
                if (isTrackHidden(i)) continue;
                const auto* slot = hub.getSlot(i);
                if (slot != nullptr
                    && slot->active.load(std::memory_order_acquire) != 0
                    && (nowMs - slot->lastHeartbeatMs.load(std::memory_order_relaxed) <= 600))
                {
                    const float d = slot->depthZ.load(std::memory_order_relaxed);
                    if (d > frameMaxDepth) frameMaxDepth = d;
                }
            }

            if (usePeakHold) {
                if (frameMaxDepth > referenceDepth)
                    referenceDepth += (frameMaxDepth - referenceDepth) * 0.30f;
            }
            else {
                const float target = std::max(0.45f, frameMaxDepth);
                referenceDepth += (target - referenceDepth) * ((target > referenceDepth) ? 0.25f : 0.02f);
            }

            const float safeRef = std::max(0.45f, referenceDepth);

            drawList.clear();
            std::vector<ProjectedTrack> nextProjected;

            const float compW = static_cast<float>(getWidth());
            const float compH = static_cast<float>(getHeight());

            for (int i = 0; i < kMaxTracks; ++i) {
                auto& st = trackStates[static_cast<size_t>(i)];

                if (isTrackHidden(i)) {
                    st.visibility = 0.0f;
                    continue;
                }

                const auto* slot = hub.getSlot(i);
                const bool slotAlive = (slot != nullptr
                    && slot->active.load(std::memory_order_acquire) != 0
                    && (nowMs - slot->lastHeartbeatMs.load(std::memory_order_relaxed) <= 600));

                if (!slotAlive) {
                    st.visibility *= 0.75f;
                    if (st.visibility < 0.01f) {
                        st.visibility = 0.0f;
                        st.initialized = false;
                    }
                    continue;
                }

                const float rawPan = slot->panX.load(std::memory_order_relaxed);
                const float rawFreq = slot->freqY.load(std::memory_order_relaxed);
                const float rawDepth = slot->depthZ.load(std::memory_order_relaxed);
                const float rawWidth = slot->stereoWidth.load(std::memory_order_relaxed);
                const float rawDiff = slot->diffusion.load(std::memory_order_relaxed);

                float targetVis = 0.0f;
                if (rawDepth >= 0.03f) targetVis = 1.0f;
                else if (rawDepth > 0.01f) targetVis = (rawDepth - 0.01f) / 0.02f;

                st.visibility += (targetVis - st.visibility) * ((targetVis > st.visibility) ? 0.45f : 0.08f);
                if (st.visibility < 0.015f) {
                    st.pulse *= 0.80f;
                    continue;
                }

                if (!st.initialized) {
                    st.anchorX = rawPan; st.anchorY = rawFreq; st.anchorZ = rawDepth;
                    st.anchorWidth = rawWidth; st.anchorDiff = rawDiff;
                    st.fastZ = rawDepth; st.pulse = 0.0f; st.initialized = true;
                }
                else {
                    const float panSmooth = (mode == ViewMode::Front) ? 0.14f : 0.20f;
                    const float freqSmooth = (mode == ViewMode::Front) ? 0.08f : 0.14f;
                    const float zSmooth = (mode == ViewMode::Front) ? 0.10f : 0.16f;

                    st.anchorX += (rawPan - st.anchorX) * panSmooth;
                    st.anchorY += (rawFreq - st.anchorY) * freqSmooth;
                    st.anchorZ += (rawDepth - st.anchorZ) * zSmooth;
                    st.anchorWidth += (rawWidth - st.anchorWidth) * 0.16f;
                    st.anchorDiff += (rawDiff - st.anchorDiff) * 0.14f;

                    st.fastZ += (rawDepth - st.fastZ) * 0.45f;
                    const float transient = std::max(0.0f, st.fastZ - st.anchorZ);
                    st.pulse += (std::clamp(transient * 1.8f, 0.0f, 0.20f) - st.pulse) * 0.28f;
                }

                const float relZ = std::clamp(st.anchorZ / safeRef, 0.0f, 1.0f);

                if (mode == ViewMode::Front && startLayer > 0) {
                    const int trackLayer = std::clamp(static_cast<int>((1.0f - relZ) * layers), 0, layers - 1);
                    if (trackLayer < startLayer)
                        continue;
                }

                const juce::Colour col(slot->colorArgb.load(std::memory_order_relaxed));
                const float widthStretch = 1.0f + st.anchorWidth * 1.45f;
                const float diffExpand = 1.0f + st.anchorDiff * 0.24f;

                DrawableTrack dt;
                dt.index = i;
                dt.r = col.getFloatRed();
                dt.g = col.getFloatGreen();
                dt.b = col.getFloatBlue();
                dt.alpha = 0.96f * st.visibility;
                dt.diffusion = st.anchorDiff;
                dt.highlight = (i == selectedIdx) ? 1.0f : 0.0f;

                const float pan = std::clamp(st.anchorX, -1.0f, 1.0f);
                const float freqY = std::clamp(st.anchorY, 0.0f, 1.0f);

                if (mode == ViewMode::Top) {
                    constexpr float maxR = 0.88f;
                    float x = pan * maxR;
                    float y = (1.0f - relZ * 2.0f) * maxR;
                    const float dist = std::sqrt(x * x + y * y);
                    if (dist > maxR) { const float s = maxR / dist; x *= s; y *= s; }
                    const float baseS = (0.28f + relZ * 0.42f + st.pulse * 0.24f) * scaleMult;
                    dt.x = x; dt.y = y; dt.z = 0.0f;
                    dt.sx = baseS * widthStretch; dt.sy = baseS * diffExpand; dt.sz = 0.05f;
                    dt.sortDepth = relZ;
                }
                else if (mode == ViewMode::Front) {
                    dt.x = pan * (kFrontHalfW * 0.86f);
                    dt.y = 0.20f + freqY * (kFrontHeight - 0.40f);
                    dt.z = kFrontZBack + relZ * (kFrontZFront - kFrontZBack);
                    const float baseS = (0.42f + relZ * 1.05f + st.pulse * 0.36f) * scaleMult;
                    dt.sx = baseS * widthStretch; dt.sy = baseS * (1.0f + st.anchorDiff * 0.14f); dt.sz = baseS;
                    dt.sortDepth = dt.z;
                }
                else {
                    const float maxR = kDomeRadius * 0.92f;
                    float x = pan * maxR;
                    float y = 0.12f + freqY * (kDomeRadius * 0.82f);
                    float z = (relZ * 2.0f - 1.0f) * maxR;
                    const float dist = std::sqrt(x * x + y * y + z * z);
                    if (dist > maxR) { const float s = maxR / dist; x *= s; y *= s; z *= s; }
                    const float baseS = (0.32f + relZ * 0.46f + st.pulse * 0.28f) * scaleMult;
                    dt.x = x; dt.y = y; dt.z = z;
                    dt.sx = baseS * widthStretch; dt.sy = baseS; dt.sz = baseS * diffExpand;
                    dt.sortDepth = z * std::cos(currentYaw) - x * std::sin(currentYaw);
                }

                const auto [px, py] = projectToScreen(dt.x, dt.y, dt.z, proj, camTrans, camRot, compW, compH);
                nextProjected.push_back({ i, px, py, std::max(20.0f, dt.sy * 42.0f) });
                drawList.push_back(dt);
            }

            {
                const juce::SpinLock::ScopedLockType sl(projLock);
                projectedTracks = std::move(nextProjected);
            }

            std::sort(drawList.begin(), drawList.end(),
                [](const DrawableTrack& a, const DrawableTrack& b) { return a.sortDepth < b.sortDepth; });

            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);

            for (const auto& dt : drawList) {
                if (mode == ViewMode::Dome3D) {
                    juce::Matrix3D<float> stemModel;
                    stemModel.mat[5] = dt.y; stemModel.mat[12] = dt.x; stemModel.mat[14] = dt.z;
                    drawMesh(stemMesh, stemModel, GL_LINES, 1.0f, 1.0f, 1.0f, 0.22f * dt.alpha, false);
                }

                const auto model = makeNonUniformScaleTranslation(dt.x, dt.y, dt.z, dt.sx, dt.sy, dt.sz);
                drawMesh(sphereMesh, model, GL_TRIANGLES, dt.r, dt.g, dt.b, dt.alpha, true, dt.diffusion, dt.highlight);
            }

            glDisable(GL_CULL_FACE);
        }

        static juce::Matrix3D<float> makeNonUniformScaleTranslation(float x, float y, float z, float sx, float sy, float sz) {
            juce::Matrix3D<float> m;
            m.mat[0] = sx; m.mat[5] = sy; m.mat[10] = sz;
            m.mat[12] = x; m.mat[13] = y; m.mat[14] = z;
            return m;
        }

        static juce::Matrix3D<float> makeOrtho(float left, float right, float bottom, float top, float nearZ, float farZ) {
            juce::Matrix3D<float> m;
            std::fill(std::begin(m.mat), std::end(m.mat), 0.0f);
            m.mat[0] = 2.0f / (right - left);
            m.mat[5] = 2.0f / (top - bottom);
            m.mat[10] = -2.0f / (farZ - nearZ);
            m.mat[12] = -(right + left) / (right - left);
            m.mat[13] = -(top + bottom) / (top - bottom);
            m.mat[14] = -(farZ + nearZ) / (farZ - nearZ);
            m.mat[15] = 1.0f;
            return m;
        }

        void drawMesh(const StaticMesh& mesh, const juce::Matrix3D<float>& model,
            GLenum mode, float r, float g, float b, float a, bool useLighting,
            float diffusion = 0.0f, float highlight = 0.0f)
        {
            using namespace juce::gl;
            if (mesh.vao == 0 || mesh.vertexCount == 0) return;

            uModel->setMatrix4(model.mat, 1, GL_FALSE);
            uColor->set(r, g, b, a);
            uUseLighting->set(useLighting ? 1.0f : 0.0f);
            if (uDiffusion != nullptr) uDiffusion->set(diffusion);
            if (uHighlight != nullptr) uHighlight->set(highlight);

            glBindVertexArray(mesh.vao);
            glDrawArrays(mode, 0, mesh.vertexCount);
        }

        static void pushRect(std::vector<Vertex>& v, float halfW, float y0, float y1, float z) {
            auto line = [&](float x0, float yy0, float x1, float yy1) {
                v.push_back({ x0, yy0, z, 0.0f, 0.0f, 1.0f });
                v.push_back({ x1, yy1, z, 0.0f, 0.0f, 1.0f });
                };
            line(-halfW, y0, halfW, y0); line(halfW, y0, halfW, y1);
            line(halfW, y1, -halfW, y1); line(-halfW, y1, -halfW, y0);
        }

        static std::vector<Vertex> buildFrontOuterFrame(float zFrontStart) {
            std::vector<Vertex> v;
            pushRect(v, kFrontHalfW, 0.0f, kFrontHeight, zFrontStart);
            pushRect(v, kFrontHalfW, 0.0f, kFrontHeight, kFrontZBack);
            auto depthEdge = [&](float x, float y) {
                v.push_back({ x, y, kFrontZBack, 0.0f, 0.0f, 1.0f });
                v.push_back({ x, y, zFrontStart, 0.0f, 0.0f, 1.0f });
                };
            depthEdge(-kFrontHalfW, 0.0f); depthEdge(kFrontHalfW, 0.0f);
            depthEdge(-kFrontHalfW, kFrontHeight); depthEdge(kFrontHalfW, kFrontHeight);
            return v;
        }

        static std::vector<Vertex> buildFrontLayerFrames(int layers, int startLayer) {
            std::vector<Vertex> v;
            for (int i = startLayer + 1; i < layers; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(layers);
                const float z = kFrontZFront - t * (kFrontZFront - kFrontZBack);
                pushRect(v, kFrontHalfW, 0.0f, kFrontHeight, z);
            }
            return v;
        }

        static std::vector<Vertex> buildTopCircleFrame() {
            std::vector<Vertex> verts;
            const float outerR = 0.96f;
            const int segs = 80;
            for (int s = 0; s < segs; ++s) {
                const float a0 = (juce::MathConstants<float>::twoPi * s) / segs;
                const float a1 = (juce::MathConstants<float>::twoPi * (s + 1)) / segs;
                verts.push_back({ outerR * std::cos(a0), outerR * std::sin(a0), -0.1f, 0.0f, 0.0f, 1.0f });
                verts.push_back({ outerR * std::cos(a1), outerR * std::sin(a1), -0.1f, 0.0f, 0.0f, 1.0f });
            }
            verts.push_back({ 0.0f, -outerR, -0.1f, 0.0f, 0.0f, 1.0f });
            verts.push_back({ 0.0f,  outerR, -0.1f, 0.0f, 0.0f, 1.0f });
            return verts;
        }

        static std::vector<Vertex> buildDomeLayerSlices(int layers) {
            std::vector<Vertex> verts;
            const int archSegs = 32;
            for (int i = 1; i < layers; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(layers);
                const float z = -kDomeRadius + t * (2.0f * kDomeRadius);
                const float halfSpan = std::sqrt(std::max(0.0f, kDomeRadius * kDomeRadius - z * z));
                if (halfSpan < 0.05f) continue;
                verts.push_back({ -halfSpan, 0.003f, z, 0.0f, 1.0f, 0.0f });
                verts.push_back({ halfSpan, 0.003f, z, 0.0f, 1.0f, 0.0f });
                for (int s = 0; s < archSegs; ++s) {
                    const float a0 = (juce::MathConstants<float>::pi * s) / archSegs;
                    const float a1 = (juce::MathConstants<float>::pi * (s + 1)) / archSegs;
                    verts.push_back({ halfSpan * std::cos(a0), halfSpan * std::sin(a0), z, 0.0f, 1.0f, 0.0f });
                    verts.push_back({ halfSpan * std::cos(a1), halfSpan * std::sin(a1), z, 0.0f, 1.0f, 0.0f });
                }
            }
            return verts;
        }

        static std::vector<Vertex> buildTopCircleLayerLines(int layers) {
            std::vector<Vertex> verts;
            const float outerR = 0.96f;
            for (int i = 1; i < layers; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(layers);
                const float y = -outerR + t * (2.0f * outerR);
                const float halfSpan = std::sqrt(std::max(0.0f, outerR * outerR - y * y));
                verts.push_back({ -halfSpan, y, -0.08f, 0.0f, 0.0f, 1.0f });
                verts.push_back({ halfSpan, y, -0.08f, 0.0f, 0.0f, 1.0f });
            }
            return verts;
        }

        static std::vector<Vertex> buildDomeGeometry() {
            std::vector<Vertex> verts;
            const int rings = 5, segments = 48;
            for (int r = 0; r <= rings; ++r) {
                const float phi = (juce::MathConstants<float>::halfPi * r) / rings;
                const float y = kDomeRadius * std::sin(phi), ringR = kDomeRadius * std::cos(phi);
                for (int s = 0; s < segments; ++s) {
                    const float a0 = (juce::MathConstants<float>::twoPi * s) / segments;
                    const float a1 = (juce::MathConstants<float>::twoPi * (s + 1)) / segments;
                    verts.push_back({ ringR * std::cos(a0), y, ringR * std::sin(a0), 0.0f, 1.0f, 0.0f });
                    verts.push_back({ ringR * std::cos(a1), y, ringR * std::sin(a1), 0.0f, 1.0f, 0.0f });
                }
            }
            for (int m = 0; m < 8; ++m) {
                const float theta = (juce::MathConstants<float>::twoPi * m) / 8;
                for (int r = 0; r < rings; ++r) {
                    const float p0 = (juce::MathConstants<float>::halfPi * r) / rings;
                    const float p1 = (juce::MathConstants<float>::halfPi * (r + 1)) / rings;
                    verts.push_back({ kDomeRadius * std::cos(p0) * std::cos(theta), kDomeRadius * std::sin(p0), kDomeRadius * std::cos(p0) * std::sin(theta), 0.0f, 1.0f, 0.0f });
                    verts.push_back({ kDomeRadius * std::cos(p1) * std::cos(theta), kDomeRadius * std::sin(p1), kDomeRadius * std::cos(p1) * std::sin(theta), 0.0f, 1.0f, 0.0f });
                }
            }
            return verts;
        }

        static std::vector<Vertex> buildAxesGeometry() {
            return {
                { -kDomeRadius, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f },{ kDomeRadius, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, -kDomeRadius, 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f, kDomeRadius, 0.0f, 1.0f, 0.0f }
            };
        }

        static std::vector<Vertex> buildCenterAxisGeometry() {
            return { { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },{ 0.0f, kDomeRadius, 0.0f, 0.0f, 1.0f, 0.0f } };
        }

        static std::vector<Vertex> buildSphereGeometry(float radius, int stacks, int slices) {
            std::vector<Vertex> verts;
            for (int i = 0; i < stacks; ++i) {
                const float phi0 = juce::MathConstants<float>::pi * ((float)i / stacks - 0.5f);
                const float phi1 = juce::MathConstants<float>::pi * ((float)(i + 1) / stacks - 0.5f);
                for (int j = 0; j < slices; ++j) {
                    const float t0 = juce::MathConstants<float>::twoPi * ((float)j / slices);
                    const float t1 = juce::MathConstants<float>::twoPi * ((float)(j + 1)) / slices;
                    auto pt = [radius](float phi, float t) -> Vertex {
                        const float nx = std::cos(phi) * std::cos(t), ny = std::sin(phi), nz = std::cos(phi) * std::sin(t);
                        return { radius * nx, radius * ny, radius * nz, nx, ny, nz };
                        };
                    Vertex p00 = pt(phi0, t0), p10 = pt(phi1, t0), p01 = pt(phi0, t1), p11 = pt(phi1, t1);
                    verts.push_back(p00); verts.push_back(p10); verts.push_back(p11);
                    verts.push_back(p00); verts.push_back(p11); verts.push_back(p01);
                }
            }
            return verts;
        }

        const SharedDomeHub& hub;
        juce::OpenGLContext openGLContext;
        std::unique_ptr<juce::OpenGLShaderProgram> shader, bgShader, borderShader;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uProj, uCamTrans, uCamRot, uModel, uColor, uUseLighting;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uDiffusion, uHighlight, uGrain;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uBgBrightness, uBgGrain, uTopCol, uMidCol, uBotCol;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uBorderSize, uBorderRadius, uBorderStroke, uPaperCol, uInkCol;

        StaticMesh bgQuadMesh, domeMesh, axesMesh, centerAxisMesh, sphereMesh, stemMesh;
        StaticMesh frontFrameMesh, frontLayersMesh, topCircleFrameMesh;
        StaticMesh domeLayerSlicesMesh, topCircleLayersMesh;

        std::array<VisualTrackState, kMaxTracks> trackStates;
        std::array<std::atomic<bool>, kMaxTracks> hiddenFlags;
        std::vector<DrawableTrack> drawList;

        juce::SpinLock projLock;
        std::vector<ProjectedTrack> projectedTracks;
        int hoveredTrackIndex = -1;
        juce::Point<float> hoveredScreenPos;
        juce::Rectangle<int> floorBarBounds;

        float referenceDepth = 0.45f;

        std::atomic<int>      viewModeAtomic{ static_cast<int>(ViewMode::Front) };
        std::atomic<int>      numLayersAtomic{ 3 };
        std::atomic<int>      monitorModeAtomic{ 0 };
        std::atomic<int>      selectedTrackAtomic{ -1 };
        std::atomic<int>      startLayerAtomic{ 0 };
        std::atomic<bool>     bypassedAtomic{ false };
        std::atomic<bool>     layersDirty{ true };
        std::atomic<bool>     resetRefRequested{ false };
        std::atomic<float>    bgBrightnessAtomic{ 0.72f };
        std::atomic<float>    grainAmountAtomic{ 0.14f };
        std::atomic<float>    sphereScaleAtomic{ 1.0f };
        std::atomic<bool>     peakHoldAtomic{ true };
        std::atomic<uint32_t> bgTopAtomic{ 0xFFDF337A };
        std::atomic<uint32_t> bgMidAtomic{ 0xFF8F40A8 };
        std::atomic<uint32_t> bgBotAtomic{ 0xFF0F5294 };
        std::atomic<uint32_t> chromePaperAtomic{ 0xFFF5F4EE };
        std::atomic<uint32_t> chromeInkAtomic{ 0xFF161616 };

        float freeYaw = 0.0f;
        float freePitch = 0.28f;
        float targetDistance = 3.15f;

        float panOffsetX = 0.0f, panOffsetY = 0.0f;
        float currentPanX = 0.0f, currentPanY = 0.0f;

        float currentYaw = 0.0f, currentPitch = 0.0f;
        float currentCamY = -kFrontHeight * 0.5f, currentDist = 2.42f;

        juce::Point<int> lastMousePos;
        bool ctrlDragging = false, ctrlMoved = false;
    };

} // namespace vps