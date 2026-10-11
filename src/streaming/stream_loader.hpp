#pragma once

#include "audio/decoder.hpp"
#include "streaming/disk_cache.hpp"
#include "streaming/ytdlp_source.hpp"

#include <string>

namespace melodeus::streaming {

/// Carga una URL (YT/SC/fichero) a un DecodedTrack listo para el deck:
/// caché LRU → descarga completa vía YtDlpSource → DecodedTrack.
/// Bloqueante: llamar desde un hilo de UI-spawn (nunca del hilo de audio).
struct StreamLoadResult {
    audio::DecodedTrack track;
    std::string title;
    std::string error;
    bool from_cache = false;

    [[nodiscard]] bool ok() const {
        return error.empty() && track.frames > 0;
    }
};

StreamLoadResult load_url_blocking(DiskCache& cache, const YtDlpSource::ToolPaths& tools,
                                   const std::string& url, int timeout_ms = 15 * 60 * 1000);

} // namespace melodeus::streaming
