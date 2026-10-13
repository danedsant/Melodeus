#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace melodeus::audio {

/// Shifter de Pitch granular con doble retardo rotativo y solapamiento (Overlap-Add).
/// Totalmente RT-safe: memoria preasignada en el constructor, cero allocations en process().
/// Modula el retardo en función del ratio de pitch P = 2^(semitonos / 12.0) manteniendo la
/// duración y velocidad (BPM) del bloque de audio completamente inalteradas.
class PitchShifter {
public:
    static constexpr std::size_t kBufLen = 4096; // Potencia de 2 para envolvente circular rápida
    static constexpr std::size_t kBufMask = kBufLen - 1;
    static constexpr double kWindowSamples = 1200.0; // ~25 ms a 48 kHz (óptimo para transitorios)

    PitchShifter() {
        buf_l_.assign(kBufLen, 0.0f);
        buf_r_.assign(kBufLen, 0.0f);
    }

    void reset() noexcept {
        std::fill(buf_l_.begin(), buf_l_.end(), 0.0f);
        std::fill(buf_r_.begin(), buf_r_.end(), 0.0f);
        write_pos_ = 0;
        phase_ = 0.0;
    }

    /// Ratio de pitch: P = 1.0 (sin cambio), 2.0 (+1 octava), 0.5 (-1 octava).
    /// Si semitonos = S, P = std::pow(2.0, S / 12.0).
    void set_ratio(double ratio) noexcept {
        target_ratio_ = std::clamp(ratio, 0.5, 2.0);
    }

    /// Procesa n muestras de audio estéreo planar in-place. RT-safe.
    void process(float* l, float* r, std::size_t n) noexcept {
        if (l == nullptr || r == nullptr || n == 0) {
            return;
        }

        // Suavizado del ratio hacia el objetivo para evitar zipper noise
        constexpr double kRatioSmooth = 0.005;

        // Bypass transparente si el ratio es 1.0 (ahorro total de CPU)
        if (std::fabs(current_ratio_ - 1.0) < 0.0008 && std::fabs(target_ratio_ - 1.0) < 0.0008) {
            current_ratio_ = 1.0;
            // Solo alimentamos el buffer circular para que un cambio posterior no tenga discontinuidad
            for (std::size_t i = 0; i < n; ++i) {
                buf_l_[write_pos_] = l[i];
                buf_r_[write_pos_] = r[i];
                write_pos_ = (write_pos_ + 1) & kBufMask;
            }
            return;
        }

        constexpr double kPi = 3.14159265358979323846;
        constexpr double kInvWin = 1.0 / kWindowSamples;

        for (std::size_t i = 0; i < n; ++i) {
            current_ratio_ += kRatioSmooth * (target_ratio_ - current_ratio_);

            // Guardar muestra entrante
            buf_l_[write_pos_] = l[i];
            buf_r_[write_pos_] = r[i];

            // Avance de fase del desfase temporal (velocidad de rotación = 1.0 - ratio)
            phase_ += (1.0 - current_ratio_);
            if (phase_ >= kWindowSamples) {
                phase_ -= kWindowSamples;
            } else if (phase_ < 0.0) {
                phase_ += kWindowSamples;
            }

            // Dos cabezales de lectura desfasados por la mitad de la ventana
            const double d1 = phase_;
            double d2 = phase_ + (kWindowSamples * 0.5);
            if (d2 >= kWindowSamples) {
                d2 -= kWindowSamples;
            }

            // Ventana Hann de potencia constante: w1^2 + w2^2 = 1.0
            const double w_angle = d1 * kInvWin * kPi;
            const float w1 = static_cast<float>(std::sin(w_angle));
            const float w2 = static_cast<float>(std::cos(w_angle));

            // Lectura interpolada lineal para cabezal 1
            const double read_pos1 = static_cast<double>(write_pos_) - d1;
            const auto r0_1 = static_cast<std::int64_t>(std::floor(read_pos1));
            const float frac1 = static_cast<float>(read_pos1 - static_cast<double>(r0_1));
            const std::size_t idx0_1 = static_cast<std::size_t>(r0_1 & static_cast<std::int64_t>(kBufMask));
            const std::size_t idx1_1 = (idx0_1 + 1) & kBufMask;
            const float l_samp1 = buf_l_[idx0_1] + frac1 * (buf_l_[idx1_1] - buf_l_[idx0_1]);
            const float r_samp1 = buf_r_[idx0_1] + frac1 * (buf_r_[idx1_1] - buf_r_[idx0_1]);

            // Lectura interpolada lineal para cabezal 2
            const double read_pos2 = static_cast<double>(write_pos_) - d2;
            const auto r0_2 = static_cast<std::int64_t>(std::floor(read_pos2));
            const float frac2 = static_cast<float>(read_pos2 - static_cast<double>(r0_2));
            const std::size_t idx0_2 = static_cast<std::size_t>(r0_2 & static_cast<std::int64_t>(kBufMask));
            const std::size_t idx1_2 = (idx0_2 + 1) & kBufMask;
            const float l_samp2 = buf_l_[idx0_2] + frac2 * (buf_l_[idx1_2] - buf_l_[idx0_2]);
            const float r_samp2 = buf_r_[idx0_2] + frac2 * (buf_r_[idx1_2] - buf_r_[idx0_2]);

            // Mezcla Overlap-Add de ambos cabezales
            l[i] = (l_samp1 * w1 * w1 + l_samp2 * w2 * w2);
            r[i] = (r_samp1 * w1 * w1 + r_samp2 * w2 * w2);

            write_pos_ = (write_pos_ + 1) & kBufMask;
        }
    }

private:
    std::vector<float> buf_l_;
    std::vector<float> buf_r_;
    std::size_t write_pos_ = 0;
    double phase_ = 0.0;
    double target_ratio_ = 1.0;
    double current_ratio_ = 1.0;
};

} // namespace melodeus::audio
