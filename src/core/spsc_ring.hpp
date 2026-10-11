#pragma once

#include "core/hardware.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace melodeus::core {

/// Ring buffer lock-free de productor único / consumidor único (SPSC).
///
/// Garantías exigidas por el camino de audio de Melodeus:
///  * Ninguna asignación tras la construcción (almacenamiento en línea).
///  * Ningún mutex: sólo atomics con ordenación acquire/release.
///  * Sin falso sharing: los cursores productor/consumidor están en líneas de
///    caché distintas, y el búfer empieza en línea de caché.
///  * Capacidad fija y potencia de dos (máscara en vez de división).
///
/// Los cursores son monótonos y crecen sin cesar; el desbordamiento de
/// uint64_t ocurriría tras 5.8e11 millones de elementos (irrelevante: a
/// 48000 pushes/s serían ~3.800 años).
///
/// Precondiciones de uso:
///  * Sólo puede haber UN hilo llamando a write()/try_push() en todo momento.
///  * Sólo puede haber UN hilo llamando a read()/try_pop() en todo momento.
///  * Si ambos roles los cumple el mismo hilo, también es correcto.
template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(is_power_of_two(Capacity), "Capacity debe ser potencia de dos");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T debe ser trivialmente copiable (path RT sin destructores)");
    static_assert(Capacity >= 2, "Capacity minima 2");

public:
    using value_type = T;

    SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    // ---------------------------------------------------------- productor

    /// Escribe un elemento. Devuelve false si el ring está lleno.
    /// RT-safe: sin asignación, sin locks, bounded.
    bool try_push(const T& value) noexcept {
        const auto w = write_index_.load(std::memory_order_relaxed);
        const auto r = read_index_.load(std::memory_order_acquire);
        if (Capacity - (w - r) < 1) {
            return false;
        }
        buffer_[w & kMask] = value;
        write_index_.store(w + 1, std::memory_order_release);
        return true;
    }

    /// Escribe hasta `count` elementos. Devuelve los realmente escritos.
    /// Si el ring no admite todos, escribe lo que quepa (no bloquea nunca).
    std::size_t write(const T* src, std::size_t count) noexcept {
        const auto w = write_index_.load(std::memory_order_relaxed);
        const auto r = read_index_.load(std::memory_order_acquire);
        const auto free = Capacity - (w - r);
        const auto n = count < free ? count : free;
        for (std::size_t i = 0; i < n; ++i) {
            buffer_[(w + i) & kMask] = src[i];
        }
        write_index_.store(w + n, std::memory_order_release);
        return n;
    }

    // --------------------------------------------------------- consumidor

    /// Extrae un elemento. Devuelve false si el ring está vacío.
    bool try_pop(T& out) noexcept {
        const auto r = read_index_.load(std::memory_order_relaxed);
        const auto w = write_index_.load(std::memory_order_acquire);
        if (w - r < 1) {
            return false;
        }
        out = buffer_[r & kMask];
        read_index_.store(r + 1, std::memory_order_release);
        return true;
    }

    /// Extrae hasta `count` elementos. Devuelve los realmente leídos.
    std::size_t read(T* dst, std::size_t count) noexcept {
        const auto r = read_index_.load(std::memory_order_relaxed);
        const auto w = write_index_.load(std::memory_order_acquire);
        const auto available = w - r;
        const auto n = count < available ? count : available;
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = buffer_[(r + i) & kMask];
        }
        read_index_.store(r + n, std::memory_order_release);
        return n;
    }

    // ------------------------------------------------------------ estado

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const auto w = write_index_.load(std::memory_order_acquire);
        const auto r = read_index_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(w - r);
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_index_.load(std::memory_order_acquire) ==
               write_index_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool full() const noexcept {
        const auto w = write_index_.load(std::memory_order_acquire);
        const auto r = read_index_.load(std::memory_order_acquire);
        return (w - r) == Capacity;
    }

    /// Elementos libres para el productor (lectura aproximada).
    [[nodiscard]] std::size_t free_space() const noexcept {
        const auto w = write_index_.load(std::memory_order_acquire);
        const auto r = read_index_.load(std::memory_order_acquire);
        return Capacity - static_cast<std::size_t>(w - r);
    }

private:
    static constexpr std::uint64_t kMask = Capacity - 1;

    // Productor escribe write_index_ y lee read_index_; el consumidor hace lo
    // contrario. Cada cursor en su propia línea para evitar invalidación mutua.
    alignas(kCacheLine) std::atomic<std::uint64_t> write_index_{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> read_index_{0};
    alignas(kCacheLine) T buffer_[Capacity]{};
};

} // namespace melodeus::core
