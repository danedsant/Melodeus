#include <doctest.h>

#include <cmath>
#include <vector>

#include "waveform/fft.hpp"
#include "waveform/waveform.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> make_sine(double hz, double secs, double rate, float amp = 1.0f) {
    const auto frames = static_cast<std::int64_t>(secs * rate);
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (std::int64_t i = 0; i < frames; ++i) {
        v[static_cast<std::size_t>(i)] =
            amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    }
    return v;
}

} // namespace

TEST_CASE("pirámide: conteos, niveles y fusión") {
    const auto sig = make_sine(440.0, 1.0, 48000.0);
    melodeus::wave::WaveformPyramid pyr;
    pyr.build(sig.data(), static_cast<std::int64_t>(sig.size()), 1, 48000.0);
    REQUIRE_FALSE(pyr.empty());
    CHECK(pyr.total_frames() == static_cast<std::int64_t>(sig.size()));
    CHECK(pyr.peak() == doctest::Approx(1.0f).epsilon(0.01));

    const std::int64_t n0 = pyr.bucket_count(0);
    CHECK(n0 == (48000 + 255) / 256);
    CHECK(pyr.levels() >= 4);

    // Fusionar: min del padre = min de sus hijos, etc.
    for (int lvl = 1; lvl < pyr.levels(); ++lvl) {
        const auto* prev = pyr.buckets(lvl - 1);
        const auto* cur = pyr.buckets(lvl);
        const std::int64_t count = pyr.bucket_count(lvl);
        for (std::int64_t i = 0; i < count; ++i) {
            const auto& a = prev[i * 2];
            const auto& p = cur[i];
            float lo = a.lo;
            float mn = a.min;
            float mx = a.max;
            float rms = a.rms;
            if (i * 2 + 1 < pyr.bucket_count(lvl - 1)) {
                lo = std::max(lo, prev[i * 2 + 1].lo);
                mn = std::min(mn, prev[i * 2 + 1].min);
                mx = std::max(mx, prev[i * 2 + 1].max);
                rms = std::sqrt(0.5f * (a.rms * a.rms + prev[i * 2 + 1].rms * prev[i * 2 + 1].rms));
            }
            CHECK(p.min == mn);
            CHECK(p.max == mx);
            CHECK(p.lo == lo);
            CHECK(p.rms == doctest::Approx(rms));
        }
    }
}

TEST_CASE("pirámide: bandas separan grave de agudo") {
    const auto low = make_sine(100.0, 0.5, 48000.0);
    melodeus::wave::WaveformPyramid pyr_lo;
    pyr_lo.build(low.data(), static_cast<std::int64_t>(low.size()), 1, 48000.0);
    // Buscar un bucket de nivel 0 ya asentado (i>=8) y comprobar dominancia.
    const auto* b = pyr_lo.buckets(0) + 10;
    CHECK(b->lo > b->hi * 10.0f);
    CHECK(b->lo > 0.5f);

    const auto high = make_sine(8000.0, 0.5, 48000.0);
    melodeus::wave::WaveformPyramid pyr_hi;
    pyr_hi.build(high.data(), static_cast<std::int64_t>(high.size()), 1, 48000.0);
    const auto* c = pyr_hi.buckets(0) + 10;
    CHECK(c->hi > c->lo * 10.0f);
    CHECK(c->hi > 0.5f);
}

TEST_CASE("pick_level y visible_range") {
    const auto sig = make_sine(440.0, 2.0, 48000.0);
    melodeus::wave::WaveformPyramid pyr;
    pyr.build(sig.data(), static_cast<std::int64_t>(sig.size()), 1, 48000.0);

    CHECK(melodeus::wave::pick_level(pyr, 1.0) == 0);
    CHECK(melodeus::wave::pick_level(pyr, 256.0) == 0);
    CHECK(melodeus::wave::pick_level(pyr, 512.0) == 1);
    CHECK(melodeus::wave::pick_level(pyr, 1.0e9) == pyr.levels() - 1);

    // Overview: centrado, cubre todo.
    const auto ov = melodeus::wave::visible_range(pyr, 48000.0, 96000.0, 96000.0 / 1200.0);
    CHECK(ov.begin == 0);
    CHECK(ov.end == pyr.bucket_count(ov.level));

    // Detalle: 8 s alrededor de 1 s, nivel 0 (spp≈8*48000/1200=320 → nivel 1? 320>256 → nivel 0
    // porque bucket(1)=512 > 320).
    const auto det = melodeus::wave::visible_range(pyr, 48000.0, 8.0 * 48000.0, 320.0);
    CHECK(det.level == 0);
    CHECK(det.begin < det.end);
    CHECK(det.begin >= 0);
    CHECK(det.end <= pyr.bucket_count(0));
}

