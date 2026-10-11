#pragma once

#include <cstdint>

namespace melodeus::analysis {

/// Beatgrid simple: primer acento (offset) + BPM constantes. Suficiente para
/// display de F3; el sync/quantize completo vendrá con el beatgrid BLOB.
struct BeatGrid {
    double offset_sec = 0.0; ///< tiempo del primer beat
    double bpm = 0.0;        ///< 0 = sin grid

    [[nodiscard]] bool valid() const {
        return bpm > 0.0;
    }

    /// Índice del beat más reciente en `pos_sec` (-1 si antes del offset).
    [[nodiscard]] std::int64_t beat_index(double pos_sec) const;

    /// Tiempo del beat `index` en segundos.
    [[nodiscard]] double beat_time(std::int64_t index) const;
};

/// Construye un grid desde el primer onset claro y el BPM detectado.
/// `onset_first_sec` es el tiempo del primer acento (0 si no hay).
BeatGrid make_beatgrid(double bpm, double onset_first_sec);

} // namespace melodeus::analysis
