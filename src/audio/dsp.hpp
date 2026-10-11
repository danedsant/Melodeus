#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace melodeus::audio {

/// SVF TPT (Zavalishin): un estado por canal; cutoff/Q a tasa de bloque.
/// RT-safe salvo los setters (llamarlos desde el hilo de control).
class TptSvf {
public:
    TptSvf() {
        update_coefficients();
    }

    void set_sample_rate(double fs) noexcept {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        fc_ = std::clamp(fc_, 5.0, 0.45 * fs_);
        update_coefficients();
    }

    void set_cutoff(double hz) noexcept {
        fc_ = std::clamp(hz, 5.0, 0.45 * fs_);
        update_coefficients();
    }

    void set_q(double q) noexcept {
        k_ = 1.0 / std::clamp(q, 0.1, 100.0);
        update_coefficients();
    }

    void reset() noexcept {
        ic1_ = 0.0;
        ic2_ = 0.0;
    }

    struct Outputs {
        double lp;
        double bp;
        double hp;
    };

    Outputs process(double x) noexcept {
        const double v3 = x - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return {v2, v1, v3 - k_ * v1};
    }

private:
    void update_coefficients() noexcept {
        constexpr double kPi = 3.14159265358979323846;
        const double g = std::tan(kPi * fc_ / fs_);
        a1_ = 1.0 / (1.0 + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    double fs_ = 48000.0;
    double fc_ = 1000.0;
    double k_ = 1.0; // 1/Q
    double a1_ = 0.0;
    double a2_ = 0.0;
    double a3_ = 0.0;
    double ic1_ = 0.0;
    double ic2_ = 0.0;
};

inline double db_to_linear(double db) noexcept {
    constexpr double kFloorDb = -60.0;
    return std::pow(10.0, std::max(db, kFloorDb) / 20.0);
}

/// EQ de 3 bandas (tipo DJ): split LP 250 Hz + HP 4 kHz, banda media = residual.
/// Ganancias en dB con suavizado one-pole (10 ms) anti-zipper; kill = -60 dB.
class ThreeBandEq {
public:
    void set_sample_rate(double fs) noexcept {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        lp_.set_sample_rate(fs_);
        hp_.set_sample_rate(fs_);
        lp_.set_cutoff(250.0);
        lp_.set_q(0.70710678);
        hp_.set_cutoff(4000.0);
        hp_.set_q(0.70710678);
        gain_coef_ = 1.0 - std::exp(-1.0 / (0.010 * fs_));
    }

    void set_low_db(double db) noexcept {
        target_low_ = db_to_linear(db);
    }

    void set_mid_db(double db) noexcept {
        target_mid_ = db_to_linear(db);
    }

    void set_high_db(double db) noexcept {
        target_high_ = db_to_linear(db);
    }

    void set_bands(double low_db, double mid_db, double high_db) noexcept {
        set_low_db(low_db);
        set_mid_db(mid_db);
        set_high_db(high_db);
    }

    void reset() noexcept {
        lp_.reset();
        hp_.reset();
        gl_ = target_low_;
        gm_ = target_mid_;
        gh_ = target_high_;
    }

    double process(double x) noexcept {
        const TptSvf::Outputs lo = lp_.process(x);
        const TptSvf::Outputs hi = hp_.process(x);
        const double mid = x - lo.lp - hi.hp;
        gl_ += gain_coef_ * (target_low_ - gl_);
        gm_ += gain_coef_ * (target_mid_ - gm_);
        gh_ += gain_coef_ * (target_high_ - gh_);
        return lo.lp * gl_ + mid * gm_ + hi.hp * gh_;
    }

private:
    TptSvf lp_;
    TptSvf hp_;
    double fs_ = 48000.0;
    double gain_coef_ = 0.0;
    double target_low_ = 1.0;
    double target_mid_ = 1.0;
    double target_high_ = 1.0;
    double gl_ = 1.0;
    double gm_ = 1.0;
    double gh_ = 1.0;
};

/// EQ estéreo: un ThreeBandEq por canal con ganancias espejadas.
struct StereoEq {
    ThreeBandEq l;
    ThreeBandEq r;

    void set_sample_rate(double fs) noexcept {
        l.set_sample_rate(fs);
        r.set_sample_rate(fs);
    }

    void set_bands(double low_db, double mid_db, double high_db) noexcept {
        l.set_bands(low_db, mid_db, high_db);
        r.set_bands(low_db, mid_db, high_db);
    }

    void reset() noexcept {
        l.reset();
        r.reset();
    }
};

/// Limiter estéreo con lookahead (ataque cubierto por la demora) y release
/// one-pole. Brickwall exacto: la ganancia sigue al mínimo deslizante de
/// g_req sobre la ventana del lookahead (deque monótono preasignado), más
/// release hacia ese mínimo; así out[n] = in[n−L]·gain[n] ≤ techo.
/// RT-safe en process; setup() aloca (hilo de control).
class LookaheadLimiter {
public:
    LookaheadLimiter() {
        setup(48000.0, 2.0, 100.0, -1.0);
    }

    void setup(double sample_rate, double lookahead_ms, double release_ms,
               double ceiling_db) noexcept {
        fs_ = (sample_rate > 0.0) ? sample_rate : 48000.0;
        delay_ = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::ceil(lookahead_ms * fs_ / 1000.0)));
        const double release_s = std::max(0.001, release_ms / 1000.0);
        release_coef_ = 1.0 - std::exp(-1.0 / (release_s * fs_));
        buf_l_.assign(delay_, 0.0f);
        buf_r_.assign(delay_, 0.0f);
        g_hist_.assign(delay_ + 1, 1.0);
        dq_.assign(delay_ + 1, 0);
        dq_head_ = 0;
        dq_tail_ = 0;
        sample_index_ = 0;
        write_ = 0;
        gain_ = 1.0;
        set_ceiling_db(ceiling_db);
    }

