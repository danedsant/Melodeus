#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include "audio/deck.hpp"
#include "audio/mixer.hpp"

namespace melodeus::audio {

struct EngineConfig {
    double sample_rate = 48000.0;
    std::size_t max_block = 4096;
};

/// Motor de mezcla F1: 2 decks + crossfader + master. RT-safe en process():
/// scratch pre-asignados, sin locks y sin allocs (verificado con rt_guard).
class Engine {
public:
    explicit Engine(const EngineConfig& config);

    Deck& deck_a() noexcept {
        return a_;
    }
    Deck& deck_b() noexcept {
        return b_;
    }

    void set_crossfader(double x) noexcept;
    void set_curve(CrossfadeCurve c) noexcept {
        curve_ = c;
    }
    void set_master_gain(double g) noexcept {
        master_gain_ = g;
    }

    /// Mezcla n ≤ max_block frames estéreo planar. RT-safe.
    void process(float* out_l, float* out_r, std::size_t n) noexcept;

    [[nodiscard]] float peak_a() const noexcept {
        return peak_a_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float peak_b() const noexcept {
        return peak_b_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] float peak_master() const noexcept {
        return peak_master_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const EngineConfig& config() const noexcept {
        return config_;
    }

private:
    EngineConfig config_;
    Deck a_;
    Deck b_;
    CrossfadeCurve curve_ = CrossfadeCurve::ConstantPower;
    double x_ = 0.0;
    double master_gain_ = 1.0;
    std::vector<float> scratch_a_l_;
    std::vector<float> scratch_a_r_;
    std::vector<float> scratch_b_l_;
    std::vector<float> scratch_b_r_;
    std::atomic<float> peak_a_{0.0f};
    std::atomic<float> peak_b_{0.0f};
    std::atomic<float> peak_master_{0.0f};
};

} // namespace melodeus::audio
