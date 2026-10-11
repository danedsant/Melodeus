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
    eq_a_.set_sample_rate(config_.sample_rate);
    eq_b_.set_sample_rate(config_.sample_rate);
    limiter_.setup(config_.sample_rate, 2.0 /*lookahead ms*/, 100.0 /*release ms*/,
                   -1.0 /*techo dBFS*/);
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
    for (std::size_t i = 0; i < n; ++i) {
        scratch_a_l_[i] = static_cast<float>(eq_a_.l.process(scratch_a_l_[i]));
        scratch_a_r_[i] = static_cast<float>(eq_a_.r.process(scratch_a_r_[i]));
        scratch_b_l_[i] = static_cast<float>(eq_b_.l.process(scratch_b_l_[i]));
        scratch_b_r_[i] = static_cast<float>(eq_b_.r.process(scratch_b_r_[i]));
    }
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
        out_l[i] = static_cast<float>(la * ga + lb * gb);
        out_r[i] = static_cast<float>(ra * ga + rb * gb);
        pa = std::max(pa, std::max(std::fabs(la), std::fabs(ra)));
        pb = std::max(pb, std::max(std::fabs(lb), std::fabs(rb)));
    }
    limiter_.process(out_l, out_r, out_l, out_r, n);
    for (std::size_t i = 0; i < n; ++i) {
        pm = std::max(pm, std::max(std::fabs(out_l[i]), std::fabs(out_r[i])));
    }
    master_meter_.process_block(out_l, out_r, n, config_.sample_rate);
    peak_a_.store(pa, std::memory_order_relaxed);
    peak_b_.store(pb, std::memory_order_relaxed);
    peak_master_.store(pm, std::memory_order_relaxed);
}

} // namespace melodeus::audio
