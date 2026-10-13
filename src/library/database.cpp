#include "library/database.hpp"
#include "library/blob.hpp"

#include <sqlite3.h>

#include <cctype>

namespace melodeus::library {

namespace {

/// Prepara una consulta FTS5 segura: cada token entre comillas dobles
/// (AND implícito). Devuelve cadena vacía si no hay tokens.
std::string sanitize_fts(const char* query) {
    std::string out;
    if (query == nullptr) {
        return out;
    }
    std::string token;
    const auto flush = [&]() {
        if (!token.empty()) {
            if (!out.empty()) {
                out += ' ';
            }
            out += '"';
            for (char c : token) {
                if (c == '"') {
                    out += "\"\"";
                } else {
                    out += c;
                }
            }
            out += '"';
            token.clear();
        }
    };
    for (const char* p = query; *p != '\0'; ++p) {
        const unsigned char uc = static_cast<unsigned char>(*p);
        if (std::isalnum(uc) || uc == '_' || *p == '\'' || (uc & 0x80) != 0) {
            token += *p;
        } else {
            flush();
        }
    }
    flush();
    return out;
}

TrackMeta row_to_meta(sqlite3_stmt* stmt) {
    TrackMeta m;
    m.track_id = sqlite3_column_int64(stmt, 0);
    m.source_type = sqlite3_column_int(stmt, 1);
    if (const unsigned char* uri = sqlite3_column_text(stmt, 2)) {
        m.uri = reinterpret_cast<const char*>(uri);
    }
    if (const unsigned char* title = sqlite3_column_text(stmt, 3)) {
        m.title = reinterpret_cast<const char*>(title);
    }
    if (const unsigned char* artist = sqlite3_column_text(stmt, 4)) {
        m.artist = reinterpret_cast<const char*>(artist);
    }
    if (const unsigned char* album = sqlite3_column_text(stmt, 5)) {
        m.album = reinterpret_cast<const char*>(album);
    }
    m.bpm = sqlite3_column_double(stmt, 6);
    if (const unsigned char* key = sqlite3_column_text(stmt, 7)) {
        m.musical_key = reinterpret_cast<const char*>(key);
    }
    m.duration_ms = sqlite3_column_int64(stmt, 8);
    m.bitrate = sqlite3_column_int(stmt, 9);
    if (const unsigned char* fmt = sqlite3_column_text(stmt, 10)) {
        m.format = reinterpret_cast<const char*>(fmt);
    }
    m.file_size = sqlite3_column_int64(stmt, 11);
    m.camelot = sqlite3_column_int(stmt, 13);
    m.camelot_minor = sqlite3_column_int(stmt, 14) != 0;
    return m;
}

constexpr const char* kSelectCols =
    "SELECT track_id, source_type, uri, title, artist, album, bpm, musical_key,"
    " duration_ms, bitrate, format, file_size, added_at, camelot, camelot_minor"
    " FROM tracks";

} // namespace

Database::~Database() {
    close();
}

bool Database::open(const char* path) {
    close();
    sqlite3* raw = nullptr;
    if (sqlite3_open(path, &raw) != SQLITE_OK) {
        if (raw != nullptr) {
            sqlite3_close(raw);
        }
        return false;
    }
    db_ = raw;
    path_ = (path != nullptr) ? path : "";
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    // Migración mínima por user_version (1 = esquema inicial F3).
    sqlite3_stmt* version_stmt = nullptr;
    int schema_version = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &version_stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(version_stmt) == SQLITE_ROW) {
        schema_version = sqlite3_column_int(version_stmt, 0);
    }
    if (version_stmt != nullptr) {
        sqlite3_finalize(version_stmt);
    }

