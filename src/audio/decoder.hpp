#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace melodeus::audio {

/// Pista decodificada: muestras f32 interleaved en la tasa nativa del archivo.
struct DecodedTrack {
    std::vector<float> samples;
    std::int64_t frames = 0;
    int channels = 0;
    double sample_rate = 0.0;
};

/// Decodifica un archivo completo según la extensión (.wav/.flac/.mp3).
bool decode_audio_file(const std::string& path, DecodedTrack* out, std::string* error);

/// Decodifica memoria según la extensión indicada en hint_ext ("wav", "mp3"...).
bool decode_audio_memory(const std::uint8_t* data, std::size_t size, const std::string& hint_ext,
                         DecodedTrack* out, std::string* error);

/// Escribe WAV PCM16 little-endian (mono/estéreo); para tests y selftest.
bool encode_wav_pcm16(const DecodedTrack& in, std::vector<std::uint8_t>* out, std::string* error);

bool write_wav_file(const std::string& path, const DecodedTrack& in, std::string* error);

} // namespace melodeus::audio
