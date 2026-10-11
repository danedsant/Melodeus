#include "audio/decoder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_STDIO
#include <minimp3.h>
#include <minimp3_ex.h>

namespace melodeus::audio {
namespace {

std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool decode_wav_mem(const std::uint8_t* data, std::size_t size, DecodedTrack* out,
                    std::string* error) {
    drwav w;
    if (!drwav_init_memory(&w, data, size, nullptr)) {
        *error = "WAV no valido";
        return false;
    }
    if (w.channels < 1 || w.channels > 2) {
        drwav_uninit(&w);
        *error = "WAV con >2 canales no soportado";
        return false;
    }
    const std::uint64_t frames = w.totalPCMFrameCount;
    out->channels = static_cast<int>(w.channels);
    out->sample_rate = static_cast<double>(w.sampleRate);
    out->frames = static_cast<std::int64_t>(frames);
    out->samples.assign(static_cast<std::size_t>(frames) * w.channels, 0.0f);
    const std::uint64_t got = drwav_read_pcm_frames_f32(&w, frames, out->samples.data());
    drwav_uninit(&w);
    if (got != frames) {
        *error = "WAV truncado";
        return false;
    }
    return true;
}

bool decode_flac_mem(const std::uint8_t* data, std::size_t size, DecodedTrack* out,
                     std::string* error) {
    drflac* f = drflac_open_memory(data, size, nullptr);
    if (f == nullptr) {
        *error = "FLAC no valido";
        return false;
    }
    if (f->channels < 1 || f->channels > 2) {
        drflac_close(f);
        *error = "FLAC con >2 canales no soportado";
        return false;
    }
    const std::uint64_t frames = f->totalPCMFrameCount;
    out->channels = static_cast<int>(f->channels);
    out->sample_rate = static_cast<double>(f->sampleRate);
    out->frames = static_cast<std::int64_t>(frames);
    out->samples.assign(static_cast<std::size_t>(frames) * f->channels, 0.0f);
    const std::uint64_t got = drflac_read_pcm_frames_f32(f, frames, out->samples.data());
    drflac_close(f);
    if (got != frames) {
        *error = "FLAC truncado";
        return false;
    }
    return true;
}

bool decode_mp3_mem(const std::uint8_t* data, std::size_t size, DecodedTrack* out,
                    std::string* error) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_file_info_t info;
    if (mp3dec_load_buf(&dec, data, size, &info, nullptr, nullptr) != 0) {
        *error = "MP3 no valido";
        return false;
    }
    if (info.channels < 1 || info.channels > 2 || info.buffer == nullptr) {
        free(info.buffer);
        *error = "MP3 con >2 canales no soportado";
        return false;
    }
    out->channels = info.channels;
    out->sample_rate = static_cast<double>(info.hz);
    out->frames = static_cast<std::int64_t>(info.samples / info.channels);
    out->samples.assign(info.buffer, info.buffer + info.samples);
    free(info.buffer);
    return true;
}

} // namespace

bool decode_audio_memory(const std::uint8_t* data, std::size_t size, const std::string& hint_ext,
                         DecodedTrack* out, std::string* error) {
    if (data == nullptr || size == 0 || out == nullptr || error == nullptr) {
        if (error != nullptr) {
            *error = "argumentos invalidos";
        }
        return false;
    }
    out->samples.clear();
    out->frames = 0;
    out->channels = 0;
    out->sample_rate = 0.0;
    if (hint_ext == "wav") {
        return decode_wav_mem(data, size, out, error);
    }
    if (hint_ext == "flac") {
        return decode_flac_mem(data, size, out, error);
    }
    if (hint_ext == "mp3") {
        return decode_mp3_mem(data, size, out, error);
    }
    *error = "formato no soportado: " + hint_ext;
    return false;
}

bool decode_audio_file(const std::string& path, DecodedTrack* out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *error = "no se pudo abrir: " + path;
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                          std::istreambuf_iterator<char>());
    return decode_audio_memory(bytes.data(), bytes.size(), lower_ext(path), out, error);
}

namespace {

void put_u16(std::vector<std::uint8_t>* v, std::size_t off, std::uint16_t x) {
    (*v)[off] = static_cast<std::uint8_t>(x & 0xFF);
    (*v)[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
}

void put_u32(std::vector<std::uint8_t>* v, std::size_t off, std::uint32_t x) {
    (*v)[off] = static_cast<std::uint8_t>(x & 0xFF);
    (*v)[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    (*v)[off + 2] = static_cast<std::uint8_t>((x >> 16) & 0xFF);
    (*v)[off + 3] = static_cast<std::uint8_t>((x >> 24) & 0xFF);
}

} // namespace

bool encode_wav_pcm16(const DecodedTrack& in, std::vector<std::uint8_t>* out, std::string* error) {
    if (in.channels < 1 || in.channels > 2 || in.frames < 0 || in.sample_rate <= 0.0) {
        *error = "pista invalida para WAV";
        return false;
    }
    const auto ch = static_cast<std::uint16_t>(in.channels);
    const auto rate = static_cast<std::uint32_t>(in.sample_rate);
    const std::uint32_t block_align = ch * 2;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(in.frames) * block_align;
    out->assign(44 + data_bytes, 0);
    std::memcpy(out->data(), "RIFF", 4);
    put_u32(out, 4, 36 + data_bytes);
    std::memcpy(out->data() + 8, "WAVEfmt ", 8);
    put_u32(out, 16, 16);
    put_u16(out, 20, 1); // PCM
    put_u16(out, 22, ch);
    put_u32(out, 24, rate);
    put_u32(out, 28, rate * block_align);
    put_u16(out, 32, static_cast<std::uint16_t>(block_align));
    put_u16(out, 34, 16);
    std::memcpy(out->data() + 36, "data", 4);
    put_u32(out, 40, data_bytes);
    std::size_t wpos = 44;
    const std::size_t total = static_cast<std::size_t>(in.frames) * in.channels;
    for (std::size_t i = 0; i < total; ++i) {
        const float s = std::clamp(in.samples[i], -1.0f, 1.0f);
        const auto q = static_cast<std::int16_t>(s * 32767.0f);
        put_u16(out, wpos, static_cast<std::uint16_t>(q));
        wpos += 2;
    }
    return true;
}

bool write_wav_file(const std::string& path, const DecodedTrack& in, std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!encode_wav_pcm16(in, &bytes, error)) {
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        *error = "no se pudo escribir: " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

} // namespace melodeus::audio