    if (schema_version < 1) {
        const char* schema = R"SQL(
        CREATE TABLE tracks(
          track_id INTEGER PRIMARY KEY,
          source_type INTEGER NOT NULL,
          uri TEXT NOT NULL, title TEXT, artist TEXT, album TEXT,
          bpm REAL, musical_key TEXT, duration_ms INTEGER,
          bitrate INTEGER, format TEXT, audio_sha1 BLOB,
          file_size INTEGER, added_at INTEGER, last_played_at INTEGER,
          camelot INTEGER NOT NULL DEFAULT 0,
          camelot_minor INTEGER NOT NULL DEFAULT 0
        );
        CREATE VIRTUAL TABLE tracks_fts USING fts5(
          title, artist, album, content='tracks', content_rowid='track_id',
          tokenize = "unicode61 remove_diacritics 2"
        );
        CREATE TRIGGER tracks_ai AFTER INSERT ON tracks BEGIN
          INSERT INTO tracks_fts(rowid, title, artist, album)
          VALUES (new.track_id, new.title, new.artist, new.album);
        END;
        CREATE TRIGGER tracks_ad AFTER DELETE ON tracks BEGIN
          INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album)
          VALUES ('delete', old.track_id, old.title, old.artist, old.album);
        END;
        CREATE TRIGGER tracks_au AFTER UPDATE ON tracks BEGIN
          INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album)
          VALUES ('delete', old.track_id, old.title, old.artist, old.album);
          INSERT INTO tracks_fts(rowid, title, artist, album)
          VALUES (new.track_id, new.title, new.artist, new.album);
        END;
        PRAGMA user_version = 1;
    )SQL";
        char* err = nullptr;
        const int rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
        if (err != nullptr) {
            sqlite3_free(err);
        }
        if (rc != SQLITE_OK) {
            close();
            return false;
        }
    }
    // Migración v2: tabla de BLOBs de análisis (Docs §7.2).
    if (!exec_sql("CREATE TABLE IF NOT EXISTS analysis_blobs("
                  " track_id INTEGER PRIMARY KEY,"
                  " format_version INTEGER NOT NULL,"
                  " audio_sha1 BLOB NOT NULL,"
                  " kind INTEGER NOT NULL,"
                  " data BLOB NOT NULL);") ||
        !exec_sql("PRAGMA user_version = 2;")) {
        close();
        return false;
    }
    return true;
}

void Database::close() {
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
    path_.clear();
}

