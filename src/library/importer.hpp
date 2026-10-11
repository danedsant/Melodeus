#pragma once

#include "analysis/key.hpp"
#include "library/database.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
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
/// key con los detectores de analysis/, y hace insert en `db`.
ImportResult import_file(Database& db, const std::filesystem::path& path);

} // namespace melodeus::library
