#pragma once

#include "streaming/i_stream_source.hpp"
#include "streaming/jitter_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace melodeus::streaming {

/// Fuente YouTube/SoundCloud tras `IStreamSource`: lanza `yt-dlp -o -` |
/// `ffmpeg -f f32le` por pipes en un hilo propio, empuja al jitter buffer y
/// reintenta automáticamente hasta `kMaxAttempts` si el stream muere antes
/// de tiempo (DoD F4). Los binarios se inyectan (external/) para no fijar
/// rutas en la librería.
class YtDlpSource final : public IStreamSource {
public:
    struct ToolPaths {
        std::filesystem::path ytdlp;  ///< external/yt-dlp/yt-dlp.exe
        std::filesystem::path ffmpeg; ///< external/ffmpeg/bin/ffmpeg.exe
    };

    static constexpr int kMaxAttempts = 3;
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kChannels = 2;

    explicit YtDlpSource(ToolPaths tools, int prefill_frames = JitterBuffer::kDefaultPrefillFrames);
    ~YtDlpSource() override;

    bool open(const char* url) override;
    void stop() override;
    int read(float* out, int frames) override;
    [[nodiscard]] StreamState state() const override {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] double sample_rate() const override {
        return kSampleRate;
    }
    [[nodiscard]] int channels() const override {
        return kChannels;
    }
    [[nodiscard]] const char* title() const override {
        return title_.c_str();
    }
    [[nodiscard]] const char* last_error() const override {
        return error_.c_str();
    }

    /// Descarga completa a memoria (sin pasar por el jitter buffer): útil
    /// para el camino de carga a deck (stream_loader) y para tests offline
    /// con ficheros locales. `timeout_ms` aborta con Error.
    bool pull_all(std::vector<float>* out, int timeout_ms);

    /// Consulta el título con `yt-dlp --print title --skip-download`.
    bool query_title(const std::string& url, std::string* out, int timeout_ms);

private:
    void worker(std::string url);
    /// Una sesión de descarga (proceso + lectura al jitter buffer).
    /// Devuelve los frames empujados; `*died_early` si el EOF fue dudoso.
    std::int64_t run_session(const std::string& url, bool http, bool* died_early);

    ToolPaths tools_;
    JitterBuffer jb_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<std::int64_t> pushed_frames_{0};
    std::atomic<std::int64_t> duration_frames_{-1}; ///< -1 = desconocido
    mutable std::mutex text_mutex_;
    std::string title_;
    std::string error_;
    void* job_ = nullptr; // HANDLE del job de los hijos (para stop())
};

} // namespace melodeus::streaming
