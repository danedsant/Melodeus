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
    svf_a_l_.set_sample_rate(config_.sample_rate);
    svf_a_r_.set_sample_rate(config_.sample_rate);
    svf_b_l_.set_sample_rate(config_.sample_rate);
    svf_b_r_.set_sample_rate(config_.sample_rate);
    svf_a_l_.set_q(0.707);
    svf_a_r_.set_q(0.707);
    svf_b_l_.set_q(0.707);
    svf_b_r_.set_q(0.707);
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

    // Filtro DJ: suaviza la posición y mapea a cutoff por bloque.
    // Coeficiente calculado para el tamaño real del bloque (~20 ms).
    const double blk_coef = 1.0 - std::exp(-static_cast<double>(n) / (0.020 * config_.sample_rate));
    sm_filter_a_ += blk_coef * (filter_a_ - sm_filter_a_);
    sm_filter_b_ += blk_coef * (filter_b_ - sm_filter_b_);
    const bool filt_a = std::fabs(sm_filter_a_) > 0.02;
    const bool filt_b = std::fabs(sm_filter_b_) > 0.02;
    if (filt_a) {
        // -1 → LP 300 Hz; +1 → HP 3 kHz (barrido exponencial).
        const double t = (sm_filter_a_ < 0.0) ? -sm_filter_a_ : sm_filter_a_;
        const double fc = (sm_filter_a_ < 0.0) ? 20000.0 * std::pow(300.0 / 20000.0, t)
                                               : 30.0 * std::pow(3000.0 / 30.0, t);
        svf_a_l_.set_cutoff(fc);
        svf_a_r_.set_cutoff(fc);
    }
    if (filt_b) {
        const double t = (sm_filter_b_ < 0.0) ? -sm_filter_b_ : sm_filter_b_;
        const double fc = (sm_filter_b_ < 0.0) ? 20000.0 * std::pow(300.0 / 20000.0, t)
                                               : 30.0 * std::pow(3000.0 / 30.0, t);
        svf_b_l_.set_cutoff(fc);
        svf_b_r_.set_cutoff(fc);
    }

    for (std::size_t i = 0; i < n; ++i) {
        double la = eq_a_.l.process(scratch_a_l_[i]);
        double ra = eq_a_.r.process(scratch_a_r_[i]);
        double lb = eq_b_.l.process(scratch_b_l_[i]);
        double rb = eq_b_.r.process(scratch_b_r_[i]);
        if (filt_a) {
            la = (sm_filter_a_ < 0.0) ? svf_a_l_.process(la).lp : svf_a_l_.process(la).hp;
            ra = (sm_filter_a_ < 0.0) ? svf_a_r_.process(ra).lp : svf_a_r_.process(ra).hp;
        }
        if (filt_b) {
            lb = (sm_filter_b_ < 0.0) ? svf_b_l_.process(lb).lp : svf_b_l_.process(lb).hp;
            rb = (sm_filter_b_ < 0.0) ? svf_b_r_.process(rb).lp : svf_b_r_.process(rb).hp;
        }
        scratch_a_l_[i] = static_cast<float>(la);
        scratch_a_r_[i] = static_cast<float>(ra);
        scratch_b_l_[i] = static_cast<float>(lb);
        scratch_b_r_[i] = static_cast<float>(rb);
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
        float ml = static_cast<float>(la * ga + lb * gb);
        float mr = static_cast<float>(ra * ga + rb * gb);

        // Saturador suave (Master Soft Clipper): respuesta C1 transparente
        // en [-1.0, 1.0] con saturación hiperbólica suave en picos > 0 dBFS.
        if (ml > 1.0f) {
            ml = 1.0f + std::tanh(ml - 1.0f);
        } else if (ml < -1.0f) {
            ml = -1.0f + std::tanh(ml + 1.0f);
        }
        if (mr > 1.0f) {
            mr = 1.0f + std::tanh(mr - 1.0f);
        } else if (mr < -1.0f) {
            mr = -1.0f + std::tanh(mr + 1.0f);
        }

        out_l[i] = ml;
        out_r[i] = mr;
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
