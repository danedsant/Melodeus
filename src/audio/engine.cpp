#include "audio/engine.hpp"

#include <algorithm>
#include <cmath>

#include "core/rt_guard.hpp"

namespace melodeus::audio {

Engine::Engine(const EngineConfig& config) : config_(config) {
    if (config_.sample_rate <= 0.0) {
        config_.sample_rate = 48000.0;
    }
    if (config_.max_block == 0) {
        config_.max_block = 4096;
    }
    scratch_a_l_.assign(config_.max_block, 0.0f);
    scratch_a_r_.assign(config_.max_block, 0.0f);
    scratch_b_l_.assign(config_.max_block, 0.0f);
    scratch_b_r_.assign(config_.max_block, 0.0f);
}

void Engine::set_crossfader(double x) noexcept {
    x_ = std::clamp(x, -1.0, 1.0);
}

void Engine::process(float* out_l, float* out_r, std::size_t n) noexcept {
    core::rt::RtContext rt; // cualquier alloc dentro dispara la violación RT
    if (n > config_.max_block) {
        n = config_.max_block;
    }
    a_.render(scratch_a_l_.data(), scratch_a_r_.data(), n, config_.sample_rate);
    b_.render(scratch_b_l_.data(), scratch_b_r_.data(), n, config_.sample_rate);
    double ga = 0.0;
    double gb = 0.0;
    crossfade_gains(curve_, x_, &ga, &gb);
    ga *= master_gain_;
    gb *= master_gain_;
    float pa = 0.0f;
    float pb = 0.0f;
    float pm = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float la = scratch_a_l_[i];
        const float ra = scratch_a_r_[i];
        const float lb = scratch_b_l_[i];
        const float rb = scratch_b_r_[i];
        const float l = static_cast<float>(la * ga + lb * gb);
        const float r = static_cast<float>(ra * ga + rb * gb);
        out_l[i] = l;
        out_r[i] = r;
        pa = std::max(pa, std::max(std::fabs(la), std::fabs(ra)));
        pb = std::max(pb, std::max(std::fabs(lb), std::fabs(rb)));
        pm = std::max(pm, std::max(std::fabs(l), std::fabs(r)));
    }
    peak_a_.store(pa, std::memory_order_relaxed);
    peak_b_.store(pb, std::memory_order_relaxed);
    peak_master_.store(pm, std::memory_order_relaxed);
}

} // namespace melodeus::audio
