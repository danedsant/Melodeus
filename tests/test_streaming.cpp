#include <doctest.h>

#include "audio/decoder.hpp"
#include "streaming/disk_cache.hpp"
#include "streaming/i_stream_source.hpp"
#include "streaming/jitter_buffer.hpp"
#include "streaming/stream_loader.hpp"
#include "streaming/stream_search.hpp"
#include "streaming/ytdlp_source.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path ffmpeg_path() {
    return fs::path(MELODEUS_EXTERNAL_DIR) / "ffmpeg" / "bin" / "ffmpeg.exe";
}

fs::path ytdlp_path() {
    return fs::path(MELODEUS_EXTERNAL_DIR) / "yt-dlp" / "yt-dlp.exe";
}

bool have_ffmpeg() {
    std::error_code ec;
    return fs::exists(ffmpeg_path(), ec);
}

/// WAV mono 1 s @ 48 kHz para el smoke offline de ffmpeg.
fs::path write_short_wav(const char* name) {
    melodeus::audio::DecodedTrack t;
    t.sample_rate = 48000.0;
    t.channels = 1;
    t.frames = 48000;
    t.samples.resize(48000);
    for (std::int64_t i = 0; i < t.frames; ++i) {
        t.samples[static_cast<std::size_t>(i)] =
            0.25f * static_cast<float>(
                        std::sin(6.283185307179586 * 440.0 * static_cast<double>(i) / 48000.0));
    }
    const fs::path out = fs::temp_directory_path() / name;
    std::string err;
    if (!melodeus::audio::write_wav_file(out.string(), t, &err)) {
        return {};
    }
    return out;
}

