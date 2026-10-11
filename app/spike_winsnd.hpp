#pragma once

#include "waveform/waveform.hpp"

namespace melodeus::app {

/// Spike F0.3/F2: abre el audio por JUCE/WASAPI (exclusivo con fallback a
/// compartido de baja latencia) y mezcla 2 pistas sintéticas por el motor
/// real durante `seconds` (o hasta audio_spike_finish()). F2 añade pirámides
/// de waveform por deck y un tap del master para el espectro.
void audio_spike_start(int seconds);

/// Detiene el spike, imprime el informe (dispositivo, modo, latencia,
/// muestras, xruns, tiempo de proceso) y devuelve 0 si todo OK.
int audio_spike_finish();

/// Instantánea para el hilo de render (posiciones, pirámides inmutables y
/// tap del master en doble búfer SPSC). Devuelve false si el audio no corre.
struct AudioSpikeView {
    double deck_a_seconds = 0.0;
    double deck_b_seconds = 0.0;
    double sample_rate = 48000.0;
    const wave::WaveformPyramid* pyramid_a = nullptr;
    const wave::WaveformPyramid* pyramid_b = nullptr;
    /// Último bloque mono publicado (1024 muestras); estable entre cambios
    /// de tap_seq. Recalcular el espectro sólo si cambia el seq.
    const float* tap = nullptr;
    int tap_len = 0;
    unsigned tap_seq = 0;
};

bool audio_spike_view(AudioSpikeView* out);

} // namespace melodeus::app
