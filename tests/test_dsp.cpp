#include <doctest.h>

#include <cmath>
#include <vector>

#include "audio/decoder.hpp"
#include "audio/deck.hpp"
#include "audio/dsp.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Pico máximo de una senoide procesada tras descartar el transitorio.
float sine_peak_after(melodeus::audio::TptSvf& svf, double hz, double rate, int total, int skip) {
    float peak = 0.0f;
    for (int i = 0; i < total; ++i) {
        const double x = std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate);
        const auto o = svf.process(x);
        if (i >= skip) {
            peak = std::max(peak, static_cast<float>(std::fabs(o.lp)));
        }
    }
    return peak;
}

float eq_peak_after(melodeus::audio::ThreeBandEq& eq, double hz, double rate, int total, int skip) {
    float peak = 0.0f;
    for (int i = 0; i < total; ++i) {
        const double x = std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate);
        const float y = static_cast<float>(eq.process(x));
        if (i >= skip) {
            peak = std::max(peak, std::fabs(y));
        }
    }
    return peak;
}

} // namespace

TEST_CASE("svf lp pasa graves y atenúa agudos") {
    melodeus::audio::TptSvf lp;
    lp.set_sample_rate(48000.0);
    lp.set_cutoff(1000.0);
    lp.set_q(0.70710678);
    const float low = sine_peak_after(lp, 100.0, 48000.0, 1000, 400);
    CHECK(low == doctest::Approx(1.0f).epsilon(0.05));

    melodeus::audio::TptSvf lp2;
    lp2.set_sample_rate(48000.0);
    lp2.set_cutoff(1000.0);
    lp2.set_q(0.70710678);
    const float high = sine_peak_after(lp2, 8000.0, 48000.0, 1000, 400);
    CHECK(high < 0.1f);
}

TEST_CASE("svf hp pasa agudos y atenúa graves") {
    melodeus::audio::TptSvf hp;
    hp.set_sample_rate(48000.0);
    hp.set_cutoff(4000.0);
    hp.set_q(0.70710678);
    float peak = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        const double x = std::sin(2.0 * kPi * 10000.0 * static_cast<double>(i) / 48000.0);
        const auto o = hp.process(x);
        if (i >= 400) {
            peak = std::max(peak, static_cast<float>(std::fabs(o.hp)));
        }
    }
    CHECK(peak == doctest::Approx(1.0f).epsilon(0.05));

    melodeus::audio::TptSvf hp2;
    hp2.set_sample_rate(48000.0);
    hp2.set_cutoff(4000.0);
    hp2.set_q(0.70710678);
    peak = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        const double x = std::sin(2.0 * kPi * 100.0 * static_cast<double>(i) / 48000.0);
        const auto o = hp2.process(x);
        if (i >= 400) {
            peak = std::max(peak, static_cast<float>(std::fabs(o.hp)));
        }
    }
    CHECK(peak < 0.05f);
}

TEST_CASE("eq 0 dB es transparente") {
    melodeus::audio::ThreeBandEq eq;
    eq.set_sample_rate(48000.0);
    eq.set_bands(0.0, 0.0, 0.0);
    eq.reset(); // smoothers ya en 1.0
    float max_err = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const double x = std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 48000.0);
        const float y = static_cast<float>(eq.process(x));
        max_err = std::max(max_err, std::fabs(y - static_cast<float>(x)));
    }
    CHECK(max_err < 0.001f); // suma complementaria exacta + suavizado en 1.0
}

TEST_CASE("eq kill bajos silencia un tono grave") {
    melodeus::audio::ThreeBandEq eq;
    eq.set_sample_rate(48000.0);
    eq.set_bands(-60.0, -60.0, -60.0); // todo a la suelo
    const float peak = eq_peak_after(eq, 100.0, 48000.0, 4800, 2400);
    CHECK(peak < 0.01f);
}

TEST_CASE("eq kill solo bajos deja pasar un tono agudo") {
    melodeus::audio::ThreeBandEq eq;
    eq.set_sample_rate(48000.0);
    eq.set_bands(-60.0, 0.0, 0.0);
    // A 8 kHz el lp≈0 y el hp≈x; sin bajos que matar, pasa casi entero.
    const float peak = eq_peak_after(eq, 8000.0, 48000.0, 4800, 2400);
    CHECK(peak > 0.5f);
}