/// Carpeta temporal única para la caché del test.
struct TempCacheDir {
    fs::path path;
    explicit TempCacheDir(const char* name) {
        path = fs::temp_directory_path() / (std::string("melodeus_cache_") + name);
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ~TempCacheDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("JitterBuffer: prefill gatea el arranque") {
    melodeus::streaming::JitterBuffer jb(/*prefill_frames=*/4, /*channels=*/2);
    float out[64];
    CHECK(jb.read(out, 8) == 0); // aún sin prefill
    CHECK(jb.underruns() >= 1);

    const float in[8] = {1, 1, 2, 2, 3, 3, 4, 4}; // 4 frames
    CHECK(jb.push(in, 4) == 4);
    CHECK(jb.read(out, 8) == 4); // prefill cumplido: arranca
    CHECK(out[0] == 1.0f);
    CHECK(out[6] == 4.0f);
}

TEST_CASE("JitterBuffer: finish permite drenar sin prefill (clip corto)") {
    melodeus::streaming::JitterBuffer jb(/*prefill_frames=*/1000, /*channels=*/2);
    const float in[6] = {0.5f, 0.5f, 0.6f, 0.6f, 0.7f, 0.7f};
    CHECK(jb.push(in, 3) == 3);
    float out[16];
    CHECK(jb.read(out, 8) == 0); // prefill no cumplido
    jb.finish();
    CHECK(jb.read(out, 8) == 3); // pero ya se puede drenar
}

TEST_CASE("JitterBuffer: overflow cuenta frames perdidos") {
    // Ring mínimo: 2 floats por channel... capacidad fija grande; llenamos
    // hasta desbordar el ring (1<<19 floats).
    melodeus::streaming::JitterBuffer jb(/*prefill_frames=*/1, /*channels=*/2);
    std::vector<float> big(melodeus::streaming::JitterBuffer::kCapacity + 1024, 0.1f);
    const int frames = static_cast<int>(big.size() / 2);
    int pushed = 0;
    // Push en trozos para no quedarnos en el Sleep del productor.
    int off = 0;
    while (off < frames) {
        const int n = jb.push(big.data() + static_cast<std::size_t>(off) * 2, frames - off);
        REQUIRE(n > 0);
        off += n;
        pushed += n;
        if (jb.overflows() > 0) {
            break;
        }
    }
    CHECK(jb.overflows() > 0);
    CHECK(pushed <= frames);
}

TEST_CASE("DiskCache: roundtrip, contiene y LRU por peso") {
    TempCacheDir tmp("lru");
    melodeus::streaming::DiskCache cache(tmp.path, /*max_bytes=*/2048);

    auto make_tmp = [&](const char* name, std::size_t bytes) {
        const fs::path p = tmp.path / name;
        std::ofstream f(p, std::ios::binary);
        std::vector<char> data(bytes, 'x');
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return p;
    };

    const std::string url_a = "https://example.com/a";
    const std::string url_b = "https://example.com/b";
    const std::string url_c = "https://example.com/c";

    CHECK(cache.commit(url_a, make_tmp("a.tmp", 1000)));
    CHECK(cache.contains(url_a));
    CHECK(cache.total_bytes() == 1000);

    CHECK(cache.commit(url_b, make_tmp("b.tmp", 1000)));
    CHECK(cache.total_bytes() == 2000);

    // Toca A (más reciente) y añade C: se debe ir B (LRU).
    CHECK(cache.commit(url_a, make_tmp("a2.tmp", 1000))); // re-commit actualiza
    CHECK(cache.commit(url_c, make_tmp("c.tmp", 1000)));
    CHECK(cache.total_bytes() <= 2048);
    CHECK(cache.contains(url_a));
    CHECK(cache.contains(url_c));
    CHECK_FALSE(cache.contains(url_b)); // evictado

    // Persistencia: una instancia nueva re-indexa el directorio.
    melodeus::streaming::DiskCache cache2(tmp.path, 2048);
    CHECK(cache2.contains(url_a));
    CHECK(cache2.total_bytes() == cache.total_bytes());

    CHECK(cache2.remove(url_a));
    CHECK_FALSE(cache2.contains(url_a));
}

TEST_CASE("YtDlpSource: decode local por ffmpeg (smoke offline)") {
    if (!have_ffmpeg()) {
        MESSAGE("ffmpeg no está en external/; smoke omitido");
        return;
    }
    const fs::path wav = write_short_wav("melodeus_stream_short.wav");
    REQUIRE(!wav.empty());

    melodeus::streaming::YtDlpSource::ToolPaths tools;
    tools.ffmpeg = ffmpeg_path();
    melodeus::streaming::YtDlpSource source(tools, /*prefill_frames=*/48000);
    REQUIRE(source.open(wav.string().c_str()));

    std::vector<float> samples;
    const bool ok = source.pull_all(&samples, 30000);
    REQUIRE(ok);
    // pull_all sólo devuelve true al llegar a Ended.
    CHECK(source.state() == melodeus::streaming::StreamState::Ended);
    source.stop();
    CHECK(source.state() == melodeus::streaming::StreamState::Idle);
    // ffmpeg resamplea/remezcla a estéreo 48 kHz: 1 s ± tolerancia de
    // priming del WAV (hasta ~64 ms).
    const std::int64_t frames = static_cast<std::int64_t>(samples.size()) / 2;
    CHECK(frames >= 45000);
    CHECK(frames <= 49000);

    std::error_code ec;
    fs::remove(wav, ec);
}

TEST_CASE("stream_loader: fichero local + hit de caché") {
    if (!have_ffmpeg()) {
        MESSAGE("ffmpeg no está en external/; smoke omitido");
        return;
    }
    const fs::path wav = write_short_wav("melodeus_stream_load.wav");
    REQUIRE(!wav.empty());

    TempCacheDir tmp("load");
    melodeus::streaming::DiskCache cache(tmp.path, 1ull << 20);
    melodeus::streaming::YtDlpSource::ToolPaths tools;
    tools.ffmpeg = ffmpeg_path();

    const std::string url = wav.string(); // sin esquema = fichero local
    const auto r1 = melodeus::streaming::load_url_blocking(cache, tools, url, 30000);
    INFO(r1.error);
    REQUIRE(r1.ok());
    CHECK_FALSE(r1.from_cache);
    CHECK(r1.track.channels == 2);
    CHECK(r1.track.sample_rate == doctest::Approx(48000.0));
    CHECK(r1.track.frames > 40000);
    CHECK(cache.contains(url));

    // Segunda carga: desde la caché (sin tocar ffmpeg).
    const auto r2 = melodeus::streaming::load_url_blocking(cache, tools, url, 30000);
    REQUIRE(r2.ok());
    CHECK(r2.from_cache);
    CHECK(r2.track.frames == r1.track.frames);
    CHECK(r2.title == r1.title);

    std::error_code ec;
    fs::remove(wav, ec);
}

TEST_CASE("Streaming: parse_yt_json_line (YouTube & SoundCloud JSON Lines)") {
    // YouTube sample
    const std::string yt_line =
        R"({"_type": "url", "id": "2kpAzC2Mja8", "url": "https://www.youtube.com/watch?v=2kpAzC2Mja8", "title": "Martin Garrix - Animals", "duration": 202.5, "uploader": "Spinnin Records"})";

    melodeus::streaming::OnlineTrackResult res_yt;
    REQUIRE(melodeus::streaming::parse_yt_json_line(yt_line, &res_yt));
    CHECK(res_yt.id == "2kpAzC2Mja8");
    CHECK(res_yt.title == "Martin Garrix - Animals");
    CHECK(res_yt.uploader == "Spinnin Records");
    CHECK(res_yt.duration == doctest::Approx(202.5));
    CHECK(res_yt.url == "https://www.youtube.com/watch?v=2kpAzC2Mja8");
    CHECK_FALSE(res_yt.already_downloaded);

    // SoundCloud sample
    const std::string sc_line =
        R"({"uploader": "Skrillex", "title": "First Of The Year", "duration": 261.5, "webpage_url": "https://soundcloud.com/skrillex/first-of-the-year", "id": "21792829"})";

    melodeus::streaming::OnlineTrackResult res_sc;
    REQUIRE(melodeus::streaming::parse_yt_json_line(sc_line, &res_sc));
    CHECK(res_sc.id == "21792829");
    CHECK(res_sc.title == "First Of The Year");
    CHECK(res_sc.uploader == "Skrillex");
    CHECK(res_sc.duration == doctest::Approx(261.5));
    CHECK(res_sc.url == "https://soundcloud.com/skrillex/first-of-the-year");

    // Invalid line
    melodeus::streaming::OnlineTrackResult bad;
    CHECK_FALSE(melodeus::streaming::parse_yt_json_line("{}", &bad));
    CHECK_FALSE(melodeus::streaming::parse_yt_json_line("", &bad));
}
