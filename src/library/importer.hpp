#pragma once

#include "analysis/key.hpp"
#include "library/database.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace melodeus::library {

/// Resultado de importar un archivo (metadatos + análisis).
struct ImportResult {
    std::int64_t track_id = -1;
    double bpm = 0.0;
    analysis::KeyInfo key;
    std::string error; ///< vacío si ok
};

/// Escanea una carpeta (no recursivo por defecto) y devuelve las rutas de
/// audio soportadas (.wav/.flac/.mp3).
std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& dir,
                                               bool recursive = false);

/// Importa un archivo a la base: decodifica (dr_* / minimp3), analiza BPM y
/// key, escribe el BLOB del beatgrid (kind=0) y hace insert en `db`.
ImportResult import_file(Database& db, const std::filesystem::path& path);

/// Importador en background: abre su propia conexión (WAL) e importa una
/// carpeta mientras la UI sigue a 144 fps. Progreso vía atomics.
class ImportWorker {
public:
    ImportWorker() = default;
    ~ImportWorker();

    ImportWorker(const ImportWorker&) = delete;
    ImportWorker& operator=(const ImportWorker&) = delete;

    /// Lanza el hilo (no-ops si ya corre). `db_path` + carpeta a importar.
    void start(const std::filesystem::path& db_path, const std::filesystem::path& folder,
               bool recursive = false);

    /// Bloquea hasta terminar el hilo (si corre).
    void join();

    [[nodiscard]] bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int done() const {
        return done_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int total() const {
        return total_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int imported() const {
        return imported_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const std::string& last_error() const {
        return last_error_; // sólo tras join()
    }

private:
    void run(const std::filesystem::path db_path, const std::filesystem::path folder,
             bool recursive);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};
    std::atomic<int> imported_{0};
    std::string last_error_;
};

} // namespace melodeus::library
