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
            if (i * 2 + 1 < prev.size()) {
                const WaveBucket& b = prev[i * 2 + 1];
                o.min = std::min(o.min, b.min);
                o.max = std::max(o.max, b.max);
                o.lo = std::max(o.lo, b.lo);
                o.mid = std::max(o.mid, b.mid);
                o.hi = std::max(o.hi, b.hi);
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
                                float amplitude_scale, WaveInstance* out, std::size_t max_out) {
    if (out == nullptr || max_out == 0 || range.begin >= range.end || samples_visible <= 0.0) {
        return 0;
    }
    const WaveBucket* buckets = pyramid.buckets(range.level);
    if (buckets == nullptr) {
        return 0;
    }
    const std::int64_t bsize = pyramid.bucket_samples(range.level);
    const double inv_span = 1.0 / samples_visible;
    std::size_t n = 0;
    for (std::int64_t i = range.begin; i < range.end && n < max_out; ++i, ++n) {
        const WaveBucket& bk = buckets[i];
        const double s0 = static_cast<double>(i * bsize) - first_sample;
        const double s1 = static_cast<double>((i + 1) * bsize) - first_sample;
        WaveInstance& w = out[n];
        w.x0 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s0 * inv_span));
        w.x1 = ndc_x0 + static_cast<float>((ndc_x1 - ndc_x0) * (s1 * inv_span));
        const float a0 = bk.min * amplitude_scale;
        const float a1 = bk.max * amplitude_scale;
        w.y0 = ndc_y_center + ndc_y_half * a0;
        w.y1 = ndc_y_center + ndc_y_half * a1;
        const float m = std::max({bk.lo, bk.mid, bk.hi, 1.0e-6f});
        const float brightness = std::clamp(std::max(std::fabs(a0), std::fabs(a1)), 0.0f, 1.0f);
        w.r = std::clamp(bk.lo / m * brightness, 0.0f, 1.0f);
        w.g = std::clamp(bk.mid / m * brightness * 0.9f, 0.0f, 1.0f);
        w.b = std::clamp(bk.hi / m * brightness, 0.0f, 1.0f);
    }
    return n;
}

} // namespace melodeus::wave
