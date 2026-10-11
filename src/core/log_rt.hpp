#pragma once

#include "core/spsc_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#include <sal.h>
#endif

namespace melodeus::core {

enum class LogLevel : std::uint8_t { trace = 0, info = 1, warn = 2, error = 3 };

/// Registro de log de tamaño fijo: copiable con memcpy, sin posesiones.
struct LogRecord {
    char message[120]{};
    std::uint64_t qpc_ticks = 0; // QueryPerformanceCounter
    std::uint32_t thread_id = 0;
    std::uint8_t length = 0;
    LogLevel level = LogLevel::info;
};

/// Logger RT-safe.
///
/// El hilo de audio (o cualquier hilo con RtContext) escribe con `write()`;
/// nunca bloquea, nunca aloja, y si el ring está lleno descarta y cuenta el
/// descarte. Un consumidor (hilo de UI o de telemetría) vacía con `drain()`.
///
/// Uso: calentar `instance()` en el arranque (main) antes del primer callback.
class RtLogger {
public:
    static RtLogger& instance() noexcept;

    /// Productor. RT-safe: bounded, sin asignación, sin locks.
    void write(LogLevel level, const char* message) noexcept;

    /// Productor con formato. `fmt` debe ser de longitud acotada y usar
    /// especificadores estándar (RT: sin anchos dinámicos gigantes).
    void writef(LogLevel level,
#if defined(_MSC_VER)
                _Printf_format_string_
#endif
                const char* fmt,
                ...) noexcept;

    /// Consumidor: extrae hasta `max` registros. Devuelve cuántos.
    std::size_t drain(LogRecord* out, std::size_t max) noexcept;

    /// Umbral mínimo a registrar (por debajo se descarta en el productor).
    void set_min_level(LogLevel level) noexcept;
    [[nodiscard]] LogLevel min_level() const noexcept;

    /// Mensajes descartados por ring lleno desde el arranque.
    [[nodiscard]] std::size_t dropped() const noexcept;

    static constexpr std::size_t kRingCapacity = 1024;

private:
    RtLogger() = default;

    SpscRing<LogRecord, kRingCapacity> ring_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint32_t> min_level_{static_cast<std::uint32_t>(LogLevel::info)};
};

} // namespace melodeus::core
