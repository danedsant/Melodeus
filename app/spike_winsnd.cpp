#include "spike_winsnd.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "audio/decoder.hpp"
#include "audio/engine.hpp"
#include "waveform/waveform.hpp"

namespace melodeus::app {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTrackSeconds = 30.0; // pistas sintéticas para F2 (waveform)
constexpr int kTapLen = 1024;

audio::DecodedTrack make_sine(double hz, double secs, double rate) {
    audio::DecodedTrack t;
    t.channels = 2;
    t.sample_rate = rate;
    t.frames = static_cast<std::int64_t>(secs * rate);
    t.samples.resize(static_cast<std::size_t>(t.frames) * 2);
    for (std::int64_t i = 0; i < t.frames; ++i) {
        const float v =
            static_cast<float>(0.5 * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
        t.samples[static_cast<std::size_t>(i) * 2] = v;
        t.samples[static_cast<std::size_t>(i) * 2 + 1] = v;
    }
    return t;
}

struct EngineCallback final : juce::AudioIODeviceCallback {
    std::unique_ptr<audio::Engine> engine;
    audio::DecodedTrack track_a;
    audio::DecodedTrack track_b;
    std::vector<float> scratch_l;
    std::vector<float> scratch_r;
    std::size_t max_block = 0;
    std::atomic<long long> samples{0};
    std::atomic<int> callbacks{0};
    std::atomic<long long> process_ns_total{0};
    std::atomic<long long> process_ns_max{0};
    std::atomic<long long> xrun_gaps{0};
    std::atomic<long long> max_gap_ns{0};
    std::atomic<int> device_errors{0};
    // Tap del master (SPSC doble búfer): escritor = hilo de audio.
    std::atomic<unsigned> tap_seq{0};
    float tap_buf[2][kTapLen]{};
    int tap_fill = 0;
    int tap_cur = 0;
    // Pirámides inmutables tras aboutToStart (leídas por el hilo de render).
    wave::WaveformPyramid pyr_a;
    wave::WaveformPyramid pyr_b;
    // Carga con staging: el hilo llamador decodifica en stage[i] y publica
    // pending_load; el hilo de audio hace el swap al inicio del callback.
    audio::DecodedTrack stage[2];
    char title_a[128] = "Sine 440 Hz";
    char title_b[128] = "Sine 660 Hz";
    std::atomic<int> pending_load{-1};
    // Inputs de UI aplicados en el hilo de audio (smoothing del motor).
    std::atomic<float> ui_xf{0.0f};
    std::atomic<float> ui_gain_a{1.0f};
    std::atomic<float> ui_gain_b{1.0f};
    std::atomic<float> ui_master{0.15f};
    std::atomic<int> ui_play_toggle{0}; ///< bit0=A bit1=B
    std::atomic<float> ui_pitch[2]{{0.0f}, {0.0f}};
    std::atomic<float> ui_seek_sec[2]{{-1.0f}, {-1.0f}}; ///< <0 = sin seek
    std::atomic<float> ui_eq[2][3]{};                    ///< [deck][0=lo,1=mid,2=hi] dB
    std::atomic<float> ui_filter[2]{{0.0f}, {0.0f}};     ///< -1 LP .. +1 HP
    char last_load_error[160]{};
    std::atomic<bool> running{false};
    // Solo en el hilo de audio:
    std::chrono::steady_clock::time_point last_cb_time{};
    bool have_last_cb = false;
    long long gap_threshold_ns = 0;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        const double r = device->getCurrentSampleRate();
        const double rate = r > 0.0 ? r : 48000.0;
        max_block = static_cast<std::size_t>(std::max(device->getCurrentBufferSizeSamples(), 64));
        const double period_s = static_cast<double>(device->getCurrentBufferSizeSamples()) / rate;
        // xrun = llegada de callback con gap > 2.5× periodo nominal (buffer
        // insuficiente: con MMCSS "Pro Audio" un gap así es underrun real).
        gap_threshold_ns = static_cast<long long>(2.5 * period_s * 1.0e9);
        engine = std::make_unique<audio::Engine>(audio::EngineConfig{rate, max_block});
        scratch_l.assign(max_block, 0.0f);
        scratch_r.assign(max_block, 0.0f);
        track_a = make_sine(440.0, kTrackSeconds, rate);
        track_b = make_sine(660.0, kTrackSeconds, rate);
        pyr_a.build(track_a.samples.data(), track_a.frames, track_a.channels, rate);
        pyr_b.build(track_b.samples.data(), track_b.frames, track_b.channels, rate);
        engine->deck_a().load({track_a.samples.data(), track_a.frames, track_a.channels, rate});
        engine->deck_a().play();
        engine->deck_b().load({track_b.samples.data(), track_b.frames, track_b.channels, rate});
        engine->deck_b().set_pitch_percent(-12.0);
        engine->deck_b().play();
        engine->set_curve(audio::CrossfadeCurve::Linear);
        engine->set_crossfader(0.0);
        engine->set_master_gain(0.15);
        pending_load.store(-1, std::memory_order_relaxed);
        ui_xf.store(0.0f, std::memory_order_relaxed);
        ui_gain_a.store(1.0f, std::memory_order_relaxed);
        ui_gain_b.store(1.0f, std::memory_order_relaxed);
        ui_master.store(0.15f, std::memory_order_relaxed);
        ui_play_toggle.store(0, std::memory_order_relaxed);
        samples.store(0, std::memory_order_relaxed);
        callbacks.store(0, std::memory_order_relaxed);
        process_ns_total.store(0, std::memory_order_relaxed);
        process_ns_max.store(0, std::memory_order_relaxed);
        xrun_gaps.store(0, std::memory_order_relaxed);
        max_gap_ns.store(0, std::memory_order_relaxed);
        device_errors.store(0, std::memory_order_relaxed);
        tap_fill = 0;
        tap_cur = 0;
        tap_seq.store(0, std::memory_order_relaxed);
        running.store(true, std::memory_order_release);
        have_last_cb = false;
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output,
                                          int num_out, int num_samples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        const auto arrival = std::chrono::steady_clock::now();
        if (have_last_cb) {
            const long long gap =
                std::chrono::duration_cast<std::chrono::nanoseconds>(arrival - last_cb_time)
                    .count();
            long long prev = max_gap_ns.load(std::memory_order_relaxed);
            while (gap > prev &&
                   !max_gap_ns.compare_exchange_weak(prev, gap, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
            }
            if (gap > gap_threshold_ns) {
                xrun_gaps.fetch_add(1, std::memory_order_relaxed);
            }
        }
        last_cb_time = arrival;
        have_last_cb = true;

        if (engine != nullptr && num_samples > 0) {
            // Swap de carga staged (decode hecho en el hilo llamador).
            const int pend = pending_load.exchange(-1, std::memory_order_acq_rel);
            if (pend >= 0 && pend < 2) {
                audio::DecodedTrack& staged = stage[pend];
                if (staged.frames > 0 && staged.samples.size() >= 2) {
                    wave::WaveformPyramid& pyr = (pend == 0) ? pyr_a : pyr_b;
                    pyr.build(staged.samples.data(), staged.frames, staged.channels,
                              staged.sample_rate);
                    audio::Deck& deck = (pend == 0) ? engine->deck_a() : engine->deck_b();
                    deck.load({staged.samples.data(), staged.frames, staged.channels,
                               staged.sample_rate});
                    deck.play();
                }
            }
            // Inputs de UI (smoothing en el motor).
            engine->set_crossfader(ui_xf.load(std::memory_order_relaxed));
            engine->deck_a().set_gain(ui_gain_a.load(std::memory_order_relaxed));
            engine->deck_b().set_gain(ui_gain_b.load(std::memory_order_relaxed));
            engine->set_master_gain(ui_master.load(std::memory_order_relaxed));
            const int toggles = ui_play_toggle.exchange(0, std::memory_order_acq_rel);
            if ((toggles & 1) != 0) {
                audio::Deck& d = engine->deck_a();
                if (d.playing()) {
                    d.stop();
                } else {
                    d.play();
                }
            }
            if ((toggles & 2) != 0) {
                audio::Deck& d = engine->deck_b();
                if (d.playing()) {
                    d.stop();
                } else {
                    d.play();
                }
            }
            // Pitch / seek (CUE) / EQ / filtro (F3.5).
            for (int d = 0; d < 2; ++d) {
                audio::Deck& deck = (d == 0) ? engine->deck_a() : engine->deck_b();
                deck.set_pitch_percent(ui_pitch[d].load(std::memory_order_relaxed));
                const float seek_to = ui_seek_sec[d].exchange(-1.0f, std::memory_order_acq_rel);
                if (seek_to >= 0.0f) {
                    deck.seek(static_cast<double>(seek_to));
                }
            }
            engine->set_eq_a(ui_eq[0][0].load(std::memory_order_relaxed),
                             ui_eq[0][1].load(std::memory_order_relaxed),
                             ui_eq[0][2].load(std::memory_order_relaxed));
            engine->set_eq_b(ui_eq[1][0].load(std::memory_order_relaxed),
                             ui_eq[1][1].load(std::memory_order_relaxed),
                             ui_eq[1][2].load(std::memory_order_relaxed));
            engine->set_filter_a(ui_filter[0].load(std::memory_order_relaxed));
            engine->set_filter_b(ui_filter[1].load(std::memory_order_relaxed));

            const auto t0 = std::chrono::steady_clock::now();
            int done = 0;
            while (done < num_samples) {
                const int chunk = static_cast<int>(
                    std::min<std::size_t>(max_block, static_cast<std::size_t>(num_samples - done)));
                engine->process(scratch_l.data(), scratch_r.data(),
                                static_cast<std::size_t>(chunk));
                for (int i = 0; i < chunk; ++i) {
                    if (num_out > 0 && output[0] != nullptr) {
                        output[0][done + i] = scratch_l[static_cast<std::size_t>(i)];
                    }
                    if (num_out > 1 && output[1] != nullptr) {
                        output[1][done + i] = scratch_r[static_cast<std::size_t>(i)];
                    }
                    // Tap mono del master para el espectro (SPSC doble búfer).
                    float* dst = tap_buf[tap_cur];
                    dst[tap_fill] = 0.5f * (scratch_l[static_cast<std::size_t>(i)] +
                                            scratch_r[static_cast<std::size_t>(i)]);
                    if (++tap_fill == kTapLen) {
                        tap_cur ^= 1;
                        tap_fill = 0;
                        tap_seq.fetch_add(1, std::memory_order_release);
                    }
                }
                done += chunk;
            }
            const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - t0)
                                     .count();
            process_ns_total.fetch_add(ns, std::memory_order_relaxed);
            long long prev = process_ns_max.load(std::memory_order_relaxed);
            while (ns > prev &&
                   !process_ns_max.compare_exchange_weak(prev, ns, std::memory_order_relaxed,
                                                         std::memory_order_relaxed)) {
            }
        }
        samples.fetch_add(num_samples, std::memory_order_relaxed);
        callbacks.fetch_add(1, std::memory_order_relaxed);
    }

    void audioDeviceError(const juce::String&) override {
        device_errors.fetch_add(1, std::memory_order_relaxed);
    }

    void audioDeviceStopped() override {
        running.store(false, std::memory_order_release);
        engine.reset();
    }
};

struct SpikeReport {
    bool ok = false;
    std::string text;
};

/// Logger a stdout: los errores de apertura de JUCE/WASAPI no tienen por qué
/// devolverse como String de initialise(); aquí salen a la consola del selftest.
class StdoutLogger final : public juce::Logger {
public:
    void logMessage(const juce::String& message) override {
        std::fputs(message.toRawUTF8(), stdout);
        std::fputc('\n', stdout);
    }
};

std::thread g_thread;
std::atomic<bool> g_stop{false};
bool g_started = false;
EngineCallback g_cb;
SpikeReport g_report; // escrito antes del join; leído tras el join

/// Abre con el tipo de dispositivo indicado a 48 kHz / 144 frames.
///
/// Matices de la API (derivados de la fuente de AudioDeviceManager):
///  * initialise() llama a pickCurrentDeviceTypeWithDevices() y descarta la
///    selección de modo hecha aquí (recaía en el compartido) → no usarlo.
///  * setAudioDeviceSetup() hace early-return si los nombres de dispositivo
///    están vacíos: hay que pasar el nombre por defecto del tipo.
///  * bufferSize 144 puede ser rechazado por el modo; se reintenta con 0
///    (preferencia del dispositivo).
juce::String open_with(juce::AudioDeviceManager& mgr, const char* type) {
    mgr.setCurrentAudioDeviceType(type, false);
    juce::AudioIODeviceType* current = mgr.getCurrentDeviceTypeObject();
    if (current == nullptr || current->getTypeName() != juce::String(type)) {
        return juce::String("tipo no disponible: ") + type;
    }
    const juce::StringArray outs = current->getDeviceNames(false /*output*/);
    if (outs.isEmpty()) {
        return juce::String("sin dispositivos de salida en ") + type;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = outs[0];
    setup.sampleRate = 48000.0;
    setup.bufferSize = 144;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;

    juce::String err = mgr.setAudioDeviceSetup(setup, false);
    if (err.isNotEmpty()) {
        setup.bufferSize = 0; // preferencia del dispositivo
        err = mgr.setAudioDeviceSetup(setup, false);
    }
    if (err.isEmpty() && mgr.getCurrentAudioDevice() == nullptr) {
        err = "dispositivo nulo tras abrir";
    }
    return err;
}

void run_spike(int seconds) {
    // Este hilo pasa a ser el message thread de JUCE (dispatch loop abajo);
    // los callbacks de audio corren en el hilo de WASAPI.
    juce::MessageManager::getInstance();
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    SpikeReport rep;
    juce::AudioDeviceManager mgr;
    mgr.getAvailableDeviceTypes(); // crea y escanea los tipos (sin initialise)

    struct Mode {
        const char* type;
        const char* label;
    };
    const Mode modes[] = {
        {"Windows Audio (Exclusive Mode)", "exclusivo"},
        {"Windows Audio (Low Latency Mode)", "compartido low-latency (IAudioClient3)"},
        {"Windows Audio", "compartido"},
    };

    juce::String err;
    const Mode* chosen = nullptr;
    for (const Mode& mode : modes) {
        err = open_with(mgr, mode.type);
        if (err.isEmpty() && mgr.getCurrentAudioDevice() != nullptr) {
            chosen = &mode;
            break;
        }
    }

    if (chosen == nullptr) {
        juce::Logger::setCurrentLogger(nullptr);
        rep.ok = false;
        rep.text = "[audio] FALLO al abrir el dispositivo: " + err.toStdString() + "\n";
        g_report = std::move(rep);
        return;
    }

    mgr.addAudioCallback(&g_cb);

    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - t0 >= std::chrono::seconds(seconds)) {
            break;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    juce::AudioIODevice* dev = mgr.getCurrentAudioDevice();
    const double rate = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
    const int buffer = dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;
    const int latency = dev != nullptr ? dev->getOutputLatencyInSamples() : 0;
    const std::string device_name =
        dev != nullptr ? dev->getName().toStdString() : "(sin dispositivo)";
    const double total_ms =
        rate > 0.0 ? 1000.0 * static_cast<double>(buffer + latency) / rate : 0.0;

    mgr.removeAudioCallback(&g_cb);
    const long long n = g_cb.samples.load(std::memory_order_relaxed);
    const long long cb = g_cb.callbacks.load(std::memory_order_relaxed);
    const long long ns_total = g_cb.process_ns_total.load(std::memory_order_relaxed);
    const long long ns_max = g_cb.process_ns_max.load(std::memory_order_relaxed);
    const long long xruns = g_cb.xrun_gaps.load(std::memory_order_relaxed);
    const double max_gap_ms =
        static_cast<double>(g_cb.max_gap_ns.load(std::memory_order_relaxed)) / 1.0e6;
    const int dev_errors = g_cb.device_errors.load(std::memory_order_relaxed);
    const double avg_ms = cb > 0 ? static_cast<double>(ns_total) / cb / 1.0e6 : 0.0;
    const double max_ms = static_cast<double>(ns_max) / 1.0e6;
    const double threshold_ms = static_cast<double>(g_cb.gap_threshold_ns) / 1.0e6;
    const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double expected = wall * rate;
    const std::string type_name =
        mgr.getCurrentDeviceTypeObject() != nullptr
            ? mgr.getCurrentDeviceTypeObject()->getTypeName().toStdString()
            : "(sin tipo)";
    juce::Logger::setCurrentLogger(nullptr);

    char buf[1280];
    std::snprintf(buf, sizeof(buf),
                  "[audio] modo=%s\n"
                  "[audio] tipo-juce=%s\n"
                  "[audio] dispositivo=%s\n"
                  "[audio] rate=%.0f Hz  buffer=%d f  latencia salida=%d f\n"
                  "[audio] LATENCIA (buffer+salida) = %.2f ms\n"
                  "[audio] wall=%.2f s  callbacks=%lld  muestras=%lld (esperado~%.0f, %.1f%%)\n"
                  "[audio] motor: avg=%.3f ms/bloque  max=%.3f ms  (presupuesto 5 ms)\n"
                  "[audio] xruns=%lld (umbral gap>%.2f ms)  max-gap=%.2f ms  "
                  "errores-dispositivo=%d\n",
                  chosen->label, type_name.c_str(), device_name.c_str(), rate, buffer, latency,
                  total_ms, wall, cb, n, expected,
                  expected > 0.0 ? 100.0 * static_cast<double>(n) / expected : 0.0, avg_ms, max_ms,
                  xruns, threshold_ms, max_gap_ms, dev_errors);
    rep.ok = n > 0 && max_ms < 5.0 && xruns == 0 && dev_errors == 0;
    rep.text = buf;
    g_report = std::move(rep);
}

} // namespace

bool audio_spike_view(AudioSpikeView* out) {
    if (out == nullptr || !g_started || g_cb.engine == nullptr ||
        !g_cb.running.load(std::memory_order_acquire)) {
        return false;
    }
    audio::Engine& engine = *g_cb.engine;
    out->deck_a_seconds = engine.deck_a().position_seconds();
    out->deck_b_seconds = engine.deck_b().position_seconds();
    const audio::TrackView& ta = engine.deck_a().track();
    const audio::TrackView& tb = engine.deck_b().track();
    out->duration_a_seconds =
        (ta.sample_rate > 0.0) ? static_cast<double>(ta.frames) / ta.sample_rate : 0.0;
    out->duration_b_seconds =
        (tb.sample_rate > 0.0) ? static_cast<double>(tb.frames) / tb.sample_rate : 0.0;
    out->sample_rate = engine.config().sample_rate;
    out->playing_a = engine.deck_a().playing();
    out->playing_b = engine.deck_b().playing();
    out->pitch_a_pct = engine.deck_a().pitch_percent();
    out->pitch_b_pct = engine.deck_b().pitch_percent();
    out->peak_a = engine.peak_a();
    out->peak_b = engine.peak_b();
    out->master_env = engine.master_env();
    out->title_a = g_cb.title_a;
    out->title_b = g_cb.title_b;
    out->pyramid_a = &g_cb.pyr_a;
    out->pyramid_b = &g_cb.pyr_b;
    out->tap_seq = g_cb.tap_seq.load(std::memory_order_acquire);
    const int idx = (out->tap_seq & 1u) ^ 1u;
    out->tap = g_cb.tap_buf[idx];
    out->tap_len = kTapLen;
    return true;
}

bool audio_spike_load(int deck, const char* path, const char* title) {
    g_cb.last_load_error[0] = '\0';
    if (!g_started || deck < 0 || deck > 1 || path == nullptr || path[0] == '\0') {
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error),
                      "carga inválida (deck %d)", deck);
        return false;
    }
    if (g_cb.pending_load.load(std::memory_order_acquire) >= 0) {
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error),
                      "carga anterior aún pendiente");
        return false; // carga anterior aún pendiente
    }
    audio::DecodedTrack decoded;
    std::string error;
    if (!audio::decode_audio_file(path, &decoded, &error) || decoded.frames <= 0) {
        std::printf("[f3] decode falló: %s (%s)\n", path, error.c_str());
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error), "decode falló: %s",
                      error.empty() ? "formato no soportado" : error.c_str());
        return false;
    }
    g_cb.stage[deck] = std::move(decoded);
    char* t = (deck == 0) ? g_cb.title_a : g_cb.title_b;
    std::snprintf(t, 128, "%s", (title != nullptr && title[0] != '\0') ? title : path);
    g_cb.pending_load.store(deck, std::memory_order_release);
    return true;
}

