#include "analysis/bpm.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace melodeus::analysis {

namespace {

constexpr int kHop = 512;
constexpr std::int64_t kMinEnvelopeFrames = 96; // ~1 s a 48 kHz

} // namespace

double detect_bpm(const float* mono, std::int64_t frames, double sample_rate) {
    if (mono == nullptr || frames <= 0 || sample_rate <= 0.0) {
        return 0.0;
    }
    const std::int64_t n_env = frames / kHop;
    if (n_env < kMinEnvelopeFrames) {
        return 0.0;
    }

    // Envolvente de onset: energía por hop con media-onda rectificada
    // (diferencia positiva frente al hop anterior).
    std::vector<float> env(static_cast<std::size_t>(n_env), 0.0f);
    float prev_energy = 0.0f;
    for (std::int64_t i = 0; i < n_env; ++i) {
        const float* p = mono + i * kHop;
        float energy = 0.0f;
        for (int j = 0; j < kHop; ++j) {
            energy += p[j] * p[j];
        }
        env[static_cast<std::size_t>(i)] = (std::max)(0.0f, energy - prev_energy);
        prev_energy = energy;
    }

    // Autocorrelación normalizada por lag (score en [0,1]).
    const double env_rate = sample_rate / static_cast<double>(kHop);
    const int min_lag = static_cast<int>(env_rate * 60.0 / 180.0);
    const int max_lag = static_cast<int>(env_rate * 60.0 / 70.0);
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
    if (best_lag == 0 || best_score < 0.10) {
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

    // Corrección de octava: si cae por debajo de ~80 y el doble es casi
    // igual de bueno en la autocorrelación, preferir el doble (banda DJ).
    if (bpm < 80.0 && best_lag / 2 >= min_lag) {
        if (score_at(best_lag / 2) >= 0.75 * best_score) {
            bpm *= 2.0;
        }
    } else if (bpm > 165.0 && best_lag * 2 <= max_lag) {
        if (score_at(best_lag * 2) >= 0.75 * best_score) {
            bpm *= 0.5;
        }
    }
    return bpm;
}

} // namespace melodeus::analysis
