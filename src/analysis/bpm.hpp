#pragma once

#include <cstdint>

namespace melodeus::analysis {

struct BpmAnalysis {
    double bpm = 0.0;
    double offset_sec = 0.0;
};

/// Detecta el BPM de una señal mono (aprox. 70–180) y opcionalmente extrae el
/// offset del primer downbeat (fase). Devuelve 0.0 si la señal es demasiado corta.
/// Optimizado para todo el espectro EDM (House, Techno, Trance, Hardstyle, DnB, Trap, Dubstep).
double detect_bpm(const float* mono, std::int64_t frames, double sample_rate,
                  double* out_offset_sec = nullptr);

/// Versión estructurada con BPM y offset de fase.
BpmAnalysis analyze_bpm(const float* mono, std::int64_t frames, double sample_rate);

} // namespace melodeus::analysis
