#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace melodeus::library {

/// Metadatos de pista para la biblioteca (esquema Docs §7.2 + Camelot).
struct TrackMeta {
    std::int64_t track_id = 0;
    int source_type = 0; ///< 0=local 1=youtube 2=soundcloud 3=spotify(meta)
    std::string uri;     ///< ruta de archivo para fuentes locales
    std::string title;
    std::string artist;
    std::string album;
    double bpm = 0.0;
    int camelot = 0;            ///< 1..12 (0 = sin análisis)
    bool camelot_minor = false; ///< true = sufijo A (menor)
    std::string musical_key;    ///< nombre tradicional, p.ej. "Am"
    std::int64_t duration_ms = 0;
    int bitrate = 0;
    std::string format; ///< "wav"/"flac"/"mp3"
    std::int64_t file_size = 0;
};

/// Wrapper mínimo de SQLite (WAL, un escritor) para la biblioteca. El FTS5
/// usa content='tracks' con triggers de sincronía. No thread-safe por
/// objeto: cada hilo escritor abre su propio Database (WAL lo permite).
class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const char* path);
    void close();

    [[nodiscard]] bool ok() const {
        return db_ != nullptr;
    }

    /// Ruta con la que se abrió (para reabrir desde otro hilo).
    [[nodiscard]] const std::string& path() const {
        return path_;
    }

    /// Ejecuta SQL sin resultados (migraciones, BEGIN/COMMIT de tests).
    bool exec_sql(const char* sql);

    /// Inserta metadatos; devuelve el track_id o -1. BPM/key pueden ser 0.
    std::int64_t insert_track(const TrackMeta& meta);

    /// Actualiza el resultado de análisis (BPM + key) de una pista.
    bool update_analysis(std::int64_t track_id, double bpm, int camelot, bool camelot_minor,
                         const char* key_name);

    /// BLOB de análisis (Docs §7.2): kind 0=beatgrid 1=waveform 2=cues.
    /// `audio_sha1` es de 20 bytes; se invalida si cambia o cambia
    /// format_version.
    bool insert_analysis_blob(std::int64_t track_id, int kind, const std::uint8_t audio_sha1[20],
                              const void* data, std::size_t size);

    /// Búsqueda FTS5 (tokens AND). Consulta vacía = primeras N por título.
    std::vector<TrackMeta> search(const char* query, int limit = 200) const;

    [[nodiscard]] int count() const;

    [[nodiscard]] int blob_count() const;

private:
    void* db_ = nullptr; // sqlite3*
    std::string path_;
};

} // namespace melodeus::library
