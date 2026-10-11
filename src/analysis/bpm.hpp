#pragma once

#include <cstdint>

namespace melodeus::analysis {

/// Detecta el BPM de una señal mono (aprox. 70–180). Devuelve 0.0 si la
/// señal es demasiado corta o no hay picos claros. Algoritmo: envolvente de
/// onset (diferencia de energía por hop) + autocorrelación con corrección
/// de octava hacia la banda DJ (90–150 preferida).
double detect_bpm(const float* mono, std::int64_t frames, double sample_rate);

} // namespace melodeus::analysis
