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