TEST_CASE("limiter brickwall recorta por encima del techo") {
    melodeus::audio::LookaheadLimiter lim;
    lim.setup(48000.0, 2.0, 100.0, -1.0); // techo 0.891
    const double ceiling = lim.ceiling();
    std::vector<float> in_l(2000);
    std::vector<float> in_r(2000);
    std::vector<float> out_l(2000);
    std::vector<float> out_r(2000);
    for (int i = 0; i < 2000; ++i) {
        const float v = static_cast<float>(
            2.0 * std::sin(2.0 * kPi * 200.0 * static_cast<double>(i) / 48000.0));
        in_l[static_cast<std::size_t>(i)] = v;
        in_r[static_cast<std::size_t>(i)] = v;
    }
    lim.process(in_l.data(), in_r.data(), out_l.data(), out_r.data(), 2000);
    for (std::size_t i = 0; i < 2000; ++i) {
        CHECK(std::fabs(out_l[i]) <= ceiling + 1e-3);
        CHECK(std::fabs(out_r[i]) <= ceiling + 1e-3);
    }
}

TEST_CASE("limiter deja pasar señal bajo el techo (con demora)") {
    melodeus::audio::LookaheadLimiter lim;
    lim.setup(48000.0, 2.0, 100.0, -1.0);
    const auto dly = lim.delay_samples();
    std::vector<float> in_l(512, 0.5f);
    std::vector<float> in_r(512, 0.5f);
    std::vector<float> out_l(512);
    std::vector<float> out_r(512);
    lim.process(in_l.data(), in_r.data(), out_l.data(), out_r.data(), 512);
    for (std::size_t i = dly; i < 512; ++i) {
        CHECK(out_l[i] == doctest::Approx(0.5f).epsilon(1e-5));
    }
    for (std::size_t i = 0; i < dly; ++i) {
        CHECK(out_l[i] == 0.0f);
    }
}

TEST_CASE("limiter recupera la ganancia tras el release") {
    melodeus::audio::LookaheadLimiter lim;
    lim.setup(48000.0, 2.0, 100.0, -1.0);
    std::vector<float> hot(4800, 2.0f);
    std::vector<float> silence(4800, 0.0f);
    std::vector<float> out(4800);
    lim.process(hot.data(), hot.data(), out.data(), out.data(), 4800);
    CHECK(lim.gain() < 0.6);       // fuerte reducción (0.891/2)
    for (int i = 0; i < 20; ++i) { // 2 s de silencio ≫ τ de release (100 ms)
        lim.process(silence.data(), silence.data(), out.data(), out.data(), 4800);
    }
    CHECK(lim.gain() > 0.99);
}

TEST_CASE("peak meter sube al pico y decae en silencio") {
    melodeus::audio::PeakMeter meter;
    std::vector<float> full(512, 1.0f);
    meter.process_block(full.data(), full.data(), 512, 48000.0);
    CHECK(meter.load() == doctest::Approx(1.0f).epsilon(1e-6));
    std::vector<float> zero(512, 0.0f);
    for (int i = 0; i < 10; ++i) {
        meter.process_block(zero.data(), zero.data(), 512, 48000.0);
    }
    // 10 bloques * 512 f = 5120 f ≈ 107 ms; con τ=400 ms aún queda señal,
    // pero debe haber decaído de 1.0.
    CHECK(meter.load() < 0.8f);
    CHECK(meter.load() > 0.1f);
}

TEST_CASE("deck scrub a 1/8 avanza ocho veces más lento") {
    melodeus::audio::DecodedTrack tr;
    tr.channels = 2;
    tr.sample_rate = 48000.0;
    tr.frames = 48000;
    tr.samples.assign(48000 * 2, 0.5f);
    melodeus::audio::Deck deck;
    deck.load({tr.samples.data(), tr.frames, tr.channels, tr.sample_rate});
    deck.set_scrub_rate(0.125);
    deck.play();
    std::vector<float> l(48000);
    std::vector<float> r(48000);
    deck.render(l.data(), r.data(), 48000, 48000.0);
    CHECK(deck.scrubbing());
    // 1 s de salida a 1/8 = 0.125 s de pista consumidos.
    CHECK(deck.position_seconds() == doctest::Approx(0.125).epsilon(0.01));
    deck.set_scrub_rate(0.0);
    CHECK_FALSE(deck.scrubbing());
}
