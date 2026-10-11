#include <doctest.h>

#include "core/log_rt.hpp"
#include "core/rt_guard.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace melodeus::core;

namespace {

std::atomic<int> g_rt_hits{0};

void counting_handler(const char* /*what*/, const void* /*caller*/) noexcept {
    g_rt_hits.fetch_add(1, std::memory_order_relaxed);
}

void drain_all(RtLogger& log) {
    LogRecord buffer[32];
    while (log.drain(buffer, 32) > 0) {
    }
}

} // namespace

TEST_CASE("RtLogger: escribe dentro de RtContext sin violaciones de heap") {
    const auto previous = rt::set_violation_handler(&counting_handler);
    g_rt_hits.store(0);

    auto& log = RtLogger::instance();
    log.set_min_level(LogLevel::trace);
    drain_all(log);

    const std::size_t violations_before = rt::violation_count();
    {
        rt::RtContext ctx;
        log.write(LogLevel::info, "bloque de audio procesado");
    }

    CHECK(g_rt_hits.load() == 0);
    CHECK(rt::violation_count() == violations_before);

    LogRecord out[4];
    const std::size_t n = log.drain(out, 4);
    REQUIRE(n == 1);
    CHECK(out[0].level == LogLevel::info);
    CHECK(out[0].length == std::strlen("bloque de audio procesado"));
    CHECK(std::strcmp(out[0].message, "bloque de audio procesado") == 0);
    CHECK(out[0].qpc_ticks != 0);
    CHECK(out[0].thread_id != 0);

    rt::set_violation_handler(previous);
    log.set_min_level(LogLevel::info);
}

TEST_CASE("RtLogger: el formato acotado llega correcto") {
    auto& log = RtLogger::instance();
    log.set_min_level(LogLevel::trace);
    drain_all(log);

    log.writef(LogLevel::warn, "underrun deck %d (%s)", 2, "master");
    log.writef(LogLevel::error, "%s", "fallo del driver");

    LogRecord out[4];
    const std::size_t n = log.drain(out, 4);
    REQUIRE(n == 2);
    CHECK(out[0].level == LogLevel::warn);
    CHECK(std::strcmp(out[0].message, "underrun deck 2 (master)") == 0);
    CHECK(out[1].level == LogLevel::error);
    CHECK(std::strcmp(out[1].message, "fallo del driver") == 0);
}

TEST_CASE("RtLogger: el anillo se desborda sin perder la forma") {
    auto& log = RtLogger::instance();
    log.set_min_level(LogLevel::trace);
    drain_all(log);

    constexpr std::size_t kTotal = RtLogger::kRingCapacity + 5;
    for (std::size_t i = 0; i < kTotal; ++i) {
        log.write(LogLevel::trace, "x");
    }

    CHECK(log.dropped() == 5);

    std::vector<LogRecord> out(RtLogger::kRingCapacity);
    const std::size_t got = log.drain(out.data(), out.size());
    CHECK(got == RtLogger::kRingCapacity);
    CHECK(log.dropped() == 5); // drain no reinicia contadores
}

TEST_CASE("RtLogger: min_level filtra antes de tocar el anillo") {
    auto& log = RtLogger::instance();
    log.set_min_level(LogLevel::warn);
    drain_all(log);

    log.write(LogLevel::trace, "no debe aparecer");
    log.write(LogLevel::info, "tampoco");
    log.write(LogLevel::warn, "si");
    log.write(LogLevel::error, "tambien si");

    LogRecord out[8];
    const std::size_t n = log.drain(out, 8);
    REQUIRE(n == 2);
    CHECK(out[0].level == LogLevel::warn);
    CHECK(out[1].level == LogLevel::error);

    log.set_min_level(LogLevel::info);
}

TEST_CASE("RtLogger: productor y consumidor concurrentes cierran el conteo") {
    auto& log = RtLogger::instance();
    log.set_min_level(LogLevel::trace);
    drain_all(log);
    const std::size_t dropped_before = log.dropped();

    constexpr std::size_t kWrites = 5000;
    std::atomic<bool> done{false};
    std::size_t received = 0;

    std::thread producer([&] {
        for (std::size_t i = 0; i < kWrites; ++i) {
            log.writef(LogLevel::info, "m%zu", i);
        }
        done.store(true, std::memory_order_release);
    });

    LogRecord batch[64];
    while (true) {
        const std::size_t n = log.drain(batch, 64);
        received += n;
        if (done.load(std::memory_order_acquire) && n == 0) {
            // ultima barrida por si quedo algo entre la lectura y done
            received += log.drain(batch, 64);
            break;
        }
        if (n == 0) {
            std::this_thread::yield();
        }
    }
    producer.join();

    const std::size_t dropped = log.dropped() - dropped_before;
    CHECK(received + dropped == kWrites);
    CHECK(received >= 1000); // el consumidor no puede ir tan detras
}
