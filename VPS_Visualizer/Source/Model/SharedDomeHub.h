#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vps {

    static constexpr int kMaxTracks = 128;
    static constexpr int kMaxSpectrumBands = 96;

    struct TrackSlot {
        std::atomic<int>      active{ 0 };
        std::atomic<float>    panX{ 0.0f };
        std::atomic<float>    freqY{ 0.25f };
        std::atomic<float>    depthZ{ 0.0f };
        std::atomic<float>    stereoWidth{ 0.0f };
        std::atomic<float>    diffusion{ 0.0f };
        std::atomic<float>    freqHz{ 200.0f };
        std::atomic<float>    rmsDb{ -72.0f };
        std::atomic<uint32_t> colorArgb{ 0xFF38BDF8 };
        std::atomic<int64_t>  lastHeartbeatMs{ 0 };
        std::atomic<int>      spectrumCount{ 48 };
        std::atomic<float>    spectrum[kMaxSpectrumBands];
        char                  name[32];
    };

    struct SharedDomeMemory {
        // 0 = Baixa (16 bandas / FFT 256), 1 = Media (48 bandas / FFT 1024), 2 = Alta (96 bandas / FFT 2048)
        std::atomic<int>      spectrumQuality{ 1 };
        // 0 = Padrao, 1 = Escuro, 2 = Cinza, 3 = Claro, 4 = Warm, 5 = Cold
        std::atomic<int>      themeId{ 0 };
        std::atomic<uint32_t> bgTopColor{ 0xFFDF337A };
        std::atomic<uint32_t> bgMidColor{ 0xFF8F40A8 };
        std::atomic<uint32_t> bgBotColor{ 0xFF0F5294 };
        TrackSlot             slots[kMaxTracks];
    };

    class SharedDomeHub {
    public:
        SharedDomeHub() {
#ifdef _WIN32
            hMapFile = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                nullptr,
                PAGE_READWRITE,
                0,
                static_cast<DWORD>(sizeof(SharedDomeMemory)),
                "Local\\VPS_PlanosSonoros_SharedHub_v5"
            );
            const bool createdNew = (hMapFile != nullptr && GetLastError() != ERROR_ALREADY_EXISTS);
            if (hMapFile != nullptr) {
                memory = static_cast<SharedDomeMemory*>(
                    MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedDomeMemory))
                    );
                if (memory != nullptr && createdNew) {
                    memory->spectrumQuality.store(1, std::memory_order_relaxed);
                    memory->themeId.store(0, std::memory_order_relaxed);
                    memory->bgTopColor.store(0xFFDF337Au, std::memory_order_relaxed);
                    memory->bgMidColor.store(0xFF8F40A8u, std::memory_order_relaxed);
                    memory->bgBotColor.store(0xFF0F5294u, std::memory_order_relaxed);
                }
            }
