#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <algorithm>
#include "SharedDomeHub.h"

namespace vps {

    // Pipeline ultraleve para 50+ canais:
    // - Aplica Ganho e Pan do Sender no audio (suavizados, sem cliques)
    // - Pan perceptual em dB, Mid/Side (largura/reverb) e Silence Bypass (-74 dB)
    // - FFT 256 sob demanda (~10 Hz) para 16 bandas log do mini espectro
    class AudioPipeline {
    public:
        static constexpr int fftOrder = 8;          // 256 amostras (muito leve)
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hopInterval = 4096;       // ~93 ms a 44.1 kHz

        AudioPipeline()
            : forwardFFT(fftOrder),
            window(fftSize, juce::dsp::WindowingFunction<float>::hann)
        {
            fifo.fill(0.0f);
            fftData.fill(0.0f);
            smoothedBands.fill(0.0f);
        }

        void prepare(double newSampleRate) {
            sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

            const float twoPiOverSr = juce::MathConstants<float>::twoPi / static_cast<float>(sampleRate);
            c1 = 1.0f - std::exp(-180.0f * twoPiOverSr);
            c2 = 1.0f - std::exp(-900.0f * twoPiOverSr);
            c3 = 1.0f - std::exp(-3500.0f * twoPiOverSr);

            lp1 = lp2 = lp3 = 0.0f;
            fifoIndex = 0;
            samplesSinceLastFFT = 0;

            smoothedGainL.reset(sampleRate, 0.02);
            smoothedGainR.reset(sampleRate, 0.02);
            smoothedGainL.setCurrentAndTargetValue(1.0f);
            smoothedGainR.setCurrentAndTargetValue(1.0f);

            heartbeatIntervalSamples = std::max(1, static_cast<int>(sampleRate * 0.10));
            samplesSinceHeartbeat = heartbeatIntervalSamples;
        }

