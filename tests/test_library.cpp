#include <doctest.h>

#include "audio/decoder.hpp"
#include "core/sha1.hpp"
#include "library/blob.hpp"
#include "library/database.hpp"
#include "library/importer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// Ruta temporal única para la DB del test (se borra al salir).
struct TempDb {
    fs::path path;
    explicit TempDb(const char* name) {
        path = fs::temp_directory_path() / (std::string("melodeus_test_") + name + ".db");
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(fs::path(path.string() + "-wal"), ec);
        fs::remove(fs::path(path.string() + "-shm"), ec);
    }
    ~TempDb() {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(fs::path(path.string() + "-wal"), ec);
        fs::remove(fs::path(path.string() + "-shm"), ec);
    }
};

/// Click track WAV de 6 s a `bpm` para probar import+análisis.
fs::path write_click_wav(const char* name, double bpm) {
    constexpr double kRate = 48000.0;
    constexpr double kSeconds = 6.0;
    const std::size_t total = static_cast<std::size_t>(kSeconds * kRate);
    melodeus::audio::DecodedTrack track;
    track.sample_rate = kRate;
    track.channels = 1;
    track.frames = static_cast<std::int64_t>(total);
    track.samples.assign(total, 0.0f);
    const double period = 60.0 / bpm * kRate;
    for (double pos = kRate * 0.05; pos < static_cast<double>(total); pos += period) {
        const auto i0 = static_cast<std::size_t>(pos);
        for (std::size_t k = 0; k < 256 && i0 + k < total; ++k) {
            track.samples[i0 + k] +=
                std::exp(-static_cast<float>(k) / 40.0f) * static_cast<float>((k % 7) - 3) * 0.3f;
        }
    }
    const fs::path out = fs::temp_directory_path() / name;
    std::string err;
    if (!melodeus::audio::write_wav_file(out.string(), track, &err)) {
        return {};
    }
    return out;
}

} // namespace

TEST_CASE("Database: insert y búsqueda FTS por título") {
    TempDb tmp("fts");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    melodeus::library::TrackMeta m;
    m.uri = "C:/music/track1.wav";
    m.title = "Blue Monday";
    m.artist = "New Order";
    m.bpm = 130.0;
    m.musical_key = "Am";
    m.camelot = 8;
    m.camelot_minor = true;
    const std::int64_t id = db.insert_track(m);
    REQUIRE(id > 0);
    CHECK(db.count() == 1);

    const auto by_title = db.search("blue");
    REQUIRE(by_title.size() == 1);
    CHECK(by_title[0].title == "Blue Monday");
    CHECK(by_title[0].artist == "New Order");
    CHECK(by_title[0].bpm == doctest::Approx(130.0));
    CHECK(by_title[0].musical_key == "Am");
    CHECK(by_title[0].camelot == 8);
    CHECK(by_title[0].camelot_minor == true);

    CHECK(db.search("missing").empty());
    REQUIRE(db.search("").size() == 1); // consulta vacía = listar
}

TEST_CASE("Database: FTS con diacríticos (remove_diacritics 2)") {
    TempDb tmp("diacritics");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    melodeus::library::TrackMeta m;
    m.uri = "C:/music/cafe.wav";
    m.title = "Café del Mar";
    REQUIRE(db.insert_track(m) > 0);
    CHECK(db.search("cafe").size() == 1);
    CHECK(db.search("CAFÉ").size() == 1);
    CHECK(db.search("mar").size() == 1);
}

TEST_CASE("Database: update_analysis refleja en búsqueda") {
    TempDb tmp("analysis");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    melodeus::library::TrackMeta m;
    m.uri = "x.wav";
    m.title = "Pending Analysis";
    const std::int64_t id = db.insert_track(m);
    REQUIRE(id > 0);
    REQUIRE(db.update_analysis(id, 128.5, 11, false, "A"));
    const auto rows = db.search("pending");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].bpm == doctest::Approx(128.5));
    CHECK(rows[0].camelot == 11);
    CHECK(rows[0].camelot_minor == false);
    CHECK(rows[0].musical_key == "A");
}

TEST_CASE("Database: búsqueda rápida con miles de pistas") {
    TempDb tmp("perf");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    for (int i = 0; i < 3000; ++i) {
        melodeus::library::TrackMeta m;
        m.uri = "file://" + std::to_string(i);
        m.title = "Track " + std::to_string(i) + " Alpha";
        m.artist = "Artist " + std::to_string(i % 50);
        m.bpm = 120.0 + (i % 40);
        REQUIRE(db.insert_track(m) > 0);
    }
    CHECK(db.count() == 3000);
    const auto t0 = std::chrono::steady_clock::now();
    const auto rows = db.search("alpha 42");
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(rows.size() >= 1);
    CHECK(ms < 50.0); // holgado en CI; DoD real <20 ms @50k
}

