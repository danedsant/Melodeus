#include "streaming/stream_loader.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace melodeus::streaming {

namespace {

constexpr std::uint32_t kCacheMagic = 0x4353444D; // "MDSC"
constexpr std::uint32_t kCacheVersion = 1;

/// Formato de la caché: cabecera fija + samples f32 interleaved.
bool write_cache_file(const std::filesystem::path& path, const audio::DecodedTrack& track,
                      const std::string& title) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;
    }
    const std::uint32_t rate = static_cast<std::uint32_t>(track.sample_rate);
    const std::uint32_t channels = static_cast<std::uint32_t>(track.channels);
    const std::uint64_t frames = static_cast<std::uint64_t>(track.frames);
    const std::uint64_t title_len = title.size();
    f.write(reinterpret_cast<const char*>(&kCacheMagic), 4);
    f.write(reinterpret_cast<const char*>(&kCacheVersion), 4);
    f.write(reinterpret_cast<const char*>(&rate), 4);
    f.write(reinterpret_cast<const char*>(&channels), 4);
    f.write(reinterpret_cast<const char*>(&frames), 8);
    f.write(reinterpret_cast<const char*>(&title_len), 8);
    f.write(title.data(), static_cast<std::streamsize>(title_len));
    f.write(reinterpret_cast<const char*>(track.samples.data()),
            static_cast<std::streamsize>(track.samples.size() * sizeof(float)));
    return static_cast<bool>(f);
}

bool read_cache_file(const std::filesystem::path& path, audio::DecodedTrack* track,
                     std::string* title) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;
    std::uint64_t title_len = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&rate), 4);
    f.read(reinterpret_cast<char*>(&channels), 4);
    f.read(reinterpret_cast<char*>(&frames), 8);
    f.read(reinterpret_cast<char*>(&title_len), 8);
    if (!f || magic != kCacheMagic || version != kCacheVersion || title_len > (1u << 16) ||
        channels == 0 || frames == 0) {
        return false;
    }
    std::string t(static_cast<std::size_t>(title_len), '\0');
    f.read(t.data(), static_cast<std::streamsize>(title_len));
    const std::size_t samples = static_cast<std::size_t>(frames) * channels;
    track->samples.resize(samples);
    f.read(reinterpret_cast<char*>(track->samples.data()),
           static_cast<std::streamsize>(samples * sizeof(float)));
    if (!f) {
        return false;
    }
    track->frames = static_cast<std::int64_t>(frames);
    track->channels = static_cast<int>(channels);
    track->sample_rate = rate;
    if (title != nullptr) {
        *title = std::move(t);
    }
    return true;
}

} // namespace

StreamLoadResult load_url_blocking(DiskCache& cache, const YtDlpSource::ToolPaths& tools,
                                   const std::string& url, int timeout_ms) {
    StreamLoadResult result;

    // 1) Caché LRU.
    if (cache.contains(url)) {
        if (read_cache_file(cache.path_for(url), &result.track, &result.title)) {
            result.from_cache = true;
            return result;
        }
        cache.remove(url); // entrada corrupta
    }

    // 2) Descarga completa por el jitter buffer (reintento dentro de la fuente).
    YtDlpSource source(tools);
    if (!source.open(url.c_str())) {
        result.error = "no se pudo abrir la fuente";
        return result;
    }
    if (!source.pull_all(&result.track.samples, timeout_ms)) {
        source.stop();
        result.error = source.last_error()[0] != '\0' ? source.last_error() : "descarga fallida";
        return result;
    }
    source.stop();

    result.track.channels = YtDlpSource::kChannels;
    result.track.sample_rate = YtDlpSource::kSampleRate;
    result.track.frames =
        static_cast<std::int64_t>(result.track.samples.size()) / YtDlpSource::kChannels;
    result.title = source.title();
    if (result.title.empty()) {
        result.title = url;
    }
    if (result.track.frames <= 0) {
        result.error = "stream vacío";
        return result;
    }

    // 3) Commit en la caché (evicción LRU interna).
    const auto tmp = cache.path_for(url).string() + ".tmp";
    if (write_cache_file(tmp, result.track, result.title)) {
        cache.commit(url, tmp);
    }
    return result;
}

} // namespace melodeus::streaming
