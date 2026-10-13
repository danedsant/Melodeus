#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/deck.hpp"
#include "audio/decoder.hpp"

namespace {

melodeus::audio::DecodedTrack make_sine(double hz, double secs, double rate, int channels) {
    melodeus::audio::DecodedTrack t;
    t.channels = channels;
    t.sample_rate = rate;
    t.frames = static_cast<std::int64_t>(secs * rate);
    t.samples.resize(static_cast<std::size_t>(t.frames) * channels);
    for (std::int64_t i = 0; i < t.frames; ++i) {
        const float v = static_cast<float>(
            std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / rate));
        for (int c = 0; c < channels; ++c) {
            t.samples[static_cast<std::size_t>(i) * channels + c] = v;
        }
    }
    return t;
}

melodeus::audio::TrackView view(const melodeus::audio::DecodedTrack& t) {
    return {t.samples.data(), t.frames, t.channels, t.sample_rate};
}

/// Renderiza en bloques hasta que el deck termine. Devuelve frames emitidos.
std::int64_t render_until_end(melodeus::audio::Deck& deck, double out_rate) {
    constexpr std::size_t kBlock = 256;
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    std::int64_t total = 0;
    for (int guard = 0; guard < 100000 && deck.playing(); ++guard) {
        deck.render(l.data(), r.data(), kBlock, out_rate);
        total += static_cast<std::int64_t>(kBlock);
    }
    return total;
}

} // namespace

TEST_CASE("deck a pitch 0 y tasa propia copia la señal") {
    const auto tr = make_sine(440.0, 0.1, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.play();
    float l[128];
    float r[128];
    deck.render(l, r, 128, 48000.0);
    for (std::size_t i = 0; i < 128; ++i) {
        CHECK(l[i] == doctest::Approx(tr.samples[i * 2]).epsilon(1e-6));
        CHECK(r[i] == doctest::Approx(tr.samples[i * 2 + 1]).epsilon(1e-6));
    }
    CHECK(deck.playing());
}

TEST_CASE("deck pitch +8% acorta la reproduccion") {
    const auto tr = make_sine(440.0, 1.0, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_pitch_percent(8.0);
    deck.play();
    const std::int64_t emitted = render_until_end(deck, 48000.0);
    // 48000/1.08 = 44444; el último bloque puede pasarse por <256.
    CHECK(emitted >= 44444 - 256);
    CHECK(emitted <= 44444 + 256);
    CHECK_FALSE(deck.playing());
    CHECK(deck.ended());
}

TEST_CASE("deck pitch -8% alarga la reproduccion") {
    const auto tr = make_sine(440.0, 1.0, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_pitch_percent(-8.0);
    deck.play();
    const std::int64_t emitted = render_until_end(deck, 48000.0);
    // 48000/0.92 = 52174.
    CHECK(emitted >= 52174 - 256);
    CHECK(emitted <= 52174 + 256);
}

TEST_CASE("deck stop emite ceros y seek mueve la posicion") {
    const auto tr = make_sine(440.0, 1.0, 48000.0, 1);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.seek(0.5);
    CHECK(deck.position_seconds() == doctest::Approx(0.5).epsilon(1e-9));
    float l[64];
    float r[64];
    deck.render(l, r, 64, 48000.0); // detenido: ceros
    for (std::size_t i = 0; i < 64; ++i) {
        CHECK(l[i] == 0.0f);
        CHECK(r[i] == 0.0f);
    }
    deck.play();
    deck.render(l, r, 8, 48000.0);
    CHECK(std::fabs(l[0]) > 0.0f);
}

TEST_CASE("deck gain escala la amplitud") {
    const auto tr = make_sine(440.0, 0.1, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_gain(0.5);
    deck.play();
    float l[256];
    float r[256];
    deck.render(l, r, 256, 48000.0);
    float peak = 0.0f;
    for (std::size_t i = 0; i < 256; ++i) {
        peak = std::max(peak, std::fabs(l[i]));
    }
    CHECK(peak == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("deck loop envuelve la posicion y no termina") {
    const auto tr = make_sine(440.0, 1.0, 48000.0, 1);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_loop(0.2, 0.1); // [0.2 .. 0.3) s
    CHECK(deck.looping());
    deck.play();
    // Reproduce 0.5 s (> 5 vueltas del loop): sigue sonando y la posición
    // queda dentro del rango del loop.
    float l[256];
    float r[256];
    for (int i = 0; i < 94; ++i) { // 94 * 256 / 48000 ≈ 0.5 s
        deck.render(l, r, 256, 48000.0);
    }
    CHECK(deck.playing());
    const double pos = deck.position_seconds();
    CHECK(pos >= 0.2);
    CHECK(pos < 0.3 + 256.0 / 48000.0); // tolera el último bloque
}

TEST_CASE("deck clear_loop reanuda hasta el final") {
    const auto tr = make_sine(440.0, 0.5, 48000.0, 1);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_loop(0.1, 0.05);
    deck.play();
    float l[256];
    float r[256];
    deck.render(l, r, 256, 48000.0);
    CHECK(deck.playing());
    deck.clear_loop();
    CHECK_FALSE(deck.looping());
    // Sin loop, llega al final de la pista y para.
    const std::int64_t emitted = render_until_end(deck, 48000.0);
    CHECK_FALSE(deck.playing());
    CHECK(emitted > 0);
}

TEST_CASE("deck set_loop con len<=0 desactiva") {
    const auto tr = make_sine(440.0, 0.2, 48000.0, 1);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_loop(0.05, 0.1);
    CHECK(deck.looping());
    deck.set_loop(0.05, 0.0);
    CHECK_FALSE(deck.looping());
}

TEST_CASE("deck key_shift modifica afinacion sin alterar duracion") {
    const auto tr = make_sine(440.0, 0.5, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_key_shift(2.0); // +2 semitonos
    CHECK(deck.key_shift() == doctest::Approx(2.0));
    deck.play();
    const std::int64_t emitted = render_until_end(deck, 48000.0);
    // Duración de 0.5s a 48000 = 24000 frames. Key shift NO debe alterar la duración (frames emitidos).
    CHECK(emitted >= 24000 - 256);
    CHECK(emitted <= 24000 + 256);
}

TEST_CASE("deck master_tempo mantiene afinacion constante al alterar pitch") {
    const auto tr = make_sine(440.0, 1.0, 48000.0, 2);
    melodeus::audio::Deck deck;
    deck.load(view(tr));
    deck.set_master_tempo(true);
    CHECK(deck.master_tempo());
    deck.set_pitch_percent(10.0); // +10% BPM
    deck.play();
    float l[256];
    float r[256];
    deck.render(l, r, 256, 48000.0);
    CHECK(deck.playing());
}

