#include "core/log_rt.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace melodeus::core {
namespace {

std::uint64_t now_qpc() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter); // en Win10+ no transiciona al kernel
    return static_cast<std::uint64_t>(counter.QuadPart);
#else
    return 0;
#endif
}

std::uint32_t current_thread_id() noexcept {
#if defined(_WIN32)
    return ::GetCurrentThreadId(); // lectura de TEB, sin syscall
#else
    return 0;
#endif
}

std::size_t bounded_length(const char* s, std::size_t max) noexcept {
    std::size_t i = 0;
    while (i < max && s[i] != '\0') {
        ++i;
    }
    return i;
}

} // namespace

RtLogger& RtLogger::instance() noexcept {
    // Static local: inicialización de guard thread-safe y sin asignaciones.
    static RtLogger logger;
    return logger;
}

void RtLogger::write(LogLevel level, const char* message) noexcept {
    if (static_cast<std::uint32_t>(level) < min_level_.load(std::memory_order_relaxed)) {
        return;
    }
    LogRecord record;
    record.level = level;
    record.qpc_ticks = now_qpc();
    record.thread_id = current_thread_id();
    const std::size_t len = bounded_length(message, sizeof(record.message) - 1);
    std::memcpy(record.message, message, len);
    record.message[len] = '\0';
    record.length = static_cast<std::uint8_t>(len);

    if (!ring_.try_push(record)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void RtLogger::writef(LogLevel level, const char* fmt, ...) noexcept {
    if (static_cast<std::uint32_t>(level) < min_level_.load(std::memory_order_relaxed)) {
        return;
    }
    char buffer[sizeof(LogRecord::message)];
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        buffer[0] = '\0';
    }
    write(level, buffer);
}

std::size_t RtLogger::drain(LogRecord* out, std::size_t max) noexcept {
    return ring_.read(out, max);
}

void RtLogger::set_min_level(LogLevel level) noexcept {
    min_level_.store(static_cast<std::uint32_t>(level), std::memory_order_relaxed);
}

LogLevel RtLogger::min_level() const noexcept {
    return static_cast<LogLevel>(min_level_.load(std::memory_order_relaxed));
}

std::size_t RtLogger::dropped() const noexcept {
    return static_cast<std::size_t>(dropped_.load(std::memory_order_relaxed));
}

} // namespace melodeus::core
