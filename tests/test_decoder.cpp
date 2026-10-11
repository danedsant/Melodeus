#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

} // namespace

TEST_CASE("wav pcm16 roundtrip") {
    const auto src = make_sine(440.0, 0.05, 48000.0, 2);
    std::vector<std::uint8_t> bytes;
    std::string err;
    REQUIRE(melodeus::audio::encode_wav_pcm16(src, &bytes, &err));
    REQUIRE(bytes.size() > 44);

    melodeus::audio::DecodedTrack got;
    REQUIRE(melodeus::audio::decode_audio_memory(bytes.data(), bytes.size(), "wav", &got, &err));
    CHECK(got.channels == 2);
    CHECK(got.sample_rate == doctest::Approx(48000.0));
    CHECK(got.frames == src.frames);
    double max_err = 0.0;
    for (std::size_t i = 0; i < src.samples.size(); ++i) {
        max_err =
            std::max(max_err, std::fabs(static_cast<double>(got.samples[i]) - src.samples[i]));
    }
    CHECK(max_err < 1e-4); // cuantización a int16
}

TEST_CASE("wav mono conserva canales") {
    const auto src = make_sine(220.0, 0.01, 44100.0, 1);
    std::vector<std::uint8_t> bytes;
    std::string err;
    REQUIRE(melodeus::audio::encode_wav_pcm16(src, &bytes, &err));
    melodeus::audio::DecodedTrack got;
    REQUIRE(melodeus::audio::decode_audio_memory(bytes.data(), bytes.size(), "wav", &got, &err));
    CHECK(got.channels == 1);
    CHECK(got.sample_rate == doctest::Approx(44100.0));
    CHECK(got.frames == src.frames);
}

TEST_CASE("decoder rechaza basura y formatos desconocidos") {
    const std::uint8_t junk[] = {0x01, 0x02, 0x03, 0x04};
    melodeus::audio::DecodedTrack got;
    std::string err;
    CHECK_FALSE(melodeus::audio::decode_audio_memory(junk, sizeof(junk), "wav", &got, &err));
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK_FALSE(melodeus::audio::decode_audio_memory(junk, sizeof(junk), "xyz", &got, &err));
    CHECK_FALSE(err.empty());
}
