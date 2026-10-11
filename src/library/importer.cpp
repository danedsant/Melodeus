#include "library/importer.hpp"

#include "analysis/bpm.hpp"
#include "audio/decoder.hpp"

#include <algorithm>
#include <cctype>

namespace melodeus::library {

namespace {

bool is_audio_extension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".flac" || ext == ".mp3";
}

} // namespace

std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& dir, bool recursive) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return out;
    }
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    if (recursive) {
        for (std::filesystem::recursive_directory_iterator it(dir, opts, ec), last;
             it != last && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec) && is_audio_extension(it->path())) {
                out.push_back(it->path());
            }
        }
    } else {
        for (std::filesystem::directory_iterator it(dir, opts, ec), last; it != last && !ec;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && is_audio_extension(it->path())) {
                out.push_back(it->path());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

ImportResult import_file(Database& db, const std::filesystem::path& path) {
    ImportResult result;
    if (!db.ok()) {
        result.error = "database not open";
        return result;
    }

    audio::DecodedTrack decoded;
    std::string decode_error;
    if (!audio::decode_audio_file(path.string(), &decoded, &decode_error)) {
        result.error = decode_error.empty() ? "decode failed" : decode_error;
        return result;
    }

    // Mono para análisis.
    std::vector<float> mono(static_cast<std::size_t>(decoded.frames), 0.0f);
    if (decoded.channels == 1) {
        mono.assign(decoded.samples.begin(),
                    decoded.samples.begin() + static_cast<std::ptrdiff_t>(decoded.frames));
    } else if (decoded.channels >= 2) {
        for (std::int64_t i = 0; i < decoded.frames; ++i) {
            mono[static_cast<std::size_t>(i)] =
                0.5f * (decoded.samples[static_cast<std::size_t>(i * 2)] +
                        decoded.samples[static_cast<std::size_t>(i * 2 + 1)]);
        }
    } else {
        result.error = "no channels";
        return result;
    }

    result.bpm = analysis::detect_bpm(mono.data(), decoded.frames, decoded.sample_rate);
    result.key = analysis::detect_key(mono.data(), decoded.frames, decoded.sample_rate);

    TrackMeta meta;
    meta.source_type = 0;
    meta.uri = path.string();
    meta.title = path.stem().string();
    meta.bpm = result.bpm;
    meta.camelot = result.key.camelot;
    meta.camelot_minor = result.key.camelot_flat;
    meta.musical_key = result.key.traditional_name();
    meta.duration_ms = (decoded.sample_rate > 0.0)
                           ? static_cast<std::int64_t>(
                                 1000.0 * static_cast<double>(decoded.frames) / decoded.sample_rate)
                           : 0;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    meta.format = ext.empty() ? std::string() : ext.substr(1);
    std::error_code ec;
    meta.file_size = static_cast<std::int64_t>(std::filesystem::file_size(path, ec));

    result.track_id = db.insert_track(meta);
    if (result.track_id < 0) {
        result.error = "insert failed";
    }
    return result;
}

} // namespace melodeus::library