bool Database::exec_sql(const char* sql) {
    if (db_ == nullptr || sql == nullptr) {
        return false;
    }
    char* err = nullptr;
    const int rc = sqlite3_exec(static_cast<sqlite3*>(db_), sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

std::int64_t Database::insert_track(const TrackMeta& meta) {
    if (db_ == nullptr) {
        return -1;
    }
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tracks(source_type, uri, title, artist, album, bpm, musical_key,"
                      " duration_ms, bitrate, format, file_size, added_at, camelot, camelot_minor)"
                      " VALUES (?,?,?,?,?,?,?,?,?,?,?,strftime('%s','now'),?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, meta.source_type);
    sqlite3_bind_text(stmt, 2, meta.uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, meta.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, meta.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, meta.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, meta.bpm);
    sqlite3_bind_text(stmt, 7, meta.musical_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, meta.duration_ms);
    sqlite3_bind_int(stmt, 9, meta.bitrate);
    sqlite3_bind_text(stmt, 10, meta.format.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 11, meta.file_size);
    sqlite3_bind_int(stmt, 12, meta.camelot);
    sqlite3_bind_int(stmt, 13, meta.camelot_minor ? 1 : 0);
    std::int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = sqlite3_last_insert_rowid(db);
    }
    sqlite3_finalize(stmt);
    return id;
}

bool Database::delete_track(std::int64_t track_id) {
    if (db_ == nullptr || track_id <= 0) {
        return false;
    }
    sqlite3* db = static_cast<sqlite3*>(db_);
    // Eliminar blobs de análisis asociados a la pista
    sqlite3_stmt* blob_stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM analysis_blobs WHERE track_id = ?", -1, &blob_stmt,
                           nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(blob_stmt, 1, track_id);
        sqlite3_step(blob_stmt);
        sqlite3_finalize(blob_stmt);
    }
    // Eliminar registro de tracks (el trigger tracks_ad actualiza tracks_fts automáticamente)
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM tracks WHERE track_id = ?", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, track_id);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::update_analysis(std::int64_t track_id, double bpm, int camelot, bool camelot_minor,
                               const char* key_name) {
    if (db_ == nullptr) {
        return false;
    }
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE tracks SET bpm=?, camelot=?, camelot_minor=?, musical_key=?"
                      " WHERE track_id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_double(stmt, 1, bpm);
    sqlite3_bind_int(stmt, 2, camelot);
    sqlite3_bind_int(stmt, 3, camelot_minor ? 1 : 0);
    sqlite3_bind_text(stmt, 4, (key_name != nullptr) ? key_name : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, track_id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<TrackMeta> Database::search(const char* query, int limit, int source_type) const {
    std::vector<TrackMeta> out;
    if (db_ == nullptr || limit <= 0) {
        return out;
    }
    sqlite3* db = static_cast<sqlite3*>(db_);
    const std::string fts = sanitize_fts(query);
    sqlite3_stmt* stmt = nullptr;
    if (fts.empty()) {
        std::string sql = std::string(kSelectCols);
        if (source_type >= 0) {
            sql += " WHERE source_type = ? ORDER BY title LIMIT ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return out;
            }
            sqlite3_bind_int(stmt, 1, source_type);
            sqlite3_bind_int(stmt, 2, limit);
        } else {
            sql += " ORDER BY title LIMIT ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return out;
            }
            sqlite3_bind_int(stmt, 1, limit);
        }
    } else {
        // JOIN con la tabla FTS (no subquery): `rank` sólo existe con la
        // tabla FTS en el FROM de la consulta principal.
        std::string sql =
            "SELECT t.track_id, t.source_type, t.uri, t.title, t.artist, t.album, t.bpm,"
            " t.musical_key, t.duration_ms, t.bitrate, t.format, t.file_size, t.added_at,"
            " t.camelot, t.camelot_minor"
            " FROM tracks_fts f JOIN tracks t ON t.track_id = f.rowid"
            " WHERE tracks_fts MATCH ?";
        if (source_type >= 0) {
            sql += " AND t.source_type = ? ORDER BY rank LIMIT ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return out;
            }
            sqlite3_bind_text(stmt, 1, fts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, source_type);
            sqlite3_bind_int(stmt, 3, limit);
        } else {
            sql += " ORDER BY rank LIMIT ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return out;
            }
            sqlite3_bind_text(stmt, 1, fts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit);
        }
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(row_to_meta(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

int Database::count() const {
    if (db_ == nullptr) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), "SELECT COUNT(*) FROM tracks;", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

int Database::count_source(int source_type) const {
    if (db_ == nullptr) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_),
                           "SELECT COUNT(*) FROM tracks WHERE source_type = ?;", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, source_type);
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

bool Database::insert_analysis_blob(std::int64_t track_id, int kind,
                                    const std::uint8_t audio_sha1[20], const void* data,
                                    std::size_t size) {
    if (db_ == nullptr || audio_sha1 == nullptr || data == nullptr || size == 0) {
        return false;
    }
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO analysis_blobs(track_id, format_version, audio_sha1, kind, data)"
        " VALUES (?, 1, ?, ?, ?)"
        " ON CONFLICT(track_id) DO UPDATE SET format_version=1, audio_sha1=excluded.audio_sha1,"
        " kind=excluded.kind, data=excluded.data";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, track_id);
    sqlite3_bind_blob(stmt, 2, audio_sha1, 20, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, kind);
    sqlite3_bind_blob(stmt, 4, data, static_cast<int>(size), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int Database::blob_count() const {
    if (db_ == nullptr) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), "SELECT COUNT(*) FROM analysis_blobs;", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

bool Database::get_beatgrid(std::int64_t track_id, analysis::BeatGrid* out) const {
    if (db_ == nullptr || track_id <= 0 || out == nullptr) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT data FROM analysis_blobs WHERE track_id = ? AND kind = 0";
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, track_id);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int bytes = sqlite3_column_bytes(stmt, 0);
        if (blob != nullptr && bytes >= 16) {
            ok = unpack_beatgrid(static_cast<const std::uint8_t*>(blob), static_cast<std::size_t>(bytes), out);
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace melodeus::library
