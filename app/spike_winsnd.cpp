#include "spike_winsnd.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

namespace melodeus::app {
namespace {

constexpr double kToneHz = 440.0;
constexpr double kPi = 3.14159265358979323846;

struct ToneCallback final : juce::AudioIODeviceCallback {
    std::atomic<long long> samples{0};
    std::atomic<int> callbacks{0};
    double rate = 48000.0;
    double phase = 0.0;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        const double r = device->getCurrentSampleRate();
        rate = r > 0.0 ? r : 48000.0;
        phase = 0.0;
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output,
                                          int num_out, int num_samples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        const double inc = kToneHz / rate;
        for (int i = 0; i < num_samples; ++i) {
            const float v = static_cast<float>(0.2 * std::sin(phase * 2.0 * kPi));
            phase += inc;
            if (phase >= 1.0) {
                phase -= 1.0;
            }
            for (int ch = 0; ch < num_out; ++ch) {
                if (output[ch] != nullptr) {
                    output[ch][i] = v;
                }
            }
        }
        samples.fetch_add(num_samples, std::memory_order_relaxed);
        callbacks.fetch_add(1, std::memory_order_relaxed);
    }

    void audioDeviceStopped() override {
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
ToneCallback g_cb;
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
    const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double expected = wall * rate;
    const std::string type_name =
        mgr.getCurrentDeviceTypeObject() != nullptr
            ? mgr.getCurrentDeviceTypeObject()->getTypeName().toStdString()
            : "(sin tipo)";
    juce::Logger::setCurrentLogger(nullptr);

    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "[audio] modo=%s\n"
                  "[audio] tipo-juce=%s\n"
                  "[audio] dispositivo=%s\n"
                  "[audio] rate=%.0f Hz  buffer=%d f  latencia salida=%d f\n"
                  "[audio] LATENCIA (buffer+salida) = %.2f ms\n"
                  "[audio] wall=%.2f s  callbacks=%d  muestras=%lld (esperado~%.0f, %.1f%%)\n",
                  chosen->label, type_name.c_str(), device_name.c_str(), rate, buffer, latency,
                  total_ms, wall, g_cb.callbacks.load(std::memory_order_relaxed), n, expected,
                  expected > 0.0 ? 100.0 * static_cast<double>(n) / expected : 0.0);
    rep.ok = n > 0;
    rep.text = buf;
    g_report = std::move(rep);
}

} // namespace

void audio_spike_start(int seconds) {
    if (g_started) {
        return;
    }
    g_started = true;
    g_stop.store(false, std::memory_order_release);
    g_report = SpikeReport{};
    g_cb.samples.store(0, std::memory_order_relaxed);
    g_cb.callbacks.store(0, std::memory_order_relaxed);
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
