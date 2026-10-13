#include <doctest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/decoder.hpp"
#include "audio/engine.hpp"
#include "core/rt_guard.hpp"

namespace {

melodeus::audio::DecodedTrack make_constant(float value, double secs, double rate) {
    melodeus::audio::DecodedTrack t;
    t.channels = 2;
    t.sample_rate = rate;
    t.frames = static_cast<std::int64_t>(secs * rate);
    t.samples.assign(static_cast<std::size_t>(t.frames) * 2, value);
    return t;
}

melodeus::audio::DecodedTrack make_sine(double freq, double secs, double rate) {
    melodeus::audio::DecodedTrack t;
    t.channels = 2;
    t.sample_rate = rate;
    t.frames = static_cast<std::int64_t>(secs * rate);
    t.samples.resize(static_cast<std::size_t>(t.frames) * 2);
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::int64_t i = 0; i < t.frames; ++i) {
        const float s =
            0.5f * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / rate));
        t.samples[static_cast<std::size_t>(i) * 2] = s;
        t.samples[static_cast<std::size_t>(i) * 2 + 1] = s;
    }
    return t;
}

float block_peak(const std::vector<float>& l) {
    float peak = 0.0f;
    for (float v : l) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void load_play(melodeus::audio::Deck& deck, const melodeus::audio::DecodedTrack& tr) {
    deck.load({tr.samples.data(), tr.frames, tr.channels, tr.sample_rate});
    deck.play();
}

std::atomic<int> g_violations{0};

void count_violation(const char*, const void*) noexcept {
    g_violations.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

TEST_CASE("engine crossfader linear: extremos y centro") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    engine.set_limiter_ceiling_db(0.0); // sin recorte para medir la curva
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    const auto b = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    load_play(engine.deck_b(), b);

    std::vector<float> l(256);
    std::vector<float> r(256);
    const std::size_t dly = engine.limiter_delay_samples();
    REQUIRE(dly < 256);

    engine.set_crossfader(1.0);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(1.0f).epsilon(1e-6));

    engine.set_crossfader(-1.0);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(1.0f).epsilon(1e-6));

    engine.set_crossfader(0.0);
    engine.process(l.data(), r.data(), 256);
    // Lineal: ga + gb = 1; dos pistas constantes a 1.0 suman 1.0 (sin dip).
    CHECK(l[dly] == doctest::Approx(1.0f).epsilon(1e-6));

    // Solo A al centro: salida = 0.5 (dos bloques: el primero aún trae
    // muestras demoradas de la suma anterior).
    engine.deck_b().stop();
    engine.process(l.data(), r.data(), 256);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(0.5f).epsilon(1e-6));
    CHECK(engine.peak_master() == doctest::Approx(0.5f).epsilon(1e-6));
}

TEST_CASE("engine const-power al centro mantiene potencia") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::ConstantPower);
    engine.set_limiter_ceiling_db(0.0);
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    const auto b = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    load_play(engine.deck_b(), b);
    engine.set_crossfader(0.0);
    std::vector<float> l(256);
    std::vector<float> r(256);
    const std::size_t dly = engine.limiter_delay_samples();
    REQUIRE(dly < 256);

    // Solo A: salida = ga = cos(π/4) = 0.7071.
    engine.deck_b().stop();
    engine.process(l.data(), r.data(), 256);
    const double ga = std::fabs(l[dly]);

    // Solo B: salida = gb = sen(π/4) = 0.7071.
    engine.deck_a().stop();
    engine.deck_b().play();
    engine.process(l.data(), r.data(), 256);
    const double gb = std::fabs(l[dly]);

    CHECK(ga == doctest::Approx(0.7071).epsilon(0.001));
    CHECK(gb == doctest::Approx(0.7071).epsilon(0.001));
    CHECK(ga * ga + gb * gb == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("engine crossfader scratch: corte rapido en extremos y plano en centro") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Scratch);
    engine.set_limiter_ceiling_db(0.0);
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    const auto b = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    load_play(engine.deck_b(), b);

    std::vector<float> l(256);
    std::vector<float> r(256);
    const std::size_t dly = engine.limiter_delay_samples();

    // Centro x = 0.0: ambos al 100% (Scratch flat)
    engine.set_crossfader(0.0);
    engine.deck_b().stop();
    engine.process(l.data(), r.data(), 256);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(1.0f).epsilon(1e-4));

    // Extremo A (x = 1.0): A = 1.0, B = 0.0
    engine.set_crossfader(1.0);
    engine.deck_a().stop();
    engine.deck_b().play();
    engine.process(l.data(), r.data(), 256);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("engine master soft-clipper: saturacion C1 transparente hasta 0 dBFS") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    engine.set_limiter_ceiling_db(6.0); // techo alto para medir el clipper
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    engine.set_crossfader(1.0); // solo A

    std::vector<float> l(256);
    std::vector<float> r(256);
    const std::size_t dly = engine.limiter_delay_samples();

    // Input 1.0f: exactamente 1.0f (transparente por debajo y en 0 dBFS)
    engine.process(l.data(), r.data(), 256);
    CHECK(l[dly] == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("engine master gain escala la salida") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    engine.set_limiter_ceiling_db(0.0);
    engine.set_master_gain(0.5);
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    engine.set_crossfader(1.0);
    std::vector<float> l(256);
    std::vector<float> r(256);
    engine.process(l.data(), r.data(), 256);
    CHECK(l[engine.limiter_delay_samples()] == doctest::Approx(0.5f).epsilon(1e-6));
}