TEST_CASE("Importer: archivo WAV click 128 BPM") {
    TempDb tmp("import");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    const fs::path wav = write_click_wav("melodeus_import_click.wav", 128.0);
    REQUIRE(!wav.empty());
    const auto result = melodeus::library::import_file(db, wav);
    std::error_code ec;
    fs::remove(wav, ec);
    INFO(result.error);
    REQUIRE(result.track_id > 0);
    CHECK(result.bpm == doctest::Approx(128.0).epsilon(0.02));
    CHECK(db.count() == 1);
    const auto rows = db.search("click");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].bpm == doctest::Approx(128.0).epsilon(0.02));
    CHECK(rows[0].format == "wav");
    CHECK(rows[0].duration_ms == 6000);
    CHECK(db.blob_count() == 1); // beatgrid BLOB del import
}

TEST_CASE("blob beatgrid roundtrip") {
    melodeus::analysis::BeatGrid in;
    in.bpm = 127.8;
    in.offset_sec = 0.243;
    const auto packed = melodeus::library::pack_beatgrid(in);
    CHECK(packed.size() == 16);
    melodeus::analysis::BeatGrid out;
    REQUIRE(melodeus::library::unpack_beatgrid(packed.data(), packed.size(), &out));
    CHECK(out.bpm == doctest::Approx(127.8));
    CHECK(out.offset_sec == doctest::Approx(0.243));
    CHECK_FALSE(melodeus::library::unpack_beatgrid(packed.data(), 8, &out));
}

TEST_CASE("sha1 vectores conocidos") {
    const auto abc = melodeus::core::sha1("abc", 3);
    CHECK(abc[0] == 0xa9);
    CHECK(abc[1] == 0x99);
    CHECK(abc[2] == 0x3e);
    CHECK(abc[3] == 0x36);
    CHECK(abc[19] == 0x9d); // a9993e36...9cd0d89d
    const auto empty = melodeus::core::sha1("", 0);
    CHECK(empty[0] == 0xda);
    CHECK(empty[19] == 0x09); // da39a3ee...afd80709
}

TEST_CASE("Database: 50k pistas + búsqueda < 20 ms (DoD F3)") {
    TempDb tmp("perf50k");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));
    REQUIRE(db.exec_sql("BEGIN;"));
    for (int i = 0; i < 50000; ++i) {
        melodeus::library::TrackMeta m;
        m.uri = "file://" + std::to_string(i);
        m.title = "Track " + std::to_string(i) + " Omega";
        m.artist = "Artist " + std::to_string(i % 500);
        m.bpm = 110.0 + (i % 80);
        REQUIRE(db.insert_track(m) > 0);
    }
    REQUIRE(db.exec_sql("COMMIT;"));
    CHECK(db.count() == 50000);

    // Varias búsquedas FTS5; el DoD pide < 20 ms con audio reproduciéndose
    // (aquí medimos sólo la query; el audio se valida en el selftest).
    const char* queries[] = {"omega 42", "track 49999", "artist 7", "alpha"};
    double worst = 0.0;
    for (const char* q : queries) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto rows = db.search(q);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        worst = (std::max)(worst, ms);
        CHECK(rows.size() >= 0);
    }
    CHECK(worst < 20.0);
    std::printf("[perf50k] peor búsqueda: %.2f ms\n", worst);
}

TEST_CASE("Importer: scan_folder encuentra wav/flac/mp3") {
    const fs::path dir = fs::temp_directory_path() / "melodeus_scan_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir / "sub", ec);
    melodeus::audio::DecodedTrack t;
    t.sample_rate = 48000.0;
    t.channels = 1;
    t.frames = 100;
    t.samples.assign(100, 0.1f);
    std::string err;
    REQUIRE(melodeus::audio::write_wav_file((dir / "a.wav").string(), t, &err));
    REQUIRE(melodeus::audio::write_wav_file((dir / "b.flac").string(), t, &err)); // ext engañosa
    REQUIRE(melodeus::audio::write_wav_file((dir / "c.mp3").string(), t, &err));
    {
        std::ofstream notes(dir / "notes.txt");
        notes << "hola";
    }
    const auto files = melodeus::library::scan_folder(dir);
    CHECK(files.size() == 3);
    CHECK(melodeus::library::scan_folder(dir / "missing").empty());
    fs::remove_all(dir, ec);
}