bool audio_spike_load_samples(int deck, const float* interleaved, std::int64_t frames, int channels,
                              double sample_rate, const char* title) {
    g_cb.last_load_error[0] = '\0';
    if (!g_started || deck < 0 || deck > 1 || interleaved == nullptr || frames <= 0 ||
        channels <= 0 || sample_rate <= 0.0) {
        return false;
    }
    if (g_cb.pending_load.load(std::memory_order_acquire) >= 0) {
        return false;
    }
    audio::DecodedTrack track;
    track.channels = channels;
    track.sample_rate = sample_rate;
    track.frames = frames;
    track.samples.assign(interleaved, interleaved + frames * channels);
    g_cb.stage[deck] = std::move(track);
    char* t = (deck == 0) ? g_cb.title_a : g_cb.title_b;
    std::snprintf(t, 128, "%s", (title != nullptr && title[0] != '\0') ? title : "stream");
    g_cb.pending_load.store(deck, std::memory_order_release);
    return true;
}

void audio_spike_toggle_play(int deck) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_play_toggle.fetch_or(1 << deck, std::memory_order_release);
}

void audio_spike_set_mixer(float crossfader, float gain_a, float gain_b, float master) {
    g_cb.ui_xf.store(crossfader, std::memory_order_relaxed);
    g_cb.ui_gain_a.store(gain_a, std::memory_order_relaxed);
    g_cb.ui_gain_b.store(gain_b, std::memory_order_relaxed);
    g_cb.ui_master.store(master, std::memory_order_relaxed);
}

