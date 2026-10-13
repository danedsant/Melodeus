#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace melodeus::streaming {

enum class SearchPlatform : int {
    YouTube = 1,
    SoundCloud = 2,
};

struct OnlineTrackResult {
    std::string id;
    std::string title;
    std::string uploader;
    std::string url;
    double duration = 0.0;
    bool already_downloaded = false;
    SearchPlatform platform = SearchPlatform::YouTube;
};

/// Parser helper: extrae campos simples de un JSON line emitido por yt-dlp.
bool parse_yt_json_line(const std::string& line, OnlineTrackResult* out);

/// Servicio de búsqueda online asíncrono para YouTube y SoundCloud vía yt-dlp.
class StreamSearchService {
public:
    StreamSearchService();
    ~StreamSearchService();

    StreamSearchService(const StreamSearchService&) = delete;
    StreamSearchService& operator=(const StreamSearchService&) = delete;

    /// Inicia la búsqueda asíncrona. Si ya había una en curso, la cancela.
    void start_search(const std::filesystem::path& ytdlp_bin,
                      SearchPlatform platform,
                      const std::string& query,
                      int max_results = 15);

    /// Cancela la búsqueda actual si está corriendo.
    void cancel();

    [[nodiscard]] bool is_searching() const;
    [[nodiscard]] std::vector<OnlineTrackResult> get_results() const;
    [[nodiscard]] std::string get_error() const;
    [[nodiscard]] SearchPlatform current_platform() const;
    [[nodiscard]] std::string current_query() const;

    /// Actualiza el estado already_downloaded en los resultados existentes.
    void update_downloaded_status(
        const std::function<bool(const std::string& id, const std::string& title)>& is_downloaded_fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace melodeus::streaming
