#pragma once

namespace melodeus::streaming {

/// Estado de una fuente de streaming.
enum class StreamState {
    Idle,      ///< sin open()
    Connecting ///< proceso yt-dlp/ffmpeg lanzado, aún sin datos
    ,
    Buffering, ///< llegando datos; aún por debajo del prefill
    Playing,   ///< prefill cumplido; el consumidor puede leer sin glitch
    Ended,     ///< stream terminado y drenado
    Error,     ///< fallo irrecuperable (reintentos agotados)
};

/// Fuente de audio remota (o fichero vía ffmpeg) detrás de un interface
/// estable: el resto de la app no ve yt-dlp ni ffmpeg (Docs §6, F4).
///
/// Modelo de uso: `open(url)` lanza un hilo de descarga que empuja samples
/// f32 interleaved a un jitter buffer interno; el consumidor (hilo de carga
/// o de audio, según el caso) llama a `read()` y observa `state()`.
class IStreamSource {
public:
    virtual ~IStreamSource() = default;

    IStreamSource(const IStreamSource&) = delete;
    IStreamSource& operator=(const IStreamSource&) = delete;

    /// Lanza la descarga en background. false si la URL es inválida o ya
    /// hay una sesión activa.
    virtual bool open(const char* url) = 0;

    /// Detiene la sesión (mata los procesos hijos) y drena el buffer.
    /// Bloquea hasta que el hilo de descarga termine.
    virtual void stop() = 0;

    /// Extrae hasta `frames` frames (channels muestras interleaved por
    /// frame). Devuelve 0 si no hay datos listos (underrun o prefill).
    virtual int read(float* out, int frames) = 0;

    [[nodiscard]] virtual StreamState state() const = 0;

    [[nodiscard]] virtual double sample_rate() const = 0;
    [[nodiscard]] virtual int channels() const = 0;

    /// Título del stream (best effort: yt-dlp --print title; URL si falla).
    [[nodiscard]] virtual const char* title() const = 0;

    [[nodiscard]] virtual const char* last_error() const = 0;

protected:
    IStreamSource() = default;
};

} // namespace melodeus::streaming
