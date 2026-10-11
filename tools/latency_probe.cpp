// latency_probe — abre WASAPI en modo exclusivo y emite un tono continuo,
// reportando el presupuesto de latencia de salida y la puntualidad del hilo
// de emisión (F0 del blueprint).
//
// No es un medidor de round-trip físico (haría falta loopback de hardware):
// declara la latencia calculada (frames de buffer + periodo del motor) y
// comprueba que ningún evento del driver llega tarde.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <propkeydef.h>
#include <propsys.h>
// functiondiscoverykeys necesita DEFINE_PROPERTYKEY de propkeydef.h: al final.
#include <functiondiscoverykeys_devpkey.h>

#include "core/rt_guard.hpp"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kChannels = 2;
// 3 ms = periodo mínimo de rotación del driver (144 frames @48 kHz →
// 0x19 → alineado 192 = 4 ms, que NO coincide con la rotación y provoca
// descartes; a 96 kHz, 3 ms = 288 frames, compatible con el cuántum).
constexpr REFERENCE_TIME kDefaultBufferDuration = 30000; // 3 ms (100-ns units)
constexpr double kToneHz = 440.0;
constexpr int kRunSeconds = 5;

std::string wide_to_utf8(const wchar_t* wide) {
    if (wide == nullptr) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

struct ProbeResult {
    bool ok = false;
    bool exclusive = false;
    long exclusive_error = 0;
    std::string device;
    std::string format_note;
    DWORD sample_rate = 0;
    WORD bits_per_sample = 0;
    WORD channels = 0;
    UINT32 buffer_frames = 0;
    WORD block_align = 0;
    REFERENCE_TIME engine_period = 0; // periodo por defecto del engine (compartido)
    REFERENCE_TIME min_period = 0;
    REFERENCE_TIME buffer_duration = 0;
    UINT64 frames_written = 0;
    std::uint64_t late_events = 0;
    std::uint64_t cycles = 0;
    double wall_seconds = 0.0;
    double gap_avg_ms = 0.0;
    double gap_min_ms = 0.0;
    double gap_max_ms = 0.0;
    double playback_rate = 0.0; // frames/s medidos con IAudioClock
    unsigned long long clock_frequency = 0;
    std::uint64_t gaps_lt1 = 0;
    std::uint64_t gaps_1_35 = 0;
    std::uint64_t gaps_35_45 = 0;
    std::uint64_t gaps_gt45 = 0;
};

const char* wasapi_error_text(HRESULT hr) {
    switch (static_cast<unsigned long>(hr)) {
    case 0x88890001ul:
        return "AUDCLNT_E_NOT_INITIALIZED: cliente sin inicializar";
    case 0x88890004ul:
        return "AUDCLNT_E_DEVICE_INVALIDATED: dispositivo desconectado";
    case 0x88890005ul:
        return "AUDCLNT_E_NOT_STOPPED: el stream no esta parado";
    case 0x88890006ul:
        return "AUDCLNT_E_BUFFER_TOO_LARGE: se pidio mas frames de los libres";
    case 0x88890007ul:
        return "AUDCLNT_E_OUT_OF_ORDER: GetBuffer/ReleaseBuffer desordenados";
    case 0x88890008ul:
        return "AUDCLNT_E_UNSUPPORTED_FORMAT: formato no soportado en exclusivo";
    case 0x88890009ul:
        return "AUDCLNT_E_INVALID_SIZE: tamano de buffer invalido";
    case 0x8889000Aul:
        return "AUDCLNT_E_DEVICE_IN_USE: endpoint ocupado o control exclusivo "
               "deshabilitado en las propiedades del dispositivo";
    case 0x8889000Bul:
        return "AUDCLNT_E_BUFFER_ERROR: error de buffer del driver";
    case 0x8889000Eul:
        return "AUDCLNT_E_EXCLUSIVE_MODE_ONLY: el endpoint exige exclusivo";
    case 0x88890019ul:
        return "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: alineacion de buffer";
    default:
        return "error WASAPI no catalogado";
    }
}

void release_all(IAudioClient* client, IMMDevice* device, IMMDeviceEnumerator* enumerator) {
    if (client != nullptr) {
        client->Release();
    }
    if (device != nullptr) {
        device->Release();
    }
    if (enumerator != nullptr) {
        enumerator->Release();
    }
}

ProbeResult run_probe(REFERENCE_TIME buffer_duration) {
    ProbeResult result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "CoInitializeEx fallo: 0x%08lX\n", hr);
        return result;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    IAudioClock* audio_clock = nullptr;
    HANDLE event = nullptr;
    WAVEFORMATEX* mix_format = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) {
            std::fprintf(stderr, "CoCreateInstance(MMDeviceEnumerator): 0x%08lX\n", hr);
            break;
        }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            std::fprintf(stderr, "GetDefaultAudioEndpoint: 0x%08lX\n", hr);
            break;
        }

        PROPVARIANT name_var;
        PropVariantInit(&name_var);
        IPropertyStore* properties = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) && properties != nullptr) {
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name_var))) {
                if (name_var.vt == VT_LPWSTR) {
                    result.device = wide_to_utf8(name_var.pwszVal);
                }
                PropVariantClear(&name_var);
            }
            properties->Release();
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        if (FAILED(hr)) {
            std::fprintf(stderr, "Activate(IAudioClient): 0x%08lX\n", hr);
            break;
        }

        // Candidatos de formato para exclusivo: float32 primero (formato de
        // trabajo de Melodeus), pcm16 como recurso. Se consultan con
        // IsFormatSupported en lugar de Initialize a ciegas: los drivers
        // Realtek suelen rechazar el mix format en exclusivo con
        // AUDCLNT_E_UNSUPPORTED_FORMAT.
        WAVEFORMATEX float_format{};
        float_format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        float_format.nChannels = kChannels;
        float_format.nSamplesPerSec = static_cast<DWORD>(kSampleRate);
        float_format.wBitsPerSample = 32;
        float_format.nBlockAlign = static_cast<WORD>(kChannels * 4);
        float_format.nAvgBytesPerSec = static_cast<DWORD>(kSampleRate) * float_format.nBlockAlign;

        WAVEFORMATEX float_441_format = float_format;
        float_441_format.nSamplesPerSec = 44100;
        float_441_format.nAvgBytesPerSec = 44100 * float_format.nBlockAlign;

        auto make_pcm = [](DWORD rate, WORD bits) -> WAVEFORMATEX {
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_PCM;
            f.nChannels = kChannels;
            f.nSamplesPerSec = rate;
            f.wBitsPerSample = bits;
            f.nBlockAlign = static_cast<WORD>(kChannels * ((bits + 7) / 8));
            f.nAvgBytesPerSec = rate * f.nBlockAlign;
            return f;
        };

        WAVEFORMATEX float_96000 = float_format;
        float_96000.nSamplesPerSec = 96000;
        float_96000.nAvgBytesPerSec = 96000 * float_format.nBlockAlign;

        // Tasas donde 3 ms (periodo de rotación del driver) da un número de
        // frames compatible con el cuántum de alineación: 64 kHz → 192,
        // 192 kHz → 576. A 48 kHz, 144 se alinea a 192 y desincroniza.
        const WAVEFORMATEX pcm24_64000 = make_pcm(64000, 24);
        const WAVEFORMATEX pcm24_192000 = make_pcm(192000, 24);
        const WAVEFORMATEX pcm24_48000 = make_pcm(48000, 24);
        const WAVEFORMATEX pcm16_64000 = make_pcm(64000, 16);
        const WAVEFORMATEX pcm16_192000 = make_pcm(192000, 16);
        const WAVEFORMATEX pcm16_48000 = make_pcm(48000, 16);
        const WAVEFORMATEX pcm16_44100 = make_pcm(44100, 16);

        const WAVEFORMATEX* candidates[] = {
            &pcm24_64000,  &pcm24_192000,     &pcm16_64000, &pcm16_192000, &float_96000,
            &float_format, &float_441_format, &pcm24_48000, &pcm16_48000,  &pcm16_44100};

        // Cada Initialize fallido deja el cliente inutilizable en algunos
        // drivers; se pide una instancia nueva antes del siguiente intento.
        auto reactivate = [&]() -> HRESULT {
            if (client != nullptr) {
                client->Release();
                client = nullptr;
            }
            return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(&client));
        };

        const WAVEFORMATEX* active_format = nullptr;
        REFERENCE_TIME requested_duration = buffer_duration;

        for (const WAVEFORMATEX* candidate : candidates) {
            WAVEFORMATEX* closest = nullptr;
            const HRESULT supported =
                client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, candidate, &closest);
            if (closest != nullptr) {
                CoTaskMemFree(closest);
            }
            std::fprintf(stderr, "candidato exclusivo %u-bit @%lu Hz: IsFormatSupported=0x%08lX",
                         static_cast<unsigned>(candidate->wBitsPerSample),
                         static_cast<unsigned long>(candidate->nSamplesPerSec),
                         static_cast<unsigned long>(supported));
            if (supported != S_OK) {
                std::fprintf(stderr, " (descartado)\n");
                continue;
            }
            hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    requested_duration, requested_duration, candidate, nullptr);
            std::fprintf(stderr, " Initialize=0x%08lX\n", static_cast<unsigned long>(hr));
            if (SUCCEEDED(hr)) {
                active_format = candidate;
                break;
            }
            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
                // El formato vale; solo pide un buffer alineado a su tamaño.
                // El baile posterior lo recalcula con GetBufferSize.
                active_format = candidate;
                break;
            }
            if (FAILED(reactivate())) {
                break;
            }
        }

        // Ultimo recurso en exclusivo: el formato mezcla que declara el driver.
        if (active_format == nullptr && client != nullptr) {
            hr = client->GetMixFormat(&mix_format);
            if (SUCCEEDED(hr)) {
                hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, requested_duration,
                                        requested_duration, mix_format, nullptr);
                std::fprintf(stderr, "candidato exclusivo mix: Initialize=0x%08lX\n",
                             static_cast<unsigned long>(hr));
                if (SUCCEEDED(hr)) {
                    active_format = mix_format;
                } else {
                    reactivate();
                }
            }
        }

        // Baile de alineación del modo exclusivo (pasos oficiales de MSDN):
        // GetBufferSize da el tamaño alineado, se libera el cliente y se crea
        // uno nuevo con Initialize ajustado.
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && active_format != nullptr) {
            UINT32 aligned_frames = 0;
            if (SUCCEEDED(client->GetBufferSize(&aligned_frames)) && aligned_frames > 0) {
                const double rate = static_cast<double>(active_format->nSamplesPerSec);
                requested_duration = static_cast<REFERENCE_TIME>(
                    (10000.0 * 1000.0 * static_cast<double>(aligned_frames)) / rate + 0.5);
                client->Release();
                client = nullptr;
                const HRESULT dance_activate = device->Activate(
                    __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
                hr = FAILED(dance_activate)
                         ? dance_activate
                         : client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK, requested_duration,
                                              requested_duration, active_format, nullptr);
            }
        }

        if (FAILED(hr) || active_format == nullptr) {
            // El modo exclusivo fue rechazado (formato / ocupado / política).
            // Fallback a modo compartido con conversión automática: seguimos
            // midiendo la puntualidad, pero declaramos el modo real y el
            // motivo del rechazo.
            const HRESULT exclusive_hr = FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
            result.exclusive_error = static_cast<long>(exclusive_hr);

            if (client != nullptr) {
                client->Release();
                client = nullptr;
            }
            const HRESULT activate_hr = device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));

            bool shared_ok = false;
            if (SUCCEEDED(activate_hr) && client != nullptr) {
                active_format = &float_format;
                requested_duration = buffer_duration;
                const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, requested_duration, 0,
                                        &float_format, nullptr);
                shared_ok = SUCCEEDED(hr);
            }

            if (!shared_ok) {
                std::fprintf(stderr, "Exclusivo rechazado: 0x%08lX (%s)\n",
                             static_cast<unsigned long>(exclusive_hr),
                             wasapi_error_text(exclusive_hr));
                std::fprintf(stderr, "Compartido tambien fallo: 0x%08lX (%s)\n",
                             static_cast<unsigned long>(hr), wasapi_error_text(hr));
                break;
            }
        } else {
            result.exclusive = true;
        }

        if (active_format == nullptr) {
            break;
        }

        const bool is_mix = (active_format == mix_format && mix_format != nullptr);
        if (is_mix) {
            result.format_note = "formato mezcla del dispositivo";
        } else if (active_format->wFormatTag == WAVE_FORMAT_PCM) {
            result.format_note =
                "pcm" + std::to_string(static_cast<unsigned>(active_format->wBitsPerSample));
        } else {
            result.format_note = "float32";
        }
        result.sample_rate = active_format->nSamplesPerSec;
        result.bits_per_sample = active_format->wBitsPerSample;
        result.channels = active_format->nChannels;
        result.block_align = active_format->nBlockAlign;
        result.buffer_duration = requested_duration;

        REFERENCE_TIME default_period = 0;
        REFERENCE_TIME min_period = 0;
        client->GetDevicePeriod(&default_period, &min_period);
        // En compartido los eventos llegan cada periodo por defecto del
        // engine; el mínimo (3 ms típico) solo aplica si pedimos ese tamaño.
        result.engine_period = default_period > 0 ? default_period : min_period;
        result.min_period = min_period;

        client->GetBufferSize(&result.buffer_frames);

        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            std::fprintf(stderr, "CreateEventW fallo\n");
            break;
        }
        hr = client->SetEventHandle(event);
        if (FAILED(hr)) {
            std::fprintf(stderr, "SetEventHandle: 0x%08lX\n", hr);
            break;
        }

        hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
        if (FAILED(hr)) {
            std::fprintf(stderr, "GetService(IAudioRenderClient): 0x%08lX\n", hr);
            break;
        }

        // IAudioClock: mide la tasa REAL de reproducción (frames/s) para
        // distinguir reloj nativo 64 kHz de eventos a 64 kHz con playback 48k.
        client->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&audio_clock));
        if (audio_clock != nullptr) {
            audio_clock->GetFrequency(&result.clock_frequency);
        }

        // Prefill: arrancamos con el buffer lleno para no arrancar en vacío.
        BYTE* data = nullptr;
        hr = render->GetBuffer(result.buffer_frames, &data);
        if (FAILED(hr) || data == nullptr) {
            std::fprintf(stderr, "GetBuffer(prefill): 0x%08lX\n", hr);
            break;
        }
        std::memset(data, 0, static_cast<std::size_t>(result.buffer_frames) * result.block_align);
        render->ReleaseBuffer(result.buffer_frames, 0);

        hr = client->Start();
        if (FAILED(hr)) {
            std::fprintf(stderr, "Start: 0x%08lX\n", hr);
            break;
        }

        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> late{0};
        std::atomic<std::uint64_t> written{0};
        std::atomic<std::uint64_t> cycles{0};
        std::atomic<long> emitter_error{0};
        // Solo el hilo emisor escribe; main lee tras join (happens-before).
        double gap_sum_ms = 0.0;
        double gap_min_ms = 1.0e9;
        double gap_max_ms = 0.0;
        std::uint64_t gaps = 0;
        std::uint64_t gaps_lt1 = 0;   // dobles señales (< 1 ms)
        std::uint64_t gaps_1_35 = 0;  // ~3 ms (periodo mínimo del driver)
        std::uint64_t gaps_35_45 = 0; // ~4 ms (nuestro buffer)
        std::uint64_t gaps_gt45 = 0;  // tardíos
        const UINT32 frames = result.buffer_frames;
        const double rate_hz =
            result.sample_rate > 0 ? static_cast<double>(result.sample_rate) : kSampleRate;
        const int channels = static_cast<int>(result.channels);
        const int bits = static_cast<int>(result.bits_per_sample);
        const bool pcm16 = bits == 16;
        const bool pcm24 = bits == 24;
        // Intervalo esperado entre eventos: en exclusivo es el periodo que
        // inicializamos (nuestro buffer); en compartido es el periodo por
        // defecto del engine (no el mínimo del dispositivo).
        const double expected_event_ms =
            result.exclusive ? (static_cast<double>(frames) * 1000.0) / rate_hz
                             : (static_cast<double>(result.engine_period) / 10000.0);

        const auto t0 = std::chrono::steady_clock::now();

        std::thread emitter([&] {
            // MMCSS "Pro Audio": sin esto el sistema puede posponer el hilo
            // decenas de ms y el evento del driver llega "tardío".
            DWORD mmcss_task = 0;
            AvSetMmThreadCharacteristicsA("Pro Audio", &mmcss_task);

            auto last = std::chrono::steady_clock::now();
            bool first_event = true;
            double phase = 0.0;
            const double step = 2.0 * 3.14159265358979323846 * kToneHz / rate_hz;

            while (!stop.load(std::memory_order_relaxed)) {
                if (WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) {
                    late.fetch_add(1, std::memory_order_relaxed);
                    break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (first_event) {
                    // El primer evento no mide nada: llega cuando el engine
                    // arranca, no a intervalo.
                    first_event = false;
                } else {
                    const double delta_ms =
                        std::chrono::duration<double, std::milli>(now - last).count();
                    if (delta_ms > expected_event_ms + 0.5) {
                        late.fetch_add(1, std::memory_order_relaxed);
                    }
                    gap_sum_ms += delta_ms;
                    ++gaps;
                    if (delta_ms < gap_min_ms) {
                        gap_min_ms = delta_ms;
                    }
                    if (delta_ms > gap_max_ms) {
                        gap_max_ms = delta_ms;
                    }
                    if (delta_ms < 1.0) {
                        ++gaps_lt1;
                    } else if (delta_ms < 3.5) {
                        ++gaps_1_35;
                    } else if (delta_ms < 4.5) {
                        ++gaps_35_45;
                    } else {
                        ++gaps_gt45;
                    }
                }
                last = now;

                // Compartido: el evento indica un periodo libre; pedir el
                // buffer completo provoca AUDCLNT_E_BUFFER_TOO_LARGE. En
                // exclusivo el engine consume el buffer entero entre eventos
                // (GetCurrentPadding no refleja el consumo en este modo), así
                // que se reclama el buffer completo, como hace el bucle
                // clásico de WASAPI exclusivo.
                UINT32 available = frames;
                if (!result.exclusive) {
                    UINT32 padding = 0;
                    if (FAILED(client->GetCurrentPadding(&padding))) {
                        emitter_error.store(static_cast<long>(0x8889000Bul),
                                            std::memory_order_relaxed);
                        break;
                    }
                    available = (frames > padding) ? (frames - padding) : 0;
                }
                if (available == 0) {
                    continue;
                }

                BYTE* buffer = nullptr;
                const HRESULT buffer_hr = render->GetBuffer(available, &buffer);
                if (FAILED(buffer_hr) || buffer == nullptr) {
                    emitter_error.store(static_cast<long>(buffer_hr), std::memory_order_relaxed);
                    break;
                }
                if (pcm16) {
                    auto* samples = reinterpret_cast<std::int16_t*>(buffer);
                    for (UINT32 i = 0; i < available; ++i) {
                        const auto v = static_cast<std::int16_t>(0.20 * std::sin(phase) * 32767.0);
                        phase += step;
                        if (phase >= 2.0 * 3.14159265358979323846) {
                            phase -= 2.0 * 3.14159265358979323846;
                        }
                        for (int c = 0; c < channels; ++c) {
                            samples[static_cast<std::size_t>(i) * channels + c] = v;
                        }
                    }
                } else if (pcm24) {
                    for (UINT32 i = 0; i < available; ++i) {
                        const auto s = static_cast<std::uint32_t>(
                            static_cast<std::int32_t>(0.20 * std::sin(phase) * 8388607.0));
                        phase += step;
                        if (phase >= 2.0 * 3.14159265358979323846) {
                            phase -= 2.0 * 3.14159265358979323846;
                        }
                        for (int c = 0; c < channels; ++c) {
                            BYTE* p = buffer + (static_cast<std::size_t>(i) * channels +
                                                static_cast<std::size_t>(c)) *
                                                   3;
                            p[0] = static_cast<BYTE>(s & 0xFFu);
                            p[1] = static_cast<BYTE>((s >> 8) & 0xFFu);
                            p[2] = static_cast<BYTE>((s >> 16) & 0xFFu);
                        }
                    }
                } else {
                    auto* samples = reinterpret_cast<float*>(buffer);
                    for (UINT32 i = 0; i < available; ++i) {
                        const float v = static_cast<float>(0.20 * std::sin(phase));
                        phase += step;
                        if (phase >= 2.0 * 3.14159265358979323846) {
                            phase -= 2.0 * 3.14159265358979323846;
                        }
                        for (int c = 0; c < channels; ++c) {
                            samples[static_cast<std::size_t>(i) * channels + c] = v;
                        }
                    }
                }
                render->ReleaseBuffer(available, 0);
                written.fetch_add(available, std::memory_order_relaxed);
                cycles.fetch_add(1, std::memory_order_relaxed);
            }
        });

        // Muestreo de posición en el centro de la ventana de emisión.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        UINT64 pos0 = 0;
        if (audio_clock != nullptr) {
            audio_clock->GetPosition(&pos0, nullptr);
        }
        const auto tp0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds((kRunSeconds - 1) * 1000));
        UINT64 pos1 = 0;
        if (audio_clock != nullptr) {
            audio_clock->GetPosition(&pos1, nullptr);
        }
        const auto tp1 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (audio_clock != nullptr && pos1 > pos0) {
            const double dt = std::chrono::duration<double>(tp1 - tp0).count();
            result.playback_rate = static_cast<double>(pos1 - pos0) / dt;
        }

        client->Stop();
        stop.store(true, std::memory_order_relaxed);
        SetEvent(event);
        emitter.join();

        const auto t1 = std::chrono::steady_clock::now();
        result.wall_seconds = std::chrono::duration<double>(t1 - t0).count();
        result.frames_written = written.load();
        result.late_events = late.load();
        result.cycles = cycles.load();
        result.gap_avg_ms = gaps > 0 ? gap_sum_ms / static_cast<double>(gaps) : 0.0;
        result.gap_min_ms = gaps > 0 ? gap_min_ms : 0.0;
        result.gap_max_ms = gap_max_ms;
        result.gaps_lt1 = gaps_lt1;
        result.gaps_1_35 = gaps_1_35;
        result.gaps_35_45 = gaps_35_45;
        result.gaps_gt45 = gaps_gt45;
        const long emitter_hr = emitter_error.load();
        if (emitter_hr != 0) {
            std::fprintf(stderr, "GetBuffer en el emisor fallo: 0x%08lX (%s)\n",
                         static_cast<unsigned long>(emitter_hr), wasapi_error_text(emitter_hr));
        }
        result.ok = true;
    } while (false);

    if (render != nullptr) {
        render->Release();
    }
    if (audio_clock != nullptr) {
        audio_clock->Release();
    }
    if (event != nullptr) {
        CloseHandle(event);
    }
    release_all(client, device, enumerator);
    if (mix_format != nullptr) {
        CoTaskMemFree(mix_format);
    }

    CoUninitialize();
    return result;
}

} // namespace