void audio_spike_set_pitch(int deck, float pct) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_pitch[deck].store(pct, std::memory_order_relaxed);
}

void audio_spike_seek(int deck, double seconds) {
    if (deck < 0 || deck > 1 || seconds < 0.0) {
        return;
    }
    g_cb.ui_seek_sec[deck].store(static_cast<float>(seconds), std::memory_order_release);
}

void audio_spike_set_eq(int deck, float low_db, float mid_db, float high_db) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_eq[deck][0].store(low_db, std::memory_order_relaxed);
    g_cb.ui_eq[deck][1].store(mid_db, std::memory_order_relaxed);
    g_cb.ui_eq[deck][2].store(high_db, std::memory_order_relaxed);
}

void audio_spike_set_filter(int deck, float pos) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_filter[deck].store(pos, std::memory_order_relaxed);
}

const char* audio_spike_last_error() {
    return g_cb.last_load_error;
}

void audio_spike_start(int seconds) {
    if (g_started) {
        return;
    }
    g_started = true;
    g_stop.store(false, std::memory_order_release);
    g_report = SpikeReport{};
    g_cb.samples.store(0, std::memory_order_relaxed);
    g_cb.callbacks.store(0, std::memory_order_relaxed);
    g_cb.xrun_gaps.store(0, std::memory_order_relaxed);
    g_cb.max_gap_ns.store(0, std::memory_order_relaxed);
    g_cb.device_errors.store(0, std::memory_order_relaxed);
    g_thread = std::thread([seconds] { run_spike(seconds > 0 ? seconds : 5); });
}

int audio_spike_finish() {
    if (!g_started) {
        std::printf("[audio] spike no iniciado\n");
        return 0;
    }
    g_stop.store(true, std::memory_order_release);
    if (g_thread.joinable()) {
        g_thread.join();
    }
    g_started = false;
    std::fputs(g_report.text.c_str(), stdout);
    return g_report.ok ? 0 : 1;
}

} // namespace melodeus::app
