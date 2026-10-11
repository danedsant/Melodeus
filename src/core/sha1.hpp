#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace melodeus::core {

/// SHA-1 mínimo (FIPS 180-1) para `audio_sha1` de los BLOBs de análisis.
/// Suficiente para invalidación de caché; no para seguridad.
std::array<std::uint8_t, 20> sha1(const void* data, std::size_t size);

} // namespace melodeus::core
