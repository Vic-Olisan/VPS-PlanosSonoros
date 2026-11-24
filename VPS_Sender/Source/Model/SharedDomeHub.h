#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

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
    static constexpr int kSpectrumBands = 16;

    struct TrackSlot {
        std::atomic<int>      active{ 0 };          // 0 = livre, 1 = ocupado
        std::atomic<float>    panX{ 0.0f };       // -1.0 (L) a +1.0 (R)
        std::atomic<float>    freqY{ 0.25f };      //  0.0 (Grave) a 1.0 (Agudo)
        std::atomic<float>    depthZ{ 0.0f };       //  0.0 (Fundo) a 1.0 (Frente)
        std::atomic<float>    stereoWidth{ 0.0f };       //  0.0 (Mono) a 1.0 (Estereo largo)
        std::atomic<float>    diffusion{ 0.0f };       //  0.0 (Seco) a 1.0 (Difuso / Reverb)
        std::atomic<float>    freqHz{ 200.0f };     //  Centroide espectral em Hz
        std::atomic<float>    rmsDb{ -72.0f };     //  Nivel RMS em dB
        std::atomic<uint32_t> colorArgb{ 0xFF38BDF8 }; //  Cor da esfera (ARGB)
        std::atomic<int64_t>  lastHeartbeatMs{ 0 };          //  Timestamp para expirar slots
        std::atomic<float>    spectrum[kSpectrumBands];       //  16 bandas log (0.0 a 1.0)
        char                  name[32];                       //  Nome da pista/grupo
    };

    struct SharedDomeMemory {
        TrackSlot slots[kMaxTracks];
    };

    // Ponte lock-free em RAM compartilhada entre VPS_Sender.vst3 e VPS_Visualizer.vst3
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
                "Local\\VPS_PlanosSonoros_SharedHub_v4"
            );
            if (hMapFile != nullptr) {
                memory = static_cast<SharedDomeMemory*>(
                    MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedDomeMemory))
                    );
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
                0xFF38BDF8, // Ciano
                0xFFFB923C, // Laranja
                0xFFF472B6, // Rosa
                0xFF4ADE80, // Verde
                0xFFFACC15, // Amarelo
                0xFFA78BFA, // Roxo
                0xFFF87171, // Vermelho
                0xFFE2E8F0  // Branco
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
            for (int b = 0; b < kSpectrumBands; ++b)
                s.spectrum[b].store(0.0f, std::memory_order_relaxed);
            s.active.store(1, std::memory_order_release);
        }

#ifdef _WIN32
        HANDLE hMapFile = nullptr;
#endif
        SharedDomeMemory* memory = nullptr;
    };

} // namespace vps