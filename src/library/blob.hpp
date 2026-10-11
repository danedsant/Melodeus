#pragma once

#include "analysis/beatgrid.hpp"

#include <cstdint>
#include <vector>

namespace melodeus::library {

/// Serialización del beatgrid para `analysis_blobs` (kind=0). Formato v1:
/// float64 bpm, float64 offset_sec (little-endian).
std::vector<std::uint8_t> pack_beatgrid(const analysis::BeatGrid& grid);

/// Deserializa; devuelve false si el buffer no tiene el tamaño mínimo.
bool unpack_beatgrid(const std::uint8_t* data, std::size_t size, analysis::BeatGrid* out);

/// kind para analysis_blobs.
enum AnalysisBlobKind : int {
    kBlobBeatgrid = 0,
    kBlobWaveform = 1,
    kBlobCues = 2,
};

} // namespace melodeus::library
