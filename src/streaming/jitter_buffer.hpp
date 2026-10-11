#pragma once

#include "core/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace melodeus::streaming {

/// Jitter buffer de red: ring SPSC de samples f32 interleaved con umbral de
/// arranque (prefill) y contadores de underrun/overflow para diagnóstico.
///
/// Productor = hilo de descarga (push desde ffmpeg). Consumidor = hilo de
/// carga o de audio (read()). El prefill protege el arranque contra jitter
/// de red: hasta que no hay `prefill_frames` disponibles, `ready()` es
/// false y read() devuelve 0 (sin glitch una vez en Playing).
class JitterBuffer {
public:
    /// ~2.7 s de estéreo a 48 kHz (524288 floats); potencia de dos.
    static constexpr std::size_t kCapacity = 1u << 19;
    static constexpr int kDefaultChannels = 2;
    static constexpr int kDefaultPrefillFrames = 48000; // 1 s @ 48 kHz

    explicit JitterBuffer(int prefill_frames = kDefaultPrefillFrames,
                          int channels = kDefaultChannels)
        : prefill_frames_(prefill_frames > 0 ? prefill_frames : 0),
          channels_(channels > 0 ? channels : kDefaultChannels),
          // 2 MB: en heap para no desbordar la pila del llamador.
          ring_(std::make_unique<core::SpscRing<float, kCapacity>>()) {
    }

    /// Productor: empuja `frames` frames interleaved. Devuelve los frames
    /// realmente escritos (si el ring está lleno escribe lo que quepa y
    /// cuenta overflow).
    int push(const float* interleaved, int frames) {
        if (interleaved == nullptr || frames <= 0) {
            return 0;
        }
        const std::size_t n =
            ring_->write(interleaved, static_cast<std::size_t>(frames) * channels_);
        const int wrote = static_cast<int>(n / static_cast<std::size_t>(channels_));
        if (wrote < frames) {
            overflow_.fetch_add(1, std::memory_order_relaxed);
        }
        return wrote;
    }

    /// Consumidor: extrae hasta `frames` frames. 0 si aún no arrancó
    /// (prefill sin cumplir) o si hay underrun de red en pleno replay.
    int read(float* out, int frames) {
        if (out == nullptr || frames <= 0) {
            return 0;
        }
        if (!playing_) {
            const int fill = fill_frames();
            // Arranque: prefill cumplido, o el productor ya terminó y queda
            // algo por drenar (clips cortos que no llegan al umbral).
            if (fill < prefill_frames_ &&
                !(finished_.load(std::memory_order_acquire) && fill > 0)) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
            playing_ = true; // umbral de arranque cumplido una sola vez
        }
        const std::size_t n = ring_->read(out, static_cast<std::size_t>(frames) * channels_);
        if (n == 0) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
        return static_cast<int>(n / static_cast<std::size_t>(channels_));
    }

    /// true cuando ya arrancó (prefill cumplido); antes de eso el consume-
    /// dor debe esperar (devuelve 0 de read()).
    [[nodiscard]] bool ready() const {
        return playing_;
    }

    /// Frames (no samples) disponibles ahora mismo.
    [[nodiscard]] int fill_frames() const {
        return static_cast<int>(ring_->size_approx() / static_cast<std::size_t>(channels_));
    }

    [[nodiscard]] int prefill_frames() const {
        return prefill_frames_;
    }

    [[nodiscard]] int channels() const {
        return channels_;
    }

    [[nodiscard]] std::uint64_t underruns() const {
        return underruns_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t overflows() const {
        return overflow_.load(std::memory_order_relaxed);
    }

    /// El productor ya no empujará más (EOF del stream). Permite drenar
    /// aunque no se haya alcanzado el prefill.
    void finish() {
        finished_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool finished() const {
        return finished_.load(std::memory_order_acquire);
    }

    /// Vacia el ring (cambio de pista). Sólo con productor/consumidor parados.
    void reset() {
        float scratch[4096];
        while (ring_->read(scratch, 4096) > 0) {
        }
        playing_ = false;
        finished_.store(false, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
        overflow_.store(0, std::memory_order_relaxed);
    }

private:
    const int prefill_frames_;
    const int channels_;
    bool playing_ = false; // sólo el consumidor lo toca
    std::atomic<bool> finished_{false};
    std::unique_ptr<core::SpscRing<float, kCapacity>> ring_; // 2 MB en heap
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overflow_{0};
};

} // namespace melodeus::streaming
