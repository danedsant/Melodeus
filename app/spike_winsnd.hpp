#pragma once

#include "waveform/waveform.hpp"

namespace melodeus::app {

/// Spike F0.3/F2/F3: abre el audio por JUCE/WASAPI (exclusivo con fallback a
/// compartido de baja latencia) y mezcla 2 pistas por el motor real durante
/// `seconds` (o hasta audio_spike_finish()). F2 añade pirámides de waveform
/// y tap del master; F3 añade carga de archivos con staging seguro y mixer
/// remoto para la UI.
void audio_spike_start(int seconds);

/// Detiene el spike, imprime el informe (dispositivo, modo, latencia,
/// muestras, xruns, tiempo de proceso) y devuelve 0 si todo OK.
int audio_spike_finish();

/// Instantánea para el hilo de render (posiciones, pirámides, mixer, tap).
/// Devuelve false si el audio no corre.
struct AudioSpikeView {
    double deck_a_seconds = 0.0;
    double deck_b_seconds = 0.0;
    double duration_a_seconds = 0.0;
    double duration_b_seconds = 0.0;
    double sample_rate = 48000.0;
    bool playing_a = false;
    bool playing_b = false;
    double pitch_a_pct = 0.0;
    double pitch_b_pct = 0.0;
    float peak_a = 0.0f;
    float peak_b = 0.0f;
    float master_env = 0.0f;
    const char* title_a = "";
    const char* title_b = "";
    const wave::WaveformPyramid* pyramid_a = nullptr;
    const wave::WaveformPyramid* pyramid_b = nullptr;
    /// Último bloque mono publicado (1024 muestras); estable entre cambios
    /// de tap_seq. Recalcular el espectro sólo si cambia el seq.
    const float* tap = nullptr;
    int tap_len = 0;
    unsigned tap_seq = 0;
};

bool audio_spike_view(AudioSpikeView* out);

/// Carga un archivo WAV/FLAC/MP3 en el deck (0=A, 1=B). El decode y la
/// pirámide se hacen en el hilo llamador; el swap es en el hilo de audio
/// al inicio del siguiente callback. false si ya hay una carga pendiente.
bool audio_spike_load(int deck, const char* path, const char* title);

/// Alterna play/stop del deck (aplicado en el hilo de audio).
void audio_spike_toggle_play(int deck);

/// Mixer remoto (aplicado en el hilo de audio con smoothing del motor).
void audio_spike_set_mixer(float crossfader, float gain_a, float gain_b, float master);

} // namespace melodeus::app