#endif
        }

        ~SharedDomeHub() {
#ifdef _WIN32
            if (memory != nullptr) {
                UnmapViewOfFile(memory);
                memory = nullptr;
            }
            if (hMapFile != nullptr) {
                CloseHandle(hMapFile);
                hMapFile = nullptr;
            }
#endif
        }

        bool isValid() const noexcept { return memory != nullptr; }

        void setSpectrumQuality(int q) const noexcept {
            if (memory)
                memory->spectrumQuality.store(std::clamp(q, 0, 2), std::memory_order_relaxed);
        }

        int getSpectrumQuality() const noexcept {
            if (!memory) return 1;
            return std::clamp(memory->spectrumQuality.load(std::memory_order_relaxed), 0, 2);
        }

        void setThemeId(int id) const noexcept {
            if (memory)
                memory->themeId.store(std::clamp(id, 0, 5), std::memory_order_relaxed);
        }

        int getThemeId() const noexcept {
            if (!memory) return 0;
            return std::clamp(memory->themeId.load(std::memory_order_relaxed), 0, 5);
        }

        void setGradientColors(uint32_t topArgb, uint32_t midArgb, uint32_t botArgb) const noexcept {
            if (!memory) return;
            memory->bgTopColor.store(topArgb, std::memory_order_relaxed);
            memory->bgMidColor.store(midArgb, std::memory_order_relaxed);
            memory->bgBotColor.store(botArgb, std::memory_order_relaxed);
        }

        void getGradientColors(uint32_t& topArgb, uint32_t& midArgb, uint32_t& botArgb) const noexcept {
            if (!memory) {
                topArgb = 0xFFDF337Au;
                midArgb = 0xFF8F40A8u;
                botArgb = 0xFF0F5294u;
                return;
            }
            const uint32_t t = memory->bgTopColor.load(std::memory_order_relaxed);
            const uint32_t m = memory->bgMidColor.load(std::memory_order_relaxed);
            const uint32_t b = memory->bgBotColor.load(std::memory_order_relaxed);
            topArgb = (t & 0x00FFFFFFu) ? (0xFF000000u | t) : 0xFFDF337Au;
            midArgb = (m & 0x00FFFFFFu) ? (0xFF000000u | m) : 0xFF8F40A8u;
            botArgb = (b & 0x00FFFFFFu) ? (0xFF000000u | b) : 0xFF0F5294u;
        }

        static int bandsForQuality(int q) noexcept {
            if (q <= 0) return 16;
            if (q == 1) return 48;
            return 96;
        }

        int claimSlot(int64_t nowMs) {
            if (!memory) return -1;

            for (int i = 0; i < kMaxTracks; ++i) {
                int expected = 0;
                if (memory->slots[i].active.compare_exchange_strong(expected, 1)) {
                    initSlotDefaults(i, nowMs);
                    return i;
                }
            }

            for (int i = 0; i < kMaxTracks; ++i) {
                const int64_t age = nowMs - memory->slots[i].lastHeartbeatMs.load(std::memory_order_relaxed);
                if (age > 5000) {
                    initSlotDefaults(i, nowMs);
                    return i;
                }
            }
            return -1;
        }

        void releaseSlot(int index) {
            if (!memory || index < 0 || index >= kMaxTracks) return;
            memory->slots[index].active.store(0, std::memory_order_release);
        }

        void setSlotName(int index, const char* newName) {
            if (!memory || index < 0 || index >= kMaxTracks || !newName) return;
            char* dst = memory->slots[index].name;
            int i = 0;
            for (; i < 31 && newName[i] != '\0'; ++i)
                dst[i] = newName[i];
            dst[i] = '\0';
        }

        TrackSlot* getSlot(int index) const noexcept {
            if (!memory || index < 0 || index >= kMaxTracks) return nullptr;
            return &memory->slots[index];
        }

        static uint32_t defaultColorForSlot(int index) {
            static constexpr uint32_t palette[8] = {
                0xFF38BDF8, 0xFFFB923C, 0xFFF472B6, 0xFF4ADE80,
                0xFFFACC15, 0xFFA78BFA, 0xFFF87171, 0xFFE2E8F0
            };
            return palette[index % 8];
        }

    private:
        void initSlotDefaults(int index, int64_t nowMs) {
            auto& s = memory->slots[index];
            std::memset(s.name, 0, sizeof(s.name));
            s.panX.store(0.0f, std::memory_order_relaxed);
            s.freqY.store(0.25f, std::memory_order_relaxed);
            s.depthZ.store(0.0f, std::memory_order_relaxed);
            s.stereoWidth.store(0.0f, std::memory_order_relaxed);
            s.diffusion.store(0.0f, std::memory_order_relaxed);
            s.freqHz.store(200.0f, std::memory_order_relaxed);
            s.rmsDb.store(-72.0f, std::memory_order_relaxed);
            s.colorArgb.store(defaultColorForSlot(index), std::memory_order_relaxed);
            s.lastHeartbeatMs.store(nowMs, std::memory_order_relaxed);
            s.spectrumCount.store(bandsForQuality(getSpectrumQuality()), std::memory_order_relaxed);
            for (int b = 0; b < kMaxSpectrumBands; ++b)
                s.spectrum[b].store(0.0f, std::memory_order_relaxed);
            s.active.store(1, std::memory_order_release);
        }

#ifdef _WIN32
        HANDLE hMapFile = nullptr;
#endif
        SharedDomeMemory* memory = nullptr;
    };

} // namespace vps