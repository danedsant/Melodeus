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
    // Pirámides de visualización (leídas ÚNICAMENTE por el hilo de render/UI).
    wave::WaveformPyramid pyr_a;
    wave::WaveformPyramid pyr_b;
    wave::WaveformPyramid staged_pyr[2];
    std::atomic<bool> pyr_ready[2]{{false}, {false}};
    char staged_title[2][128]{};
    char title_a[128] = "";
    char title_b[128] = "";

    // Búferes dobles de pistas decodificadas: tracks[deck][slot].
    // Garantiza que la pista previa NO se destruya mientras el hilo de audio reproduce.
    audio::DecodedTrack tracks[2][2];
    std::atomic<int> active_slot[2]{{0}, {0}};
    std::atomic<int> pending_slot[2]{{-1}, {-1}};
    std::atomic<bool> is_loading[2]{{false}, {false}};

    // Inputs de UI aplicados en el hilo de audio (smoothing del motor).
    std::atomic<float> ui_xf{0.0f};
    std::atomic<float> ui_gain_a{1.0f};
    std::atomic<float> ui_gain_b{1.0f};
    std::atomic<float> ui_master{0.15f};
    std::atomic<int> ui_play_toggle{0}; ///< bit0=A bit1=B
    std::atomic<float> ui_pitch[2]{{0.0f}, {0.0f}};
    std::atomic<float> ui_key_shift[2]{{0.0f}, {0.0f}};
    std::atomic<bool> ui_master_tempo[2]{{false}, {false}};
    std::atomic<float> ui_seek_sec[2]{{-1.0f}, {-1.0f}}; ///< <0 = sin seek
    std::atomic<float> ui_eq[2][3]{};                    ///< [deck][0=lo,1=mid,2=hi] dB
    std::atomic<float> ui_filter[2]{{0.0f}, {0.0f}};     ///< -1 LP .. +1 HP
    std::atomic<float> ui_loop_start[2]{{0.0f}, {0.0f}}; ///< loop start (s)
    std::atomic<float> ui_loop_len[2]{{0.0f}, {0.0f}};   ///< loop len (s); <=0 = off
    char last_load_error[160]{};
    std::atomic<bool> running{false};
    // Solo en el hilo de audio:
    std::chrono::steady_clock::time_point last_cb_time{};
    bool have_last_cb = false;
    long long gap_threshold_ns = 0;
    // Contador de cargas aplicadas por deck (incrementado al hacer el swap).
    std::atomic<int> load_gen[2]{{0}, {0}};
    bool was_playing_on_stop[2] = {false, false};
    double pos_on_stop[2] = {0.0, 0.0};

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        const double r = device->getCurrentSampleRate();
        const double rate = r > 0.0 ? r : 48000.0;
        max_block = static_cast<std::size_t>(std::max(device->getCurrentBufferSizeSamples(), 64));
        std::printf("[audio-dbg] audioDeviceAboutToStart: dev='%s' rate=%.0f buffer=%d\n",
                    device->getName().toRawUTF8(), rate, (int)max_block);
        const double period_s = static_cast<double>(device->getCurrentBufferSizeSamples()) / rate;
        // xrun = llegada de callback con gap > 2.5× periodo nominal (buffer
        // insuficiente: con MMCSS "Pro Audio" un gap así es underrun real).
        gap_threshold_ns = static_cast<long long>(2.5 * period_s * 1.0e9);

        // Si se llama con el motor activo (reconfiguración en vivo), captura estado
        if (engine != nullptr) {
            was_playing_on_stop[0] = engine->deck_a().playing();
            was_playing_on_stop[1] = engine->deck_b().playing();
            pos_on_stop[0] = engine->deck_a().position_seconds();
            pos_on_stop[1] = engine->deck_b().position_seconds();
        }

        engine = std::make_unique<audio::Engine>(audio::EngineConfig{rate, max_block});
        scratch_l.assign(max_block, 0.0f);
        scratch_r.assign(max_block, 0.0f);

        // Curva y balance del motor
        engine->set_curve(audio::CrossfadeCurve::ConstantPower);
        engine->set_crossfader(ui_xf.load(std::memory_order_relaxed));
        engine->set_master_gain(ui_master.load(std::memory_order_relaxed));
        engine->deck_a().set_gain(ui_gain_a.load(std::memory_order_relaxed));
        engine->deck_b().set_gain(ui_gain_b.load(std::memory_order_relaxed));
        engine->set_eq_a(ui_eq[0][0].load(std::memory_order_relaxed),
                         ui_eq[0][1].load(std::memory_order_relaxed),
                         ui_eq[0][2].load(std::memory_order_relaxed));
        engine->set_eq_b(ui_eq[1][0].load(std::memory_order_relaxed),
                         ui_eq[1][1].load(std::memory_order_relaxed),
                         ui_eq[1][2].load(std::memory_order_relaxed));
        engine->set_filter_a(ui_filter[0].load(std::memory_order_relaxed));
        engine->set_filter_b(ui_filter[1].load(std::memory_order_relaxed));

        // Cargar o restaurar pistas en los decks (preserva pistas y posición si ya estaban cargadas)
        for (int d = 0; d < 2; ++d) {
            const int slot = active_slot[d].load(std::memory_order_relaxed);
            audio::DecodedTrack& trk = tracks[d][slot];
            audio::Deck& deck = (d == 0) ? engine->deck_a() : engine->deck_b();
            if (trk.frames > 0 && trk.samples.size() >= 2) {
                deck.load({trk.samples.data(), trk.frames, trk.channels, trk.sample_rate});
                if (pos_on_stop[d] > 0.0) {
                    deck.seek(pos_on_stop[d]);
                }
                if (was_playing_on_stop[d]) {
                    deck.play();
                }
            } else {
                deck.load({nullptr, 0, 0, rate});
            }
            deck.set_pitch_percent(ui_pitch[d].load(std::memory_order_relaxed));
            deck.set_key_shift(static_cast<double>(ui_key_shift[d].load(std::memory_order_relaxed)));
            deck.set_master_tempo(ui_master_tempo[d].load(std::memory_order_relaxed));
            const double l_len = static_cast<double>(ui_loop_len[d].load(std::memory_order_relaxed));
            if (l_len > 0.0) {
                deck.set_loop(static_cast<double>(ui_loop_start[d].load(std::memory_order_relaxed)), l_len);
            }
        }

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
            // Swap seguro O(1) de pista (sin tocar WaveformPyramid y sin allocations)
            for (int d = 0; d < 2; ++d) {
                const int nxt = pending_slot[d].exchange(-1, std::memory_order_acq_rel);
                if (nxt >= 0 && nxt < 2) {
                    audio::DecodedTrack& trk = tracks[d][nxt];
                    if (trk.frames > 0 && trk.samples.size() >= 2) {
                        audio::Deck& deck = (d == 0) ? engine->deck_a() : engine->deck_b();
                        // load() deja el deck en pausa: sólo sigue sonando si ya
                        // estaba reproduciendo (una carga no debe arrancar sola).
                        const bool was_playing = deck.playing();
                        deck.load({trk.samples.data(), trk.frames, trk.channels,
                                   trk.sample_rate});
                        if (was_playing) {
                            deck.play();
                        }
                        active_slot[d].store(nxt, std::memory_order_release);
                        load_gen[d].fetch_add(1, std::memory_order_release);
                    }
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
            // Pitch / key shift / master tempo / seek (CUE) / EQ / filtro / loop (F3.5/F3.7).
            for (int d = 0; d < 2; ++d) {
                audio::Deck& deck = (d == 0) ? engine->deck_a() : engine->deck_b();
                deck.set_pitch_percent(ui_pitch[d].load(std::memory_order_relaxed));
                deck.set_key_shift(static_cast<double>(ui_key_shift[d].load(std::memory_order_relaxed)));
                deck.set_master_tempo(ui_master_tempo[d].load(std::memory_order_relaxed));
                const float seek_to = ui_seek_sec[d].exchange(-1.0f, std::memory_order_acq_rel);
                if (seek_to >= 0.0f) {
                    deck.seek(static_cast<double>(seek_to));
                }
                const double loop_len = static_cast<double>(ui_loop_len[d].load(std::memory_order_relaxed));
                if (loop_len > 0.0) {
                    deck.set_loop(static_cast<double>(ui_loop_start[d].load(std::memory_order_relaxed)),
                                  loop_len);
                } else {
                    deck.clear_loop();
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

    void audioDeviceError(const juce::String& err) override {
        std::printf("[audio-dbg] audioDeviceError: %s\n", err.toRawUTF8());
        device_errors.fetch_add(1, std::memory_order_relaxed);
    }

    void audioDeviceStopped() override {
        std::printf("[audio-dbg] audioDeviceStopped llamado! running->false\n");
        if (engine != nullptr) {
            was_playing_on_stop[0] = engine->deck_a().playing();
            was_playing_on_stop[1] = engine->deck_b().playing();
            pos_on_stop[0] = engine->deck_a().position_seconds();
            pos_on_stop[1] = engine->deck_b().position_seconds();
        }
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
    setup.sampleRate = 0.0; // 0 = formato nativo de Windows (ej. 48000 Hz)
    setup.bufferSize = 0;   // 0 = preferencia nativa del dispositivo/Windows
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;

    juce::String err = mgr.setAudioDeviceSetup(setup, false);
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
        {"Windows Audio", "compartido"},
        {"Windows Audio (Low Latency Mode)", "compartido low-latency (IAudioClient3)"},
        {"Windows Audio (Exclusive Mode)", "exclusivo"},
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
    // Intercambio seguro de WaveformPyramid y título en el hilo de render (fuera de fill_lane).
    for (int d = 0; d < 2; ++d) {
        if (g_cb.pyr_ready[d].exchange(false, std::memory_order_acq_rel)) {
            wave::WaveformPyramid& pyr = (d == 0) ? g_cb.pyr_a : g_cb.pyr_b;
            pyr = std::move(g_cb.staged_pyr[d]);
            char* t = (d == 0) ? g_cb.title_a : g_cb.title_b;
            std::snprintf(t, 128, "%s", g_cb.staged_title[d]);
        }
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
    out->key_shift_a = engine.deck_a().key_shift();
    out->key_shift_b = engine.deck_b().key_shift();
    out->master_tempo_a = engine.deck_a().master_tempo();
    out->master_tempo_b = engine.deck_b().master_tempo();
    out->loading_a = g_cb.is_loading[0].load(std::memory_order_acquire);
    out->loading_b = g_cb.is_loading[1].load(std::memory_order_acquire);
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
    out->load_gen[0] = g_cb.load_gen[0].load(std::memory_order_acquire);
    out->load_gen[1] = g_cb.load_gen[1].load(std::memory_order_acquire);
    return true;
}

bool audio_spike_is_loading(int deck) {
    if (deck < 0 || deck > 1) {
        return false;
    }
    return g_cb.is_loading[deck].load(std::memory_order_acquire);
}

bool audio_spike_load_async(int deck, const char* path, const char* title) {
    g_cb.last_load_error[0] = '\0';
    if (!g_started || deck < 0 || deck > 1 || path == nullptr || path[0] == '\0') {
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error),
                      "carga inválida (deck %d)", deck);
        return false;
    }
    if (g_cb.is_loading[deck].load(std::memory_order_acquire) ||
        g_cb.pending_slot[deck].load(std::memory_order_acquire) >= 0) {
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error),
                      "carga anterior aún en progreso");
        return false;
    }
    g_cb.is_loading[deck].store(true, std::memory_order_release);
    const std::string s_path(path);
    const std::string s_title((title != nullptr && title[0] != '\0') ? title : path);

    std::thread([deck, s_path, s_title]() {
        const int slot = g_cb.active_slot[deck].load(std::memory_order_relaxed) ^ 1;
        audio::DecodedTrack decoded;
        std::string error;
        if (!audio::decode_audio_file(s_path, &decoded, &error) || decoded.frames <= 0) {
            std::printf("[f3] decode falló: %s (%s)\n", s_path.c_str(), error.c_str());
            std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error), "decode falló: %s",
                          error.empty() ? "formato no soportado" : error.c_str());
            g_cb.is_loading[deck].store(false, std::memory_order_release);
            return;
        }
        g_cb.staged_pyr[deck].build(decoded.samples.data(), decoded.frames, decoded.channels,
                                   decoded.sample_rate);
        g_cb.tracks[deck][slot] = std::move(decoded);
        std::snprintf(g_cb.staged_title[deck], sizeof(g_cb.staged_title[deck]), "%s", s_title.c_str());
        g_cb.pending_slot[deck].store(slot, std::memory_order_release);
        g_cb.pyr_ready[deck].store(true, std::memory_order_release);
        g_cb.is_loading[deck].store(false, std::memory_order_release);
    }).detach();

    return true;
}

bool audio_spike_load(int deck, const char* path, const char* title) {
    g_cb.last_load_error[0] = '\0';
    if (!g_started || deck < 0 || deck > 1 || path == nullptr || path[0] == '\0') {
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error),
                      "carga inválida (deck %d)", deck);
        return false;
    }
    audio::DecodedTrack decoded;
    std::string error;
    if (!audio::decode_audio_file(path, &decoded, &error) || decoded.frames <= 0) {
        std::printf("[f3] decode falló: %s (%s)\n", path, error.c_str());
        std::snprintf(g_cb.last_load_error, sizeof(g_cb.last_load_error), "decode falló: %s",
                      error.empty() ? "formato no soportado" : error.c_str());
        return false;
    }
    const int slot = g_cb.active_slot[deck].load(std::memory_order_relaxed) ^ 1;
    g_cb.staged_pyr[deck].build(decoded.samples.data(), decoded.frames, decoded.channels,
                               decoded.sample_rate);
    g_cb.tracks[deck][slot] = std::move(decoded);
    std::snprintf(g_cb.staged_title[deck], sizeof(g_cb.staged_title[deck]), "%s",
                  (title != nullptr && title[0] != '\0') ? title : path);
    g_cb.pending_slot[deck].store(slot, std::memory_order_release);
    g_cb.pyr_ready[deck].store(true, std::memory_order_release);
    return true;
}