TEST_CASE("engine eq kill silencia la salida") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    const auto a = make_constant(1.0f, 2.0, 48000.0);
    load_play(engine.deck_a(), a);
    engine.set_crossfader(1.0);
    engine.set_eq_a(-60.0, -60.0, -60.0);
    std::vector<float> l(256);
    std::vector<float> r(256);
    float last_block_peak = 0.0f;
    for (int blk = 0; blk < 40; ++blk) { // 100 ms: suficiente para el suavizado
        engine.process(l.data(), r.data(), 256);
        last_block_peak = 0.0f;
        for (std::size_t i = 0; i < 256; ++i) {
            last_block_peak = std::max(last_block_peak, std::fabs(l[i]));
        }
    }
    // Solo el último bloque: los primeros aún tienen las ganancias bajando.
    CHECK(last_block_peak < 0.01f);
}

TEST_CASE("engine limita picos calientes por debajo del techo") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    const auto a = make_constant(1.0f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    engine.deck_a().set_gain(4.0); // entrada muy caliente
    engine.set_crossfader(1.0);
    engine.set_master_gain(1.0);
    std::vector<float> l(256);
    std::vector<float> r(256);
    engine.process(l.data(), r.data(), 256);
    CHECK(engine.peak_master() <= 0.90f); // techo -1 dBFS ≈ 0.891
    CHECK(engine.limiter_gain() < 0.5);   // mucha reducción
    // master_env refleja el pico limitado.
    CHECK(engine.master_env() <= 0.90f);
}

TEST_CASE("engine process no aloja memoria (rt_guard)") {
    melodeus::audio::Engine engine({48000.0, 512});
    const auto a = make_constant(0.25f, 1.0, 48000.0);
    load_play(engine.deck_a(), a);
    std::vector<float> l(128);
    std::vector<float> r(128);

    const auto before = melodeus::core::rt::violation_count();
    const auto prev = melodeus::core::rt::set_violation_handler(&count_violation);
    g_violations.store(0, std::memory_order_relaxed);
    engine.set_crossfader(0.3);
    for (int i = 0; i < 100; ++i) {
        engine.process(l.data(), r.data(), 128);
    }
    melodeus::core::rt::set_violation_handler(prev);
    CHECK(g_violations.load(std::memory_order_relaxed) == 0);
    CHECK(melodeus::core::rt::violation_count() == before);
}

TEST_CASE("engine filtro LP/HP corta bandas extremas; bypass pasa senal") {
    melodeus::audio::Engine engine({48000.0, 512});
    engine.set_curve(melodeus::audio::CrossfadeCurve::Linear);
    engine.set_limiter_ceiling_db(0.0);
    engine.set_master_gain(1.0);
    const auto hi = make_sine(5000.0, 2.0, 48000.0);
    const auto lo = make_sine(100.0, 2.0, 48000.0);
    std::vector<float> l(256);
    std::vector<float> r(256);
    const auto warmup = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            engine.process(l.data(), r.data(), 256);
        }
        return block_peak(l);
    };

    // Bypass: el 5 kHz pasa (amplitud del sine = 0.5).
    load_play(engine.deck_a(), hi);
    engine.set_crossfader(1.0);
    engine.set_filter_a(0.0);
    warmup(20);
    const float pass = warmup(1);
    CHECK(pass == doctest::Approx(0.5f).epsilon(0.05));

    // LP full (-1, cutoff ~300 Hz): el 5 kHz queda muy atenuado.
    engine.set_filter_a(-1.0);
    warmup(40); // suavizado 20 ms + settling
    const float lp = warmup(1);
    CHECK(lp < 0.05f);

    // HP full (+1, cutoff ~3 kHz): el 100 Hz queda muy atenuado.
    engine.set_filter_a(0.0);
    warmup(40);
    engine.deck_a().stop();
    load_play(engine.deck_a(), lo);
    engine.set_filter_a(1.0);
    warmup(40);
    const float hp = warmup(1);
    CHECK(hp < 0.05f);

    // Bypass de nuevo: el grave vuelve a pasar.
    engine.set_filter_a(0.0);
    warmup(40);
    const float lo_pass = warmup(1);
    CHECK(lo_pass == doctest::Approx(0.5f).epsilon(0.05));
}
