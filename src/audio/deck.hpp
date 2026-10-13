#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "audio/pitch_shifter.hpp"

namespace melodeus::audio {

/// Vista no propietaria de una pista decodificada (interleaved f32).
struct TrackView {
    const float* samples = nullptr;
    std::int64_t frames = 0;
    int channels = 0; // 1 (mono) o 2 (estéreo)
    double sample_rate = 48000.0;
};

/// Deck con transporte, gain, pitch, Master Tempo (Key Lock) y Key Shift.
class Deck {
public:
    void load(const TrackView& v) noexcept {
        track_ = v;
        pos_ = 0.0;
        playing_ = false;
        loop_on_ = false;
        shifter_.reset();
    }

    void clear() noexcept {
        track_ = {};
        pos_ = 0.0;
        playing_ = false;
        loop_on_ = false;
        shifter_.reset();
    }

    void play() noexcept {
        if (track_.samples != nullptr && track_.frames > 0) {
            playing_ = true;
        }
    }

    void stop() noexcept {
        playing_ = false;
    }

    void seek(double seconds) noexcept {
        pos_ = seconds * track_.sample_rate;
        clamp_pos();
        shifter_.reset();
    }

    void set_pitch_percent(double pct) noexcept {
        pitch_pct_ = std::clamp(pct, -100.0, 100.0);
    }

    [[nodiscard]] double pitch_percent() const noexcept {
        return pitch_pct_;
    }

    void set_key_shift(double semitones) noexcept {
        key_shift_ = std::clamp(semitones, -12.0, 12.0);
    }

    [[nodiscard]] double key_shift() const noexcept {
        return key_shift_;
    }

    void set_master_tempo(bool on) noexcept {
        master_tempo_ = on;
    }

    [[nodiscard]] bool master_tempo() const noexcept {
        return master_tempo_;
    }

    /// Scrub: si r != 0, el deck avanza a esa velocidad (1.0 = normal,
    /// 0.125 = 1/8, negativo = atrás) ignorando el pitch. 0 desactiva.
    void set_scrub_rate(double r) noexcept {
        scrub_rate_ = std::clamp(r, -8.0, 8.0);
    }

    [[nodiscard]] bool scrubbing() const noexcept {
        return scrub_rate_ != 0.0;
    }

    /// Loop de reproducción: al alcanzar `start_sec + len_sec` el deck vuelve
    /// a `start_sec` (wrap). len_sec <= 0 desactiva. Se re-aplica desde la UI
    /// en cada callback, así que el clamp contra la pista se hace aquí.
    void set_loop(double start_sec, double len_sec) noexcept {
        if (len_sec <= 0.0 || track_.sample_rate <= 0.0) {
            loop_on_ = false;
            return;
        }
        loop_start_ = start_sec * track_.sample_rate;
        loop_len_ = len_sec * track_.sample_rate;
        if (loop_len_ < 1.0) {
            loop_len_ = 1.0;
        }
        loop_end_ = loop_start_ + loop_len_;
        if (track_.frames > 0) {
            const double max_end = static_cast<double>(track_.frames);
            if (loop_start_ >= max_end) {
                loop_on_ = false;
                return;
            }
            if (loop_end_ > max_end) {
                loop_end_ = max_end;
                loop_len_ = loop_end_ - loop_start_;
                if (loop_len_ < 1.0) {
                    loop_on_ = false;
                    return;
                }
            }
        }
        loop_on_ = true;
    }

    void clear_loop() noexcept {
        loop_on_ = false;
    }

    [[nodiscard]] bool looping() const noexcept {
        return loop_on_;
    }

    void set_gain(double g) noexcept {
        gain_ = g;
    }

    [[nodiscard]] bool playing() const noexcept {
        return playing_;
    }

    [[nodiscard]] bool ended() const noexcept {
        return track_.samples != nullptr && pos_ >= static_cast<double>(track_.frames);
    }

    [[nodiscard]] double position_seconds() const noexcept {
        return (track_.sample_rate > 0.0) ? pos_ / track_.sample_rate : 0.0;
    }

    [[nodiscard]] const TrackView& track() const noexcept {
        return track_;
    }

    [[nodiscard]] double gain() const noexcept {
        return gain_;
    }

    /// Renderiza n frames estéreo planar a la tasa out_rate. RT-safe.
    void render(float* out_l, float* out_r, std::size_t n, double out_rate) noexcept {
        if (out_rate <= 0.0 || !playing_ || track_.samples == nullptr || track_.frames <= 0) {
            zero(out_l, out_r, n, 0);
            return;
        }
        const double mult = (scrub_rate_ != 0.0) ? scrub_rate_ : (1.0 + pitch_pct_ / 100.0);
        const double step = (track_.sample_rate / out_rate) * mult;
        const float g = static_cast<float>(gain_);
        const float* src = track_.samples;
        const std::int64_t frames = track_.frames;
        const int ch = track_.channels;
        for (std::size_t i = 0; i < n; ++i) {
            const double p = pos_;
            const auto i0 = static_cast<std::int64_t>(p);
            if (i0 >= frames) {
                playing_ = false;
                zero(out_l, out_r, n, i);
                return;
            }
            const double frac = p - static_cast<double>(i0);
            const std::int64_t i1 = (i0 + 1 < frames) ? i0 + 1 : i0;
            float l0;
            float r0;
            float l1;
            float r1;
            if (ch == 1) {
                l0 = r0 = src[i0];
                l1 = r1 = src[i1];
            } else {
                l0 = src[i0 * 2];
                r0 = src[i0 * 2 + 1];
                l1 = src[i1 * 2];
                r1 = src[i1 * 2 + 1];
            }
            out_l[i] = static_cast<float>((l0 + (l1 - l0) * frac) * g);
            out_r[i] = static_cast<float>((r0 + (r1 - r0) * frac) * g);
            pos_ = p + step;
            if (loop_on_ && pos_ >= loop_end_) {
                // Wrap al inicio del loop (fmod cubre overshoots grandes).
                pos_ = loop_start_ + std::fmod(pos_ - loop_start_, loop_len_);
            }
        }

        // Post-proceso de Pitch Shifter (Key Shift independiente y Master Tempo)
        const double key_ratio = std::pow(2.0, key_shift_ / 12.0);
        double shift_ratio = key_ratio;
        if (master_tempo_ && mult > 0.0) {
            // Master Tempo compensa la aceleración de velocidad para fijar el tono original
            shift_ratio = key_ratio / mult;
        }
        shifter_.set_ratio(shift_ratio);
        shifter_.process(out_l, out_r, n);
    }

private:
    static void zero(float* l, float* r, std::size_t n, std::size_t from) noexcept {
        for (std::size_t i = from; i < n; ++i) {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
    }

    void clamp_pos() noexcept {
        if (pos_ < 0.0) {
            pos_ = 0.0;
        }
        const double max_pos = static_cast<double>(track_.frames);
        if (pos_ > max_pos) {
            pos_ = max_pos;
        }
    }

    TrackView track_{};
    double pos_ = 0.0;
    double pitch_pct_ = 0.0;
    double scrub_rate_ = 0.0;
    double gain_ = 1.0;
    double loop_start_ = 0.0; // muestras
    double loop_len_ = 0.0;   // muestras
    double loop_end_ = 0.0;   // muestras (start+len, cache)
    PitchShifter shifter_;
    double key_shift_ = 0.0;
    bool master_tempo_ = false;
    bool playing_ = false;
    bool loop_on_ = false;
};

} // namespace melodeus::audio