bool audio_spike_load_samples(int deck, const float* interleaved, std::int64_t frames, int channels,
                              double sample_rate, const char* title) {
    g_cb.last_load_error[0] = '\0';
    if (!g_started || deck < 0 || deck > 1 || interleaved == nullptr || frames <= 0 ||
        channels <= 0 || sample_rate <= 0.0) {
        return false;
    }
    audio::DecodedTrack track;
    track.channels = channels;
    track.sample_rate = sample_rate;
    track.frames = frames;
    track.samples.assign(interleaved, interleaved + frames * channels);
    const int slot = g_cb.active_slot[deck].load(std::memory_order_relaxed) ^ 1;
    g_cb.staged_pyr[deck].build(track.samples.data(), track.frames, track.channels,
                               track.sample_rate);
    g_cb.tracks[deck][slot] = std::move(track);
    std::snprintf(g_cb.staged_title[deck], sizeof(g_cb.staged_title[deck]), "%s",
                  (title != nullptr && title[0] != '\0') ? title : "stream");
    g_cb.pending_slot[deck].store(slot, std::memory_order_release);
    g_cb.pyr_ready[deck].store(true, std::memory_order_release);
    return true;
}

void audio_spike_toggle_play(int deck) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_play_toggle.fetch_or(1 << deck, std::memory_order_release);
}

void audio_spike_set_mixer(float crossfader, float gain_a, float gain_b, float master) {
    // Mapeo UI crossfader [0.0, 1.0] (0 = Deck A, 0.5 = Centro, 1 = Deck B) a
    // espacio del motor [-1.0, 1.0] (+1 = Solo A, 0 = Centro 50/50, -1 = Solo B).
    const float engine_xf = 1.0f - 2.0f * crossfader;
    g_cb.ui_xf.store(engine_xf, std::memory_order_relaxed);
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

void audio_spike_set_key_shift(int deck, float semitones) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_key_shift[deck].store(semitones, std::memory_order_relaxed);
}

void audio_spike_set_master_tempo(int deck, bool on) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_master_tempo[deck].store(on, std::memory_order_relaxed);
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

void audio_spike_set_loop(int deck, double start_sec, double len_sec) {
    if (deck < 0 || deck > 1) {
        return;
    }
    g_cb.ui_loop_start[deck].store(static_cast<float>(start_sec), std::memory_order_relaxed);
    g_cb.ui_loop_len[deck].store(static_cast<float>(len_sec), std::memory_order_relaxed);
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
