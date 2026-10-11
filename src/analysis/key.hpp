#pragma once

#include <cstdint>

namespace melodeus::analysis {

/// Tonalidad detectada: raíz cromática (0=C..11=B), modo, y notación
/// Camelot (1A..12A menor, 1B..12B mayor) + nombre tradicional.
struct KeyInfo {
    int root_pc = 0;           ///< 0=C .. 11=B (pitch class)
    bool minor = false;        ///< true = menor, false = mayor
    int camelot = 0;           ///< 1..12
    bool camelot_flat = false; ///< false = sufijo B (mayor), true = A (menor)
    float confidence = 0.0f;   ///< correlación de Pearson de la mejor clave

    /// Nombre tradicional, p.ej. "A", "C#m", "Eb". Apunta a literal estático.
    const char* traditional_name() const;

    /// Nombre Camelot, p.ej. "8A", "11B". Apunta a literal estático.
    const char* camelot_name() const;
};

/// Detecta la tonalidad (Krumhansl-Schmuckler sobre croma STFT). Devuelve
/// confidence ~0 si la señal no tiene croma claro. `rate` = sample rate.
KeyInfo detect_key(const float* mono, std::int64_t frames, double sample_rate);

} // namespace melodeus::analysis
