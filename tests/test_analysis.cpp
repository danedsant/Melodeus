#include <doctest.h>

#include "analysis/beatgrid.hpp"
#include "analysis/bpm.hpp"
#include "analysis/key.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

/// Click track: impulsos de ruido corto cada `bpm` durante `seconds`.
std::vector<float> make_click_track(double bpm, double seconds, double rate = kRate) {
    const std::size_t total = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(total, 0.0f);
    const double period = 60.0 / bpm * rate;
    for (double pos = rate * 0.05; pos < static_cast<double>(total); pos += period) {
        const auto i0 = static_cast<std::size_t>(pos);
        for (std::size_t k = 0; k < 256 && i0 + k < total; ++k) {
            // Impulso con decaimiento exponencial corto (barrido de ruido
            // determinista).
            const float noise =
                static_cast<float>(std::sin(k * 12.9898) * 43758.5453 - std::floor(k * 1.1));
            out[i0 + k] += noise * std::exp(-static_cast<float>(k) / 40.0f);
        }
    }
    return out;
}

/// Suma de senos (acorde) durante `seconds`.
std::vector<float> make_chord(const std::vector<double>& freqs, double seconds,
                              double rate = kRate) {
    const std::size_t total = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(total, 0.0f);
    const float amp = 0.33f / static_cast<float>(freqs.size());
    for (std::size_t i = 0; i < total; ++i) {
        float s = 0.0f;
        for (double f : freqs) {
            s += amp * static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(i) / rate));
        }
        out[i] = s;
    }
    return out;
}

double note_hz(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

} // namespace

TEST_CASE("BPM: click track 128 detectado") {
    const auto track = make_click_track(128.0, 6.0);
    const double bpm = melodeus::analysis::detect_bpm(
        track.data(), static_cast<std::int64_t>(track.size()), kRate);
    CHECK(bpm == doctest::Approx(128.0).epsilon(0.01));
}

TEST_CASE("BPM: click tracks 90 y 140") {
    const auto t90 = make_click_track(90.0, 6.0);
    CHECK(melodeus::analysis::detect_bpm(t90.data(), static_cast<std::int64_t>(t90.size()),
                                         kRate) == doctest::Approx(90.0).epsilon(0.01));
    const auto t140 = make_click_track(140.0, 6.0);
    CHECK(melodeus::analysis::detect_bpm(t140.data(), static_cast<std::int64_t>(t140.size()),
                                         kRate) == doctest::Approx(140.0).epsilon(0.01));
}

TEST_CASE("BPM: señal corta o vacía devuelve 0") {
    std::vector<float> tiny(100, 0.0f);
    CHECK(melodeus::analysis::detect_bpm(tiny.data(), 100, kRate) == 0.0);
    CHECK(melodeus::analysis::detect_bpm(nullptr, 0, kRate) == 0.0);
}

TEST_CASE("Key: acorde mayor de A (11B)") {
    // A mayor: A4, C#5, E5.
    const auto track = make_chord({note_hz(69), note_hz(73), note_hz(76)}, 5.0);
    const auto key = melodeus::analysis::detect_key(track.data(),
                                                    static_cast<std::int64_t>(track.size()), kRate);
    CHECK(key.minor == false);
    CHECK(key.root_pc == 9); // A
    CHECK(key.camelot == 11);
    CHECK(std::string(key.camelot_name()) == "11B");
    CHECK(std::string(key.traditional_name()) == "A");
    CHECK(key.confidence > 0.7f);
}

TEST_CASE("Key: acorde menor de A (8A)") {
    // A menor: A4, C5, E5.
    const auto track = make_chord({note_hz(69), note_hz(72), note_hz(76)}, 5.0);
    const auto key = melodeus::analysis::detect_key(track.data(),
                                                    static_cast<std::int64_t>(track.size()), kRate);
    CHECK(key.minor == true);
    CHECK(key.root_pc == 9);
    CHECK(key.camelot == 8);
    CHECK(std::string(key.camelot_name()) == "8A");
    CHECK(std::string(key.traditional_name()) == "Am");
}

TEST_CASE("Key: acorde mayor de C (8B)") {
    const auto track = make_chord({note_hz(60), note_hz(64), note_hz(67)}, 5.0);
    const auto key = melodeus::analysis::detect_key(track.data(),
                                                    static_cast<std::int64_t>(track.size()), kRate);
    CHECK(key.minor == false);
    CHECK(key.root_pc == 0);
    CHECK(key.camelot == 8);
    CHECK(std::string(key.camelot_name()) == "8B");
}

TEST_CASE("Beatgrid: índice y tiempo de beats") {
    const auto grid = melodeus::analysis::make_beatgrid(120.0, 0.5);
    CHECK(grid.valid());
    CHECK(grid.beat_index(0.0) == -1); // antes del offset
    CHECK(grid.beat_index(0.5) == 0);
    CHECK(grid.beat_index(1.0) == 1);
    CHECK(grid.beat_time(2) == doctest::Approx(1.5));
    CHECK(melodeus::analysis::make_beatgrid(0.0, 0.0).valid() == false);
}