    void set_ceiling_db(double db) noexcept {
        ceiling_ = std::pow(10.0, std::clamp(db, -24.0, 0.0) / 20.0);
    }

    void reset() noexcept {
        std::fill(buf_l_.begin(), buf_l_.end(), 0.0f);
        std::fill(buf_r_.begin(), buf_r_.end(), 0.0f);
        std::fill(g_hist_.begin(), g_hist_.end(), 1.0);
        dq_head_ = 0;
        dq_tail_ = 0;
        sample_index_ = 0;
        write_ = 0;
        gain_ = 1.0;
    }

    /// Procesa n frames in-place (o con búferes distintos). RT-safe.
    void process(const float* in_l, const float* in_r, float* out_l, float* out_r,
                 std::size_t n) noexcept {
        const std::size_t dly = delay_;
        const std::size_t hist = g_hist_.size(); // delay_ + 1
        for (std::size_t i = 0; i < n; ++i) {
            const float il = in_l[i];
            const float ir = in_r[i];
            const double peak =
                std::max(std::fabs(static_cast<double>(il)), std::fabs(static_cast<double>(ir)));
            const double g_req = (peak > ceiling_) ? (ceiling_ / peak) : 1.0;

            // Mínimo deslizante de g_req sobre [n − delay_, n].
            // Expirar ANTES de pushear: así el deque nunca supera delay_+1
            // entradas y no pisa el frente.
            g_hist_[sample_index_ % hist] = g_req;
            const std::size_t window_start = (sample_index_ >= dly) ? sample_index_ - dly : 0;
            while (dq_tail_ > dq_head_ && dq_[dq_head_ % hist] < window_start) {
                ++dq_head_;
            }
            while (dq_tail_ > dq_head_ && g_hist_[dq_[(dq_tail_ - 1) % hist] % hist] >= g_req) {
                --dq_tail_;
            }
            dq_[dq_tail_ % hist] = sample_index_;
            ++dq_tail_;
            const double wmin = g_hist_[dq_[dq_head_ % hist] % hist];

            if (wmin < gain_) {
                gain_ = wmin; // ataque instantáneo; el lookahead lo anticipa
            } else {
                gain_ += (wmin - gain_) * release_coef_;
            }

            const float dl = buf_l_[write_];
            const float dr = buf_r_[write_];
            buf_l_[write_] = il;
            buf_r_[write_] = ir;
            write_ = (write_ + 1 == dly) ? 0 : write_ + 1;
            ++sample_index_;
            out_l[i] = static_cast<float>(dl * gain_);
            out_r[i] = static_cast<float>(dr * gain_);
        }
    }

    [[nodiscard]] double gain() const noexcept {
        return gain_;
    }

    [[nodiscard]] double ceiling() const noexcept {
        return ceiling_;
    }

    [[nodiscard]] std::size_t delay_samples() const noexcept {
        return delay_;
    }

private:
    std::vector<float> buf_l_;
    std::vector<float> buf_r_;
    std::vector<double> g_hist_;  // anillo de g_req (delay_+1)
    std::vector<std::size_t> dq_; // deque monótono de índices (delay_+1)
    std::size_t dq_head_ = 0;
    std::size_t dq_tail_ = 0;
    std::size_t sample_index_ = 0;
    std::size_t delay_ = 1;
    std::size_t write_ = 0;
    double fs_ = 48000.0;
    double ceiling_ = 0.891251; // -1 dBFS
    double release_coef_ = 0.0;
    double gain_ = 1.0;
};

/// Medidor de pico con caída exponencial (envolvente, τ = 0.4 s).
/// process_block desde el hilo de audio; load() desde la UI.
class PeakMeter {
public:
    void process_block(const float* l, const float* r, std::size_t n, double sample_rate) noexcept {
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            peak = std::max(peak, std::fabs(l[i]));
            peak = std::max(peak, std::fabs(r[i]));
        }
        const double fs = (sample_rate > 0.0) ? sample_rate : 48000.0;
        const float decay = static_cast<float>(std::exp(-static_cast<double>(n) / (0.4 * fs)));
        const float prev = env_.load(std::memory_order_relaxed);
        const float next = (peak > prev * decay) ? peak : prev * decay;
        env_.store(next, std::memory_order_relaxed);
    }

    [[nodiscard]] float load() const noexcept {
        return env_.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        env_.store(0.0f, std::memory_order_relaxed);
    }

private:
    std::atomic<float> env_{0.0f};
};

} // namespace melodeus::audio