        void processBlock(juce::AudioBuffer<float>& buffer, TrackSlot* slot, float userGainDb, float userPan) {
            const int numSamples = buffer.getNumSamples();
            const int numChannels = buffer.getNumChannels();
            if (numSamples <= 0 || numChannels <= 0)
                return;

            // 0. Aplica Ganho e Pan do proprio Sender no audio antes da analise
            const float linGain = (userGainDb <= -59.5f) ? 0.0f : juce::Decibels::decibelsToGain(userGainDb);
            const float panClamped = std::clamp(userPan, -1.0f, 1.0f);
            const float panL = std::min(1.0f, 1.0f - panClamped);
            const float panR = std::min(1.0f, 1.0f + panClamped);

            smoothedGainL.setTargetValue(linGain * panL);
            smoothedGainR.setTargetValue(linGain * panR);

            float* leftW = buffer.getWritePointer(0);
            float* rightW = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;

            if (rightW != nullptr) {
                for (int i = 0; i < numSamples; ++i) {
                    leftW[i] *= smoothedGainL.getNextValue();
                    rightW[i] *= smoothedGainR.getNextValue();
                }
            }
            else {
                for (int i = 0; i < numSamples; ++i) {
                    leftW[i] *= smoothedGainL.getNextValue();
                    smoothedGainR.getNextValue();
                }
            }

            if (slot == nullptr)
                return;

            const float* left = buffer.getReadPointer(0);
            const float* right = (numChannels > 1) ? buffer.getReadPointer(1) : left;

            float sumL2 = 0.0f, sumR2 = 0.0f;
            float sumMid2 = 0.0f, sumSide2 = 0.0f;
            float peak = 0.0f;

            if (numChannels > 1) {
                for (int i = 0; i < numSamples; ++i) {
                    const float l = left[i];
                    const float r = right[i];
                    const float m = 0.5f * (l + r);
                    const float s = 0.5f * (l - r);

                    sumL2 += l * l;
                    sumR2 += r * r;
                    sumMid2 += m * m;
                    sumSide2 += s * s;

                    const float a = std::max(std::abs(l), std::abs(r));
                    if (a > peak) peak = a;
                }
            }
            else {
                for (int i = 0; i < numSamples; ++i) {
                    const float m = left[i];
                    sumL2 += m * m;
                    const float a = std::abs(m);
                    if (a > peak) peak = a;
                }
                sumR2 = sumL2;
                sumMid2 = sumL2;
            }

            const float invN = 1.0f / static_cast<float>(numSamples);
            const float dt = static_cast<float>(numSamples / sampleRate);

            // Silence Bypass (-74 dB)
            if (peak < 0.0002f) {
                const float relAlpha = 1.0f - std::exp(-dt / releaseSec);
                smoothedDepth += relAlpha * (0.0f - smoothedDepth);
                smoothedWidth += (1.0f - std::exp(-dt / 0.35f)) * (0.0f - smoothedWidth);
                smoothedDiff += (1.0f - std::exp(-dt / 0.55f)) * (0.0f - smoothedDiff);

                slot->depthZ.store(smoothedDepth, std::memory_order_relaxed);
                slot->stereoWidth.store(smoothedWidth, std::memory_order_relaxed);
                slot->diffusion.store(smoothedDiff, std::memory_order_relaxed);
                slot->rmsDb.store(-72.0f, std::memory_order_relaxed);

                for (int b = 0; b < kSpectrumBands; ++b) {
                    smoothedBands[static_cast<size_t>(b)] *= 0.82f;
                    slot->spectrum[b].store(smoothedBands[static_cast<size_t>(b)], std::memory_order_relaxed);
                }

                fifoIndex = 0;
                samplesSinceLastFFT = 0;
                maybeUpdateHeartbeat(slot, numSamples);
                return;
            }

            const float rmsL = std::sqrt(sumL2 * invN);
            const float rmsR = std::sqrt(sumR2 * invN);
            const float rmsMid = std::sqrt(sumMid2 * invN);
            const float rmsSide = std::sqrt(sumSide2 * invN);
            const float monoRms = 0.5f * (rmsL + rmsR);

            // 1. PAN perceptual em dB
            const float lrSum = rmsL + rmsR;
            if (lrSum > 0.0015f) {
                const float panDb = juce::Decibels::gainToDecibels((rmsR + 1e-6f) / (rmsL + 1e-6f));
                const float targetPan = std::clamp(panDb / 12.0f, -1.0f, 1.0f);
                const float panAlpha = 1.0f - std::exp(-dt / panSmoothSec);
                smoothedPan += panAlpha * (targetPan - smoothedPan);
            }
            smoothedPan = std::clamp(smoothedPan, -1.0f, 1.0f);

            // 2. Ganho / Profundidade (eixo Z)
            const float db = juce::Decibels::gainToDecibels(monoRms, -72.0f);
            const float targetDepth = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
            const float depthTau = (targetDepth > smoothedDepth) ? attackSec : releaseSec;
            smoothedDepth += (1.0f - std::exp(-dt / depthTau)) * (targetDepth - smoothedDepth);

            // 3. Largura Estereo e Difusao
            const float msSum = rmsMid + rmsSide;
            const float rawSideRatio = (msSum > 1e-6f) ? (rmsSide / msSum) : 0.0f;
            const float targetWidth = std::clamp(rawSideRatio * 2.15f, 0.0f, 1.0f);

            const float crest = peak / (monoRms + 1e-6f);
            const float sustainFactor = 1.0f - std::clamp((crest - 1.5f) / 3.8f, 0.0f, 1.0f);
            const float targetDiff = std::clamp(rawSideRatio * 1.40f + (rawSideRatio > 0.08f ? sustainFactor * 0.35f : 0.0f), 0.0f, 1.0f);

            const float widthTau = (targetWidth > smoothedWidth) ? 0.07f : 0.30f;
            const float diffTau = (targetDiff > smoothedDiff) ? 0.14f : 0.55f;
            smoothedWidth += (1.0f - std::exp(-dt / widthTau)) * (targetWidth - smoothedWidth);
            smoothedDiff += (1.0f - std::exp(-dt / diffTau)) * (targetDiff - smoothedDiff);

            // 4. Centroide Espectral ultraleve via 4 bandas IIR
            float e0 = 0.0f, e1 = 0.0f, e2 = 0.0f, e3 = 0.0f;
            for (int i = 0; i < numSamples; ++i) {
                const float x = (numChannels > 1) ? 0.5f * (left[i] + right[i]) : left[i];
                lp1 += c1 * (x - lp1);
                lp2 += c2 * (x - lp2);
                lp3 += c3 * (x - lp3);

                const float b0 = lp1;
                const float b1 = lp2 - lp1;
                const float b2 = lp3 - lp2;
                const float b3 = x - lp3;

                e0 += b0 * b0;
                e1 += b1 * b1;
                e2 += b2 * b2;
                e3 += b3 * b3;
            }

            const float totalE = e0 + e1 + e2 + e3;
            if (totalE > 1e-9f) {
                constexpr float l0 = 6.49185f;
                constexpr float l1 = 8.71425f;
                constexpr float l2 = 10.81378f;
                constexpr float l3 = 12.73132f;
                constexpr float logMin = 5.64386f;
                constexpr float logMax = 13.55075f;

                const float avgLog = (e0 * l0 + e1 * l1 + e2 * l2 + e3 * l3) / totalE;
                const float hz = std::clamp(std::pow(2.0f, avgLog), 50.0f, 12000.0f);
                const float targetY = std::clamp((avgLog - logMin) / (logMax - logMin), 0.0f, 1.0f);

                const float freqAlpha = 1.0f - std::exp(-dt / 0.14f);
                smoothedHz += freqAlpha * (hz - smoothedHz);
                smoothedY += freqAlpha * (targetY - smoothedY);
            }

            // 5. Espectro de 16 bandas (FFT 256 executada apenas a cada ~93 ms)
            samplesSinceLastFFT += numSamples;
            if (samplesSinceLastFFT >= hopInterval - fftSize) {
                for (int i = 0; i < numSamples; ++i) {
                    fifo[static_cast<size_t>(fifoIndex++)] = (numChannels > 1) ? 0.5f * (left[i] + right[i]) : left[i];
                    if (fifoIndex == fftSize) {
                        fifoIndex = 0;
                        samplesSinceLastFFT = 0;
                        computeSpectrumBands(slot);
                        break;
                    }
                }
            }

            slot->panX.store(smoothedPan, std::memory_order_relaxed);
            slot->depthZ.store(smoothedDepth, std::memory_order_relaxed);
            slot->stereoWidth.store(smoothedWidth, std::memory_order_relaxed);
            slot->diffusion.store(smoothedDiff, std::memory_order_relaxed);
            slot->freqY.store(smoothedY, std::memory_order_relaxed);
            slot->freqHz.store(smoothedHz, std::memory_order_relaxed);
            slot->rmsDb.store(db, std::memory_order_relaxed);

            maybeUpdateHeartbeat(slot, numSamples);
        }

