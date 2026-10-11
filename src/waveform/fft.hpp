#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace melodeus::wave {

/// FFT real→magnitudes radix-2 (n potencia de 2). El workspace se aloca una
/// vez en el ctor; compute() es RT-safe-ish (sin allocs) para el hilo de
/// render. Sólo para display de espectro, no para análisis.
class FftScratch {
public:
    explicit FftScratch(int n_pow2) {
        int n = 1;
        while (n < n_pow2) {
            n <<= 1;
        }
        n_ = n;
        re_.assign(static_cast<std::size_t>(n), 0.0f);
        im_.assign(static_cast<std::size_t>(n), 0.0f);
        mag_.assign(static_cast<std::size_t>(n / 2), 0.0f);
    }

    /// Transforma `len` muestras (rellena con ceros hasta n_) y deja las
    /// magnitudes de los n_/2 bins en magnitudes().
    void compute(const float* input, int len) {
        const int n = n_;
        const int usable = std::clamp(len, 0, n);
        for (int i = 0; i < n; ++i) {
            re_[static_cast<std::size_t>(i)] = (i < usable) ? input[i] : 0.0f;
            im_[static_cast<std::size_t>(i)] = 0.0f;
        }
        // Permutación bit-reversal.
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(re_[static_cast<std::size_t>(i)], re_[static_cast<std::size_t>(j)]);
                std::swap(im_[static_cast<std::size_t>(i)], im_[static_cast<std::size_t>(j)]);
            }
        }
        constexpr double kPi = 3.14159265358979323846;
        for (int len2 = 2; len2 <= n; len2 <<= 1) {
            const double ang = -2.0 * kPi / static_cast<double>(len2);
            const double wr_step = std::cos(ang);
            const double wi_step = std::sin(ang);
            for (int i = 0; i < n; i += len2) {
                double wr = 1.0;
                double wi = 0.0;
                const int half = len2 >> 1;
                for (int j = 0; j < half; ++j) {
                    const int a = i + j;
                    const int b = i + j + half;
                    const double xr = static_cast<double>(re_[static_cast<std::size_t>(b)]) * wr -
                                      static_cast<double>(im_[static_cast<std::size_t>(b)]) * wi;
                    const double xi = static_cast<double>(re_[static_cast<std::size_t>(b)]) * wi +
                                      static_cast<double>(im_[static_cast<std::size_t>(b)]) * wr;
                    const double ar = static_cast<double>(re_[static_cast<std::size_t>(a)]);
                    const double ai = static_cast<double>(im_[static_cast<std::size_t>(a)]);
                    re_[static_cast<std::size_t>(a)] = static_cast<float>(ar + xr);
                    im_[static_cast<std::size_t>(a)] = static_cast<float>(ai + xi);
                    re_[static_cast<std::size_t>(b)] = static_cast<float>(ar - xr);
                    im_[static_cast<std::size_t>(b)] = static_cast<float>(ai - xi);
                    const double nwr = wr * wr_step - wi * wi_step;
                    wi = wr * wi_step + wi * wr_step;
                    wr = nwr;
                }
            }
        }
        const int bins = n >> 1;
        for (int i = 0; i < bins; ++i) {
            const double r = re_[static_cast<std::size_t>(i)];
            const double m = im_[static_cast<std::size_t>(i)];
            mag_[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(r * r + m * m));
        }
    }

    [[nodiscard]] const float* magnitudes() const {
        return mag_.data();
    }

    [[nodiscard]] int bins() const {
        return n_ >> 1;
    }

private:
    int n_ = 1;
    std::vector<float> re_;
    std::vector<float> im_;
    std::vector<float> mag_;
};

} // namespace melodeus::wave