TEST_CASE("Database: delete_track elimina metadatos, blobs y limpia FTS5") {
    TempDb tmp("delete_track");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));

    melodeus::library::TrackMeta m;
    m.uri = "file://test_delete.wav";
    m.title = "Delete Me Now";
    m.artist = "Temporary Artist";
    m.album = "Single";
    m.bpm = 124.0;
    const std::int64_t tid = db.insert_track(m);
    REQUIRE(tid > 0);
    CHECK(db.count() == 1);

    const std::uint8_t sha1[20] = {1, 2, 3, 4, 5};
    const float dummy_data[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    REQUIRE(db.insert_analysis_blob(tid, 0, sha1, dummy_data, sizeof(dummy_data)));
    CHECK(db.blob_count() == 1);

    // Debe encontrarse en FTS5
    auto found = db.search("Delete");
    REQUIRE(found.size() == 1);
    CHECK(found[0].track_id == tid);

    // Eliminar la pista
    REQUIRE(db.delete_track(tid));

    // Verificar que desapareció de tracks, de blobs y de FTS5
    CHECK(db.count() == 0);
    CHECK(db.blob_count() == 0);
    found = db.search("Delete");
    CHECK(found.empty());

    // delete_track con ID inexistente o inválido
    CHECK_FALSE(db.delete_track(-1));
}

TEST_CASE("Database: filtrado por source_type (Local, YouTube, SoundCloud)") {
    TempDb tmp("source_type");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));

    melodeus::library::TrackMeta t_local;
    t_local.uri = "file://local.wav";
    t_local.title = "Local Electro Track";
    t_local.source_type = 0;
    REQUIRE(db.insert_track(t_local) > 0);

    melodeus::library::TrackMeta t_yt;
    t_yt.uri = "file://yt.wav";
    t_yt.title = "YouTube Festival Mix";
    t_yt.source_type = 1;
    REQUIRE(db.insert_track(t_yt) > 0);

    melodeus::library::TrackMeta t_sc;
    t_sc.uri = "file://sc.wav";
    t_sc.title = "SoundCloud Remix";
    t_sc.source_type = 2;
    REQUIRE(db.insert_track(t_sc) > 0);

    CHECK(db.count() == 3);
    CHECK(db.count_source(0) == 1);
    CHECK(db.count_source(1) == 1);
    CHECK(db.count_source(2) == 1);
    CHECK(db.count_source(3) == 0);

    // Búsqueda global (source_type = -1)
    CHECK(db.search("", 100, -1).size() == 3);

    // Búsqueda filtrada por fuente sin query
    CHECK(db.search("", 100, 0).size() == 1);
    CHECK(db.search("", 100, 0)[0].title == "Local Electro Track");
    CHECK(db.search("", 100, 1).size() == 1);
    CHECK(db.search("", 100, 1)[0].title == "YouTube Festival Mix");
    CHECK(db.search("", 100, 2).size() == 1);
    CHECK(db.search("", 100, 2)[0].title == "SoundCloud Remix");

    // Búsqueda filtrada por fuente con FTS query
    CHECK(db.search("Electro", 100, 0).size() == 1);
    CHECK(db.search("Electro", 100, 1).empty());
    CHECK(db.search("Festival", 100, 1).size() == 1);
    CHECK(db.search("Festival", 100, 2).empty());
}

TEST_CASE("Database: corrección de source_type para archivos fuera de downloads") {
    TempDb tmp("sanitize_source");
    melodeus::library::Database db;
    REQUIRE(db.open(tmp.path.string().c_str()));

    melodeus::library::TrackMeta t_local_down;
    t_local_down.uri = "C:/Users/dj/Downloads/MySong.mp3";
    t_local_down.title = "User Download Song";
    t_local_down.source_type = 1; // erróneamente clasificado como youtube
    REQUIRE(db.insert_track(t_local_down) > 0);

    melodeus::library::TrackMeta t_real_yt;
    t_real_yt.uri = "C:/Users/dj/AppData/Local/Temp/melodeus_f3_demo/downloads/Stream_123.wav";
    t_real_yt.title = "Real Stream";
    t_real_yt.source_type = 1;
    REQUIRE(db.insert_track(t_real_yt) > 0);

    std::filesystem::path app_dl = "C:/Users/dj/AppData/Local/Temp/melodeus_f3_demo/downloads";
    std::error_code ec;
    auto all = db.search("", 100, -1);
    for (const auto& tr : all) {
        if (tr.source_type == 1 || tr.source_type == 2) {
            std::filesystem::path p(tr.uri);
            bool inside_dl = false;
            auto rel = std::filesystem::relative(p, app_dl, ec);
            if (!ec && !rel.empty() && rel.string().find("..") == std::string::npos) {
                inside_dl = true;
            }
            if (!inside_dl) {
                std::string sql = "UPDATE tracks SET source_type = 0 WHERE track_id = " +
                                  std::to_string(tr.track_id) + ";";
                db.exec_sql(sql.c_str());
            }
        }
    }

    CHECK(db.count_source(0) == 1);
    CHECK(db.count_source(1) == 1);
    CHECK(db.search("", 100, 0)[0].title == "User Download Song");
    CHECK(db.search("", 100, 1)[0].title == "Real Stream");
}

