#pragma once

#include <cstddef>
#include <cstdint>

namespace melodeus::core {

/// Tamaño de línea de caché asumido para el padding de estructuras compartidas
/// entre hilos. 64 es correcto en x86-64 (y en Apple Silicon); en servidores
/// ARM de doble canal serían 128, fuera del alcance de este producto.
inline constexpr std::size_t kCacheLine = 64;

/// Devuelve true si v es potencia de dos y distinto de cero.
[[nodiscard]] constexpr bool is_power_of_two(std::uint64_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

/// Siguiente potencia de dos >= v (v >= 1).
[[nodiscard]] constexpr std::uint64_t next_power_of_two(std::uint64_t v) noexcept {
    if (v <= 1) {
        return 1;
    }
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

} // namespace melodeus::core
