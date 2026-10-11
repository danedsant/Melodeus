#pragma once

#include <cmath>

namespace melodeus::audio {

/// Curvas del crossfader. x ∈ [-1,1] (-1 = solo B, +1 = solo A).
enum class CrossfadeCurve { Sharp, Linear, ConstantPower };

/// Ganancias (a,b) para la posición x del crossfader. RT-safe.
inline void crossfade_gains(CrossfadeCurve curve, double x, double* a, double* b) noexcept {
    const double t = 0.5 * (x + 1.0); // 0 = B, 1 = A
    switch (curve) {
    case CrossfadeCurve::Sharp:
        *a = (t >= 0.5) ? 1.0 : 0.0;
        *b = 1.0 - *a;
        break;
    case CrossfadeCurve::Linear:
        *a = t;
        *b = 1.0 - t;
        break;
    case CrossfadeCurve::ConstantPower: {
        constexpr double kHalfPi = 1.57079632679489661923;
        const double theta = (1.0 - t) * kHalfPi;
        *a = std::cos(theta);
        *b = std::sin(theta);
        break;
    }
    }
}

} // namespace melodeus::audio
