#include "waveform/waveform.hpp"

#include "audio/dsp.hpp"

namespace melodeus::wave {

void WaveformPyramid::build(const float* interleaved, std::int64_t frames, int channels,
                            double sample_rate) {
    levels_.clear();
    frames_ = frames;
    sample_rate_ = (sample_rate > 0.0) ? sample_rate : 48000.0;
    peak_ = 0.0f;
    if (interleaved == nullptr || frames <= 0 || channels < 1) {
        return;
    }

    const std::int64_t n0 = (frames + kBaseBucketSamples - 1) / kBaseBucketSamples;
    std::vector<WaveBucket> lv0(static_cast<std::size_t>(n0));

    audio::TptSvf lp;
    audio::TptSvf hp;
    lp.set_sample_rate(sample_rate_);
    lp.set_cutoff(250.0);
    lp.set_q(0.70710678);
    hp.set_sample_rate(sample_rate_);
    hp.set_cutoff(4000.0);
    hp.set_q(0.70710678);

    float sum_sq = 0.0f;
    std::int64_t bucket_samples_counted = 0;
    for (std::int64_t i = 0; i < frames; ++i) {
        float x = 0.0f;
        for (int c = 0; c < channels; ++c) {
            x += interleaved[static_cast<std::size_t>(i) * channels + c];
        }
        x /= static_cast<float>(channels);
        peak_ = std::max(peak_, std::fabs(x));
        const auto o_lp = lp.process(x);
        const auto o_hp = hp.process(x);
        const float lo = static_cast<float>(std::fabs(o_lp.lp));
        const float hi = static_cast<float>(std::fabs(o_hp.hp));
        const float mid = static_cast<float>(std::fabs(x - o_lp.lp - o_hp.hp));
        WaveBucket& bk = lv0[static_cast<std::size_t>(i / kBaseBucketSamples)];
        sum_sq += x * x;
        ++bucket_samples_counted;
        if (i % kBaseBucketSamples == 0) {
            bk.min = x;
            bk.max = x;
            bk.lo = lo;
            bk.mid = mid;
            bk.hi = hi;
        } else {
            bk.min = std::min(bk.min, x);
            bk.max = std::max(bk.max, x);
            bk.lo = std::max(bk.lo, lo);
            bk.mid = std::max(bk.mid, mid);
            bk.hi = std::max(bk.hi, hi);
        }
        if ((i + 1) % kBaseBucketSamples == 0 || i + 1 == frames) {
            bk.rms = (bucket_samples_counted > 0)
                         ? std::sqrt(sum_sq / static_cast<float>(bucket_samples_counted))
                         : 0.0f;
            sum_sq = 0.0f;
            bucket_samples_counted = 0;
        }
    }
    levels_.push_back(std::move(lv0));

    for (int lvl = 1; lvl < kMaxLevels; ++lvl) {
        const std::vector<WaveBucket>& prev = levels_.back();
        if (prev.size() < 2) {
            break;
        }
        const std::size_t n = (prev.size() + 1) / 2;
        std::vector<WaveBucket> cur(n);
        for (std::size_t i = 0; i < n; ++i) {
            const WaveBucket& a = prev[i * 2];
            WaveBucket& o = cur[i];
            o.min = a.min;
            o.max = a.max;
            o.lo = a.lo;
            o.mid = a.mid;
            o.hi = a.hi;
            o.rms = a.rms;
            if (i * 2 + 1 < prev.size()) {
                const WaveBucket& b = prev[i * 2 + 1];
                o.min = std::min(o.min, b.min);
                o.max = std::max(o.max, b.max);
                o.lo = std::max(o.lo, b.lo);
                o.mid = std::max(o.mid, b.mid);
                o.hi = std::max(o.hi, b.hi);
                o.rms = std::sqrt(0.5f * (a.rms * a.rms + b.rms * b.rms));
            }
        }
        levels_.push_back(std::move(cur));
    }
}

int pick_level(const WaveformPyramid& pyramid, double samples_per_pixel) {
    if (pyramid.empty()) {
        return 0;
    }
    const double spp = std::max(samples_per_pixel, 1.0);
    int level = 0;
    while (level + 1 < pyramid.levels() &&
           static_cast<double>(pyramid.bucket_samples(level + 1)) <= spp) {
        ++level;
    }
    return level;
}

VisibleRange visible_range(const WaveformPyramid& pyramid, double center_sample,
                           double span_samples, double samples_per_pixel) {
    VisibleRange r;
    if (pyramid.empty() || span_samples <= 0.0) {
        return r;
    }
    r.level = pick_level(pyramid, samples_per_pixel);
    const std::int64_t bsize = pyramid.bucket_samples(r.level);
    const std::int64_t count = pyramid.bucket_count(r.level);
    if (bsize <= 0 || count <= 0) {
        return r;
    }
    const std::int64_t b = static_cast<std::int64_t>(
        std::floor((center_sample - span_samples * 0.5) / static_cast<double>(bsize)));
    const std::int64_t e = static_cast<std::int64_t>(std::ceil(
                               (center_sample + span_samples * 0.5) / static_cast<double>(bsize))) +
                           1;
    r.begin = std::clamp<std::int64_t>(b, 0, count);
    r.end = std::clamp<std::int64_t>(e, r.begin, count);
    return r;
}

std::size_t fill_wave_instances(const WaveformPyramid& pyramid, const VisibleRange& range,
                                double first_sample, double samples_visible, float ndc_x0,
                                float ndc_x1, float ndc_y_center, float ndc_y_half,
                                float amplitude_scale, WaveInstance* out, std::size_t max_out,
                                const float* base_rgb, bool is_overview) {
    if (out == nullptr || max_out == 0 || range.begin >= range.end || samples_visible <= 0.0) {
        return 0;
    }
    const WaveBucket* buckets = pyramid.buckets(range.level);
    if (buckets == nullptr) {
        return 0;
    }
    const std::int64_t bsize = pyramid.bucket_samples(range.level);
    const double inv_span = 1.0 / samples_visible;

    // Paleta adaptativa de 3 paradas basada en base_rgb:
    // stop 0 (amp=0): sombra profunda del color del deck
    // stop 1 (amp=0.6): color base seleccionado del deck
    // stop 2 (amp=1.0): resalte luminoso pastel/blanco
    float col_low[3];
    float col_mid[3];
    float col_high[3];

    if (base_rgb != nullptr) {
        col_mid[0] = std::clamp(base_rgb[0], 0.0f, 1.0f);
        col_mid[1] = std::clamp(base_rgb[1], 0.0f, 1.0f);
        col_mid[2] = std::clamp(base_rgb[2], 0.0f, 1.0f);

        col_low[0] = col_mid[0] * 0.18f;
        col_low[1] = col_mid[1] * 0.18f;
        col_low[2] = col_mid[2] * 0.18f + 0.04f;

        col_high[0] = col_mid[0] + (1.0f - col_mid[0]) * 0.45f;
        col_high[1] = col_mid[1] + (1.0f - col_mid[1]) * 0.45f;
        col_high[2] = col_mid[2] + (1.0f - col_mid[2]) * 0.45f;
    } else {
        col_low[0] = 18.0f / 255.0f;
        col_low[1] = 32.0f / 255.0f;
        col_low[2] = 92.0f / 255.0f;

        col_mid[0] = 178.0f / 255.0f;
        col_mid[1] = 60.0f / 255.0f;
        col_mid[2] = 240.0f / 255.0f;

        col_high[0] = 236.0f / 255.0f;
        col_high[1] = 130.0f / 255.0f;
        col_high[2] = 250.0f / 255.0f;
    }

    std::size_t n = 0;

    // En vista overview: dibujar primero una línea de eje central tenue (referencia de cero).
    if (is_overview && n < max_out) {
        WaveInstance& wl = out[n++];
        wl.x0 = ndc_x0;
        wl.x1 = ndc_x1;
        wl.y0 = ndc_y_center - 0.012f;
        wl.y1 = ndc_y_center + 0.012f;
        wl.r = col_low[0] * 1.5f;
        wl.g = col_low[1] * 1.5f;
        wl.b = col_low[2] * 1.5f;
    }

    for (std::int64_t i = range.begin; i < range.end && n < max_out; ++i) {
        const WaveBucket& bk = buckets[i];
        const double s0 = static_cast<double>(i * bsize) - first_sample;
        const double s1 = static_cast<double>((i + 1) * bsize) - first_sample;
        const float x0 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s0 * inv_span));
        const float x1 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s1 * inv_span));
        const float a0 = bk.min * amplitude_scale;
        const float a1 = bk.max * amplitude_scale;

        if (is_overview) {
            // Capa 1: Envolvente exterior de picos transitorios (silueta tenue)
            WaveInstance& w_env = out[n++];
            w_env.x0 = x0;
            w_env.x1 = x1;
            w_env.y0 = ndc_y_center + ndc_y_half * a0;
            w_env.y1 = ndc_y_center + ndc_y_half * a1;
            w_env.r = col_low[0];
            w_env.g = col_low[1];
            w_env.b = col_low[2];

            // Capa 2: Núcleo interior de energía RMS con modulación espectral
            if (n < max_out) {
                const float rms_norm = std::clamp(bk.rms * 1.85f * amplitude_scale, 0.02f, 0.95f);
                WaveInstance& w_core = out[n++];
                w_core.x0 = x0;
                w_core.x1 = x1;
                w_core.y0 = ndc_y_center - ndc_y_half * rms_norm;
                w_core.y1 = ndc_y_center + ndc_y_half * rms_norm;

                // Color dinámico según energía RMS
                const float t = rms_norm;
                w_core.r = col_mid[0] + (col_high[0] - col_mid[0]) * t;
                w_core.g = col_mid[1] + (col_high[1] - col_mid[1]) * t;
                w_core.b = col_mid[2] + (col_high[2] - col_mid[2]) * t;
            }
        } else {
            WaveInstance& w = out[n++];
            w.x0 = x0;
            w.x1 = x1;
            w.y0 = ndc_y_center + ndc_y_half * a0;
            w.y1 = ndc_y_center + ndc_y_half * a1;
            const float amp = std::clamp(std::max(std::fabs(a0), std::fabs(a1)), 0.0f, 1.0f);
            if (amp <= 0.6f) {
                const float t = amp / 0.6f;
                w.r = col_low[0] + (col_mid[0] - col_low[0]) * t;
                w.g = col_low[1] + (col_mid[1] - col_low[1]) * t;
                w.b = col_low[2] + (col_mid[2] - col_low[2]) * t;
            } else {
                const float t = (amp - 0.6f) / 0.4f;
                w.r = col_mid[0] + (col_high[0] - col_mid[0]) * t;
                w.g = col_mid[1] + (col_high[1] - col_mid[1]) * t;
                w.b = col_mid[2] + (col_high[2] - col_mid[2]) * t;
            }
        }
    }
    return n;
}

} // namespace melodeus::wave