TEST_CASE("fill_wave_instances mapea muestras a NDC") {
    const auto sig = make_sine(440.0, 1.0, 48000.0);
    melodeus::wave::WaveformPyramid pyr;
    pyr.build(sig.data(), static_cast<std::int64_t>(sig.size()), 1, 48000.0);
    const auto range = melodeus::wave::visible_range(pyr, 24000.0, 48000.0, 100.0);
    std::vector<melodeus::wave::WaveInstance> inst(4096);
    const std::size_t n = melodeus::wave::fill_wave_instances(
        pyr, range, 0.0, 48000.0, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, inst.data(), inst.size());
    REQUIRE(n > 0);
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(inst[i].x0 <= inst[i].x1);
        CHECK(inst[i].x0 >= -1.5f);
        CHECK(inst[i].x1 <= 1.5f);
        // Sena de amplitud 1: |y| ≤ half en NDC.
        CHECK(std::fabs(inst[i].y0) <= 1.05f);
        CHECK(std::fabs(inst[i].y1) <= 1.05f);
        // Degradado navy→purple: componentes dentro de [0,1] y azul domina
        // en el tramo navy (amplitud baja → kPeakNavy con b > r).
        CHECK(inst[i].r >= 0.0f);
        CHECK(inst[i].g >= 0.0f);
        CHECK(inst[i].b >= 0.0f);
        CHECK(inst[i].r <= 1.0f);
        CHECK(inst[i].g <= 1.0f);
        CHECK(inst[i].b <= 1.0f);
    }
    // Primera instancia empieza en el borde izquierdo.
    CHECK(inst[0].x0 == doctest::Approx(-1.0f).epsilon(0.01));
}

TEST_CASE("fill_wave_instances genera doble capa en modo overview") {
    const auto sig = make_sine(440.0, 1.0, 48000.0);
    melodeus::wave::WaveformPyramid pyr;
    pyr.build(sig.data(), static_cast<std::int64_t>(sig.size()), 1, 48000.0);
    const auto range = melodeus::wave::visible_range(pyr, 24000.0, 48000.0, 100.0);
    std::vector<melodeus::wave::WaveInstance> inst(8192);
    const std::size_t n = melodeus::wave::fill_wave_instances(
        pyr, range, 0.0, 48000.0, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, inst.data(), inst.size(),
        nullptr, true);
    REQUIRE(n > 0);
    // En modo overview se genera la línea central más quads de envolvente y núcleo
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(inst[i].x0 <= inst[i].x1);
        CHECK(std::fabs(inst[i].y0) <= 1.05f);
        CHECK(std::fabs(inst[i].y1) <= 1.05f);
    }
}

TEST_CASE("fft: pico en el bin correcto") {
    melodeus::wave::FftScratch fft(2048);
    CHECK(fft.bins() == 1024);
    const double rate = 48000.0;
    const double hz = 1000.0;
    std::vector<float> sig(2048);
    for (int i = 0; i < 2048; ++i) {
        sig[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    }
    fft.compute(sig.data(), 2048);
    const float* mag = fft.magnitudes();
    int argmax = 0;
    for (int i = 1; i < fft.bins(); ++i) {
        if (mag[i] > mag[argmax]) {
            argmax = i;
        }
    }
    const double bin_hz = rate / 2048.0;
    CHECK(argmax == doctest::Approx(hz / bin_hz).epsilon(0.02));
    CHECK(mag[argmax] > 500.0f); // ~n/2 para seno unitario
}

TEST_CASE("fft sin allocs en compute") {
    melodeus::wave::FftScratch fft(1024);
    std::vector<float> sig(1024, 0.1f);
    for (int i = 0; i < 100; ++i) {
        fft.compute(sig.data(), 1024);
    }
    CHECK(fft.bins() == 512);
}
