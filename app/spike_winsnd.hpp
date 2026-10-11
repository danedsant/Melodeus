#pragma once

namespace melodeus::app {

/// Spike F0.3: abre el audio por JUCE/WASAPI (exclusivo con fallback a
/// compartido de baja latencia) y emite un tono de 440 Hz en paralelo a la
/// ventana durante `seconds` (o hasta audio_spike_finish()).
void audio_spike_start(int seconds);

/// Detiene el spike, imprime el informe (dispositivo, modo, latencia,
/// muestras emitidas) y devuelve 0 si el dispositivo abrió y emitió.
int audio_spike_finish();

} // namespace melodeus::app