    private:
        void computeSpectrumBands(TrackSlot* slot) {
            std::copy(fifo.begin(), fifo.end(), fftData.begin());
            std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);

            window.multiplyWithWindowingTable(fftData.data(), fftSize);
            forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

            const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
            constexpr float fMin = 40.0f;
            constexpr float fMax = 16000.0f;
            const float ratio = std::pow(fMax / fMin, 1.0f / static_cast<float>(kSpectrumBands));

            for (int b = 0; b < kSpectrumBands; ++b) {
                const float f0 = fMin * std::pow(ratio, static_cast<float>(b));
                const float f1 = f0 * ratio;

                int bin0 = std::clamp(static_cast<int>(std::floor(f0 / binHz)), 1, fftSize / 2 - 1);
                int bin1 = std::clamp(static_cast<int>(std::ceil(f1 / binHz)), bin0, fftSize / 2 - 1);

                float maxMag = 0.0f;
                for (int k = bin0; k <= bin1; ++k) {
                    const float m = fftData[static_cast<size_t>(k)] / static_cast<float>(fftSize * 0.5f);
                    if (m > maxMag) maxMag = m;
                }

                const float bandDb = juce::Decibels::gainToDecibels(maxMag, -72.0f);
                const float norm = std::clamp((bandDb + 72.0f) / 66.0f, 0.0f, 1.0f);

                float& sm = smoothedBands[static_cast<size_t>(b)];
                sm += (norm - sm) * ((norm > sm) ? 0.55f : 0.25f);
                slot->spectrum[b].store(sm, std::memory_order_relaxed);
            }
        }

        void maybeUpdateHeartbeat(TrackSlot* slot, int numSamples) {
            samplesSinceHeartbeat += numSamples;
            if (samplesSinceHeartbeat >= heartbeatIntervalSamples) {
                samplesSinceHeartbeat = 0;
                slot->lastHeartbeatMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
            }
        }

        double sampleRate = 44100.0;
        float c1 = 0.025f, c2 = 0.12f, c3 = 0.39f;
        float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;

        juce::SmoothedValue<float> smoothedGainL, smoothedGainR;

        juce::dsp::FFT forwardFFT;
        juce::dsp::WindowingFunction<float> window;
        std::array<float, fftSize> fifo;
        std::array<float, fftSize * 2> fftData;
        std::array<float, kSpectrumBands> smoothedBands;
        int fifoIndex = 0;
        int samplesSinceLastFFT = 0;

        float smoothedPan = 0.0f;
        float smoothedDepth = 0.0f;
        float smoothedWidth = 0.0f;
        float smoothedDiff = 0.0f;
        float smoothedHz = 200.0f;
        float smoothedY = 0.25f;

        int heartbeatIntervalSamples = 4410;
        int samplesSinceHeartbeat = 4410;

        static constexpr float panSmoothSec = 0.045f;
        static constexpr float attackSec = 0.030f;
        static constexpr float releaseSec = 0.550f;
    };

} // namespace vps