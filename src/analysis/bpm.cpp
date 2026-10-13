#include "analysis/bpm.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace melodeus::analysis {

namespace {

constexpr int kHop = 512;
constexpr std::int64_t kMinEnvelopeFrames = 96; // ~1 s a 48 kHz
constexpr double kPi = 3.14159265358979323846;

} // namespace

double detect_bpm(const float* mono, std::int64_t frames, double sample_rate,
                  double* out_offset_sec) {
    if (out_offset_sec != nullptr) {
        *out_offset_sec = 0.0;
    }
    if (mono == nullptr || frames <= 0 || sample_rate <= 0.0) {
        return 0.0;
    }
    const std::int64_t n_env = frames / kHop;
    if (n_env < kMinEnvelopeFrames) {
        return 0.0;
    }

    // Filtro Pasa-Bajos simple (~180 Hz) para aislar el kick drum / 808
    // sin perder los transitorios rítmicos percusivos de banda completa.
    const double alpha = (2.0 * kPi * 180.0 / sample_rate);
    const float a_lp = static_cast<float>(std::clamp(alpha / (1.0 + alpha), 0.001, 0.999));
    float y_lp = 0.0f;

    // Envolvente de onset: energía ponderada (bombo + banda completa)
    // con compresión perceptual logarítmica y media-onda rectificada.
    std::vector<float> env(static_cast<std::size_t>(n_env), 0.0f);
    float prev_energy_comp = 0.0f;
    for (std::int64_t i = 0; i < n_env; ++i) {
        const float* p = mono + i * kHop;
        float e_lp = 0.0f;
        float e_full = 0.0f;
        for (int j = 0; j < kHop; ++j) {
            const float s = p[j];
            y_lp += a_lp * (s - y_lp);
            e_lp += y_lp * y_lp;
            e_full += s * s;
        }
        // 70% graves (kick/808), 30% transitorios percusivos generales
        const float mixed_energy = 0.70f * e_lp + 0.30f * e_full;
        // Compresión perceptual logarítmica para evitar que un solo pico domine
        const float energy_comp = std::log1p(mixed_energy * 20.0f);
        env[static_cast<std::size_t>(i)] = (std::max)(0.0f, energy_comp - prev_energy_comp);
        prev_energy_comp = energy_comp;
    }

    // Autocorrelación normalizada por lag (score en [0,1]).
    const double env_rate = sample_rate / static_cast<double>(kHop);
    const int min_lag = static_cast<int>(env_rate * 60.0 / 185.0);
    const int max_lag = static_cast<int>(env_rate * 60.0 / 68.0);
    if (max_lag <= min_lag + 2 || max_lag >= n_env) {
        return 0.0;
    }

    const auto score_at = [&](int lag) {
        double sum = 0.0;
        double norm_a = 0.0;
        double norm_b = 0.0;
        const std::int64_t n = n_env - lag;
        for (std::int64_t i = 0; i < n; ++i) {
            const double a = env[static_cast<std::size_t>(i)];
            const double b = env[static_cast<std::size_t>(i + lag)];
            sum += a * b;
            norm_a += a * a;
            norm_b += b * b;
        }
        const double denom = std::sqrt(norm_a * norm_b);
        return (denom > 1e-12) ? (sum / denom) : 0.0;
    };

    int best_lag = 0;
    double best_score = 0.0;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        const double score = score_at(lag);
        if (score > best_score) {
            best_score = score;
            best_lag = lag;
        }
    }
    if (best_lag == 0 || best_score < 0.08) {
        return 0.0;
    }

    // Interpolación parabólica sobre el score del lag (refina ~0.1 lag).
    double lag_refined = static_cast<double>(best_lag);
    if (best_lag > min_lag && best_lag < max_lag) {
        const double y0 = score_at(best_lag - 1);
        const double y1 = best_score;
        const double y2 = score_at(best_lag + 1);
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs(denom) > 1e-12) {
            const double delta = 0.5 * (y0 - y2) / denom;
            if (std::abs(delta) <= 1.0) {
                lag_refined += delta;
            }
        }
    }

    double bpm = 60.0 * env_rate / lag_refined;

    // Corrección de octava EDM multi-género:
    // Si cae en la ventana half-time (65–88 BPM, ej. Trap 70-75 BPM o DnB 85-88 BPM)
    // y el armónico superior (doble de velocidad) tiene suficiente correlación (>= 0.46 * best_score),
    // promovemos al tempo real estándar de DJ club (130–176 BPM).
    if (bpm >= 65.0 && bpm <= 88.0 && best_lag / 2 >= min_lag) {
        if (score_at(best_lag / 2) >= 0.46 * best_score) {
            bpm *= 2.0;
            lag_refined *= 0.5;
        }
    } else if (bpm > 185.0 && best_lag * 2 <= max_lag) {
        if (score_at(best_lag * 2) >= 0.75 * best_score) {
            bpm *= 0.5;
            lag_refined *= 2.0;
        }
    }

    // Extracción de fase (downbeat offset en segundos):
    // Correlaciona la envolvente con un tren de impulsos sintético con periodo lag_refined
    // en los primeros 16 beats para encontrar el alineamiento exacto del primer compás.
    if (out_offset_sec != nullptr && bpm > 0.0) {
        const double beat_period_hops = lag_refined;
        const int num_beats = std::clamp<int>(static_cast<int>(n_env / std::max(1.0, beat_period_hops)), 1, 16);
        double best_phase_score = -1.0;
        double best_phase_hops = 0.0;

        for (double phi = 0.0; phi < beat_period_hops; phi += 0.5) {
            double psum = 0.0;
            for (int b = 0; b < num_beats; ++b) {
                const auto h_idx = static_cast<std::size_t>(std::round(phi + static_cast<double>(b) * beat_period_hops));
                if (h_idx < env.size()) {
                    psum += env[h_idx];
                }
            }
            if (psum > best_phase_score) {
                best_phase_score = psum;
                best_phase_hops = phi;
            }
        }
        *out_offset_sec = (best_phase_hops * static_cast<double>(kHop)) / sample_rate;
    }

    return bpm;
}

BpmAnalysis analyze_bpm(const float* mono, std::int64_t frames, double sample_rate) {
    BpmAnalysis res;
    res.bpm = detect_bpm(mono, frames, sample_rate, &res.offset_sec);
    return res;
}

} // namespace melodeus::analysis
