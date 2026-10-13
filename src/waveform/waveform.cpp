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

    // Paleta espectral de 3 bandas (Estándar profesional DJ Rekordbox/Traktor):
    // - Graves (<250 Hz, kicks, sub-bass, 808): tono primario del deck / púrpura eléctrico
    // - Medios (250-4000 Hz, voces, leads, cajas): ámbar dorado cálido / esmeralda
    // - Agudos (>4000 Hz, hi-hats, cymbals, aire): cian brillante / blanco hielo
    float spec_lo[3];
    float spec_mid[3];
    float spec_hi[3];

    if (base_rgb != nullptr) {
        spec_lo[0] = std::clamp(base_rgb[0] * 1.15f, 0.0f, 1.0f);
        spec_lo[1] = std::clamp(base_rgb[1] * 0.85f, 0.0f, 1.0f);
        spec_lo[2] = std::clamp(base_rgb[2] * 1.15f, 0.0f, 1.0f);

        spec_mid[0] = 0.92f;
        spec_mid[1] = 0.76f;
        spec_mid[2] = 0.24f;

        spec_hi[0] = 0.55f;
        spec_hi[1] = 0.95f;
        spec_hi[2] = 1.00f;
    } else {
        // Melodeus Electric Purple spectral palette
        spec_lo[0] = 178.0f / 255.0f;
        spec_lo[1] = 60.0f / 255.0f;
        spec_lo[2] = 240.0f / 255.0f;

        spec_mid[0] = 0.92f;
        spec_mid[1] = 0.76f;
        spec_mid[2] = 0.24f;

        spec_hi[0] = 0.55f;
        spec_hi[1] = 0.95f;
        spec_hi[2] = 1.00f;
    }

    std::size_t n = 0;

    // En vista overview: dibujar primero una línea de eje central tenue (referencia de cero).
    if (is_overview && n < max_out) {
        WaveInstance& wl = out[n++];
        wl.x0 = ndc_x0;
        wl.x1 = ndc_x1;
        wl.y0 = ndc_y_center - 0.012f;
        wl.y1 = ndc_y_center + 0.012f;
        wl.r = spec_lo[0] * 0.25f;
        wl.g = spec_lo[1] * 0.25f;
        wl.b = spec_lo[2] * 0.25f;
    }

    for (std::int64_t i = range.begin; i < range.end && n < max_out; ++i) {
        const WaveBucket& bk = buckets[i];
        const double s0 = static_cast<double>(i * bsize) - first_sample;
        const double s1 = static_cast<double>((i + 1) * bsize) - first_sample;
        const float x0 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s0 * inv_span));
        const float x1 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s1 * inv_span));
        const float a0 = bk.min * amplitude_scale;
        const float a1 = bk.max * amplitude_scale;

        // Mezcla espectral aditiva de 3 bandas ponderada por energías
        const float s_lo = bk.lo;
        const float s_mid = bk.mid;
        const float s_hi = bk.hi;
        const float s_sum = s_lo + s_mid + s_hi + 1e-6f;
        const float w_lo = s_lo / s_sum;
        const float w_mid = s_mid / s_sum;
        const float w_hi = s_hi / s_sum;

        const float sr = w_lo * spec_lo[0] + w_mid * spec_mid[0] + w_hi * spec_hi[0];
        const float sg = w_lo * spec_lo[1] + w_mid * spec_mid[1] + w_hi * spec_hi[1];
        const float sb = w_lo * spec_lo[2] + w_mid * spec_mid[2] + w_hi * spec_hi[2];

        if (is_overview) {
            // Capa 1: Envolvente exterior de picos transitorios (silueta tenue)
            WaveInstance& w_env = out[n++];
            w_env.x0 = x0;
            w_env.x1 = x1;
            w_env.y0 = ndc_y_center + ndc_y_half * a0;
            w_env.y1 = ndc_y_center + ndc_y_half * a1;
            w_env.r = std::clamp(sr * 0.40f, 0.0f, 1.0f);
            w_env.g = std::clamp(sg * 0.40f, 0.0f, 1.0f);
            w_env.b = std::clamp(sb * 0.40f, 0.0f, 1.0f);

            // Capa 2: Núcleo interior de energía RMS con modulación espectral luminosa
            if (n < max_out) {
                const float rms_norm = std::clamp(bk.rms * 1.85f * amplitude_scale, 0.02f, 0.95f);
                WaveInstance& w_core = out[n++];
                w_core.x0 = x0;
                w_core.x1 = x1;
                w_core.y0 = ndc_y_center - ndc_y_half * rms_norm;
                w_core.y1 = ndc_y_center + ndc_y_half * rms_norm;

                const float core_bright = 0.65f + 0.35f * rms_norm;
                w_core.r = std::clamp(sr * core_bright, 0.0f, 1.0f);
                w_core.g = std::clamp(sg * core_bright, 0.0f, 1.0f);
                w_core.b = std::clamp(sb * core_bright, 0.0f, 1.0f);
            }
        } else {
            WaveInstance& w = out[n++];
            w.x0 = x0;
            w.x1 = x1;
            w.y0 = ndc_y_center + ndc_y_half * a0;
            w.y1 = ndc_y_center + ndc_y_half * a1;

            const float amp = std::clamp(std::max(std::fabs(a0), std::fabs(a1)), 0.0f, 1.0f);
            const float bright = 0.45f + 0.55f * amp;
            w.r = std::clamp(sr * bright, 0.0f, 1.0f);
            w.g = std::clamp(sg * bright, 0.0f, 1.0f);
            w.b = std::clamp(sb * bright, 0.0f, 1.0f);
        }
    }
    return n;
}

} // namespace melodeus::wave