int main(int argc, char** argv) {
    melodeus::core::rt::install_debug_heap_hook();

    // Uso: latency_probe [buffer_ms] — tamaño de buffer a solicitar (def. 3 ms).
    REFERENCE_TIME buffer_duration = kDefaultBufferDuration;
    if (argc > 1) {
        const double ms = std::atof(argv[1]);
        if (ms >= 0.5 && ms <= 500.0) {
            buffer_duration = static_cast<REFERENCE_TIME>(ms * 10000.0 + 0.5);
        } else {
            std::fprintf(stderr, "buffer_ms fuera de rango [0.5, 500]: %s\n", argv[1]);
            return 1;
        }
    }

    std::printf("Melodeus latency_probe (WASAPI)\n");
    std::printf("------------------------------------------\n");

    const ProbeResult r = run_probe(buffer_duration);
    if (!r.ok) {
        std::fprintf(stderr, "No se pudo abrir ningun modo de emision.\n");
        return 1;
    }

    const double rate = r.sample_rate > 0 ? static_cast<double>(r.sample_rate) : kSampleRate;
    const double buffer_request_ms = static_cast<double>(r.buffer_duration) / 10000.0;
    const double engine_period_ms = static_cast<double>(r.engine_period) / 10000.0;
    const double min_period_ms = static_cast<double>(r.min_period) / 10000.0;
    const double buffer_frames_ms = (static_cast<double>(r.buffer_frames) * 1000.0) / rate;
    // Exclusivo: el buffer ES toda la cola del motor (lo que escribiste sale
    // tras el contenido ya encolado ≈ buffer). Compartido: la cola del buffer
    // más un periodo de engine por delante.
    const double estimated_latency_ms =
        r.exclusive ? buffer_frames_ms : (buffer_frames_ms + engine_period_ms);
    const double delivered = static_cast<double>(r.frames_written) / rate;

    std::printf("Dispositivo          : %s\n",
                r.device.empty() ? "(desconocido)" : r.device.c_str());
    std::printf("Modo                 : %s\n", r.exclusive ? "EXCLUSIVO" : "COMPARTIDO (fallback)");
    if (!r.exclusive) {
        std::printf("Motivo del rechazo   : 0x%08lX - %s\n",
                    static_cast<unsigned long>(r.exclusive_error),
                    wasapi_error_text(static_cast<HRESULT>(r.exclusive_error)));
    }
    std::printf("Formato              : %s @ %.0f Hz (%u bits, %u canales)\n",
                r.format_note.c_str(), rate, static_cast<unsigned>(r.bits_per_sample),
                static_cast<unsigned>(r.channels));
    std::printf("Buffer solicitado    : %.2f ms\n", buffer_request_ms);
    std::printf("Buffer efectivo      : %.2f ms (%u frames)\n", buffer_frames_ms, r.buffer_frames);
    std::printf("Periodo del motor    : %.2f ms (min %.2f ms)\n", engine_period_ms, min_period_ms);
    std::printf("Latencia estimada    : %.2f ms\n", estimated_latency_ms);
    std::printf("Ciclos emitidos      : %llu\n", static_cast<unsigned long long>(r.cycles));
    if (r.cycles > 1) {
        const double inferred_khz =
            (static_cast<double>(r.frames_written) / r.wall_seconds) / 1000.0;
        std::printf("Gaps de evento       : avg %.3f ms (min %.3f, max %.3f) [esperado ~%.3f]\n",
                    r.gap_avg_ms, r.gap_min_ms, r.gap_max_ms,
                    r.exclusive ? (static_cast<double>(r.buffer_frames) * 1000.0) / rate
                                : engine_period_ms);
        std::printf("Clock inferido       : %.2f kHz (frames escritos / wall)\n", inferred_khz);
        std::printf("Histograma gaps      : <1ms=%llu  1-3.5ms=%llu  3.5-4.5ms=%llu  >4.5ms=%llu\n",
                    static_cast<unsigned long long>(r.gaps_lt1),
                    static_cast<unsigned long long>(r.gaps_1_35),
                    static_cast<unsigned long long>(r.gaps_35_45),
                    static_cast<unsigned long long>(r.gaps_gt45));
    }
    if (r.playback_rate > 0.0) {
        std::printf("Playback real        : %.0f fps (GetFrequency=%llu)\n", r.playback_rate,
                    r.clock_frequency);
    }
    std::printf("Audio emitido        : %.2f s (wall %.2f s)\n", delivered, r.wall_seconds);
    std::printf("Eventos tardios      : %llu\n", static_cast<unsigned long long>(r.late_events));

    // El emisor tuvo que entregar prácticamente todo el tiempo de pared:
    // ciclos=0 con tardios=0 significa que el hilo murió o nunca avanzó.
    const bool emitted_ok = delivered >= r.wall_seconds * 0.8;
    const bool timing_ok = r.late_events == 0 && emitted_ok;
    std::printf("Entrega              : %s  (>= 80%% del tiempo de pared)\n",
                emitted_ok ? "OK" : "FALLA");

    if (r.exclusive) {
        const bool latency_ok = estimated_latency_ms < 5.0;
        std::printf("\n%s latencia (<5 ms) | %s timing | exclusivo\n", latency_ok ? "OK" : "FALLA",
                    timing_ok ? "OK" : "FALLA");
        if (latency_ok && timing_ok) {
            return 0;
        }
        return 3;
    }

    std::printf("\nlatencia %.2f ms (el objetivo <5 ms aplica a exclusivo) | %s timing | "
                "compartido\n",
                estimated_latency_ms, timing_ok ? "OK" : "FALLA");
    std::printf("\nNota: el modo exclusivo fue rechazado. Revisa en Configuracion > Sonido >\n"
                "propiedades del dispositivo > Altas prestaciones > 'Permitir el acceso\n"
                "exclusivo a las aplicaciones'.\n");
    return timing_ok ? 2 : 3;
}
