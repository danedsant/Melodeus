#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace melodeus::wave {

/// Un bucket de la pirámide: min/max de la forma de onda + energías de banda
/// (bajo <250 Hz, medio residual, alto >4 kHz) para el coloreado RGB.
struct WaveBucket {
    float min = 0.0f;
    float max = 0.0f;
    float lo = 0.0f;
    float mid = 0.0f;
    float hi = 0.0f;
};

/// Pirámide multiresolución: nivel 0 = buckets de kBaseBucketSamples muestras;
/// cada nivel duplica el tamaño fusionando 2 hijos (min=min, max=max, bandas=max).
/// Se construye fuera del hilo de render y se consume como solo lectura.
class WaveformPyramid {
public:
    static constexpr std::int64_t kBaseBucketSamples = 256;
    static constexpr int kMaxLevels = 8; // 256 .. 32768 muestras/bucket

    /// Construye desde muestras interleaved (promedio a mono para la onda).
    void build(const float* interleaved, std::int64_t frames, int channels, double sample_rate);

    [[nodiscard]] bool empty() const {
        return levels_.empty();
    }

    [[nodiscard]] int levels() const {
        return static_cast<int>(levels_.size());
    }

    [[nodiscard]] std::int64_t bucket_count(int level) const {
        if (level < 0 || level >= levels()) {
            return 0;
        }
        return static_cast<std::int64_t>(levels_[static_cast<std::size_t>(level)].size());
    }

    [[nodiscard]] const WaveBucket* buckets(int level) const {
        if (level < 0 || level >= levels()) {
            return nullptr;
        }
        return levels_[static_cast<std::size_t>(level)].data();
    }

    [[nodiscard]] double sample_rate() const {
        return sample_rate_;
    }

    [[nodiscard]] std::int64_t total_frames() const {
        return frames_;
    }

    [[nodiscard]] float peak() const {
        return peak_;
    }

    [[nodiscard]] std::int64_t bucket_samples(int level) const {
        if (level < 0) {
            return kBaseBucketSamples;
        }
        return kBaseBucketSamples << std::min(level, kMaxLevels - 1);
    }

private:
    std::vector<std::vector<WaveBucket>> levels_;
    double sample_rate_ = 48000.0;
    std::int64_t frames_ = 0;
    float peak_ = 0.0f;
};

/// Nivel más grueso cuyo bucket cabe en ~1 píxel para esa muestra/píxel.
int pick_level(const WaveformPyramid& pyramid, double samples_per_pixel);

struct VisibleRange {
    int level = 0;
    std::int64_t begin = 0; // bucket inicial (inclusivo)
    std::int64_t end = 0;   // bucket final (exclusivo)
};

/// Buckets visibles para el rango de muestras centrado en `center_sample`
/// con extensión `span_samples`, al nivel elegido por samples_per_pixel.
VisibleRange visible_range(const WaveformPyramid& pyramid, double center_sample,
                           double span_samples, double samples_per_pixel);

/// Instancia de geometría para el pase D3D12 (quads; 28 B).
struct WaveInstance {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f; // NDC (arriba +)
    float y1 = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

/// Rellena instancias NDC para el rango visible. `first_sample` es la muestra
/// del borde izquierdo; `samples_visible` la extensión en muestras.
std::size_t fill_wave_instances(const WaveformPyramid& pyramid, const VisibleRange& range,
                                double first_sample, double samples_visible, float ndc_x0,
                                float ndc_x1, float ndc_y_center, float ndc_y_half,
                                float amplitude_scale, WaveInstance* out, std::size_t max_out);

} // namespace melodeus::wave
