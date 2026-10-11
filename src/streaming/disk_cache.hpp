#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace melodeus::streaming {

/// Caché LRU por peso en disco para bytes crudos de streams (Docs §F4).
///
/// Almacena el stream sin decodificar (opus/m4a/wbemp...) bajo un hash
/// SHA-1 de la URL, con índice en memoria: { bytes, last_used }. Al superar
/// `max_bytes` se elimina el menos usado. No es RT: mutex de usuario.
class DiskCache {
public:
    explicit DiskCache(std::filesystem::path dir, std::uint64_t max_bytes = 1ull << 30);

    [[nodiscard]] bool contains(const std::string& url) const;

    /// Ruta del fichero para `url` (no crea nada).
    [[nodiscard]] std::filesystem::path path_for(const std::string& url) const;

    /// Mueve `tmp_file` (renombra) a la caché con la clave de `url` y hace
    /// evicción LRU si hace falta. false si el rename falla.
    bool commit(const std::string& url, const std::filesystem::path& tmp_file);

    /// Elimina la entrada de `url` si existe.
    bool remove(const std::string& url);

    [[nodiscard]] std::uint64_t total_bytes() const;
    [[nodiscard]] std::uint64_t max_bytes() const {
        return max_bytes_;
    }

    void clear();

private:
    struct Entry {
        std::uint64_t bytes = 0;
        std::uint64_t last_used = 0;
    };

    void index_dir_locked();
    void evict_locked();
    static std::string key_for(const std::string& url);

    std::filesystem::path dir_;
    const std::uint64_t max_bytes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> index_; ///< clave sha1hex -> Entry
    std::uint64_t clock_ = 0;
    std::uint64_t total_bytes_ = 0;
};

} // namespace melodeus::streaming
