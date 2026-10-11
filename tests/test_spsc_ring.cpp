#include <doctest.h>

#include "core/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using melodeus::core::SpscRing;

namespace {

struct Frame {
    float rms = 0.0f;
    std::uint64_t seq = 0;
    std::uint8_t bands[6]{};
};

} // namespace

TEST_CASE("SpscRing: semantica vacio/lleno basica") {
    SpscRing<std::uint32_t, 4> ring;

    CHECK(ring.capacity() == 4);
    CHECK(ring.empty());
    CHECK(!ring.full());
    CHECK(ring.size_approx() == 0);
    CHECK(ring.free_space() == 4);

    CHECK(ring.try_push(10));
    CHECK(ring.try_push(20));
    CHECK(!ring.empty());
    CHECK(ring.size_approx() == 2);

    std::uint32_t value = 0;
    CHECK(ring.try_pop(value));
    CHECK(value == 10);
    CHECK(ring.try_pop(value));
    CHECK(value == 20);
    CHECK(!ring.try_pop(value));
    CHECK(ring.empty());
}

TEST_CASE("SpscRing: se llena y no pierde ni duplica elementos") {
    SpscRing<std::uint32_t, 4> ring;

    CHECK(ring.try_push(1));
    CHECK(ring.try_push(2));
    CHECK(ring.try_push(3));
    CHECK(ring.try_push(4));
    CHECK(ring.full());
    CHECK(!ring.try_push(5)); // lleno: rechazado, sin corromper nada

    for (std::uint32_t expected = 1; expected <= 4; ++expected) {
        std::uint32_t v = 0;
        CHECK(ring.try_pop(v));
        CHECK(v == expected);
    }
    CHECK(ring.empty());
}

TEST_CASE("SpscRing: la capacidad es potencia de dos (compile-time)") {
    CHECK(melodeus::core::is_power_of_two(64));
    CHECK(!melodeus::core::is_power_of_two(65));
    CHECK(melodeus::core::next_power_of_two(65) == 128);
    CHECK(melodeus::core::next_power_of_two(64) == 64);
}

TEST_CASE("SpscRing: bulk write/read con wrap-around") {
    SpscRing<std::uint32_t, 8> ring;
    std::vector<std::uint32_t> in(6);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<std::uint32_t>(i + 1);
    }

    // Vuelta 1: escribe 6, lee 5 (queda 1 dentro => offset no alineado a 0)
    CHECK(ring.write(in.data(), in.size()) == 6);
    std::uint32_t out[8]{};
    CHECK(ring.read(out, 5) == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(out[i] == i + 1);
    }

    // Vuelta 2: el puntero de escritura da la vuelta sobre el buffer
    CHECK(ring.write(in.data(), in.size()) == 6); // 7 huecos, 6 a escribir
    CHECK(ring.read(out, 8) == 7);                // 1 pendiente + 6 nuevos
    CHECK(out[0] == 6);                           // el pendiente de la vuelta anterior
    CHECK(out[1] == 1);
    CHECK(out[6] == 6);
    CHECK(ring.empty());
}

TEST_CASE("SpscRing: carga multihilo conserva orden y conteo") {
    constexpr std::uint64_t kCount = 2'000'000;
    SpscRing<std::uint64_t, 1024> ring;
    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> received{0};
    std::atomic<bool> order_ok{true};

    std::thread producer([&] {
        std::uint64_t seq = 0;
        while (seq < kCount) {
            if (ring.try_push(seq)) {
                ++seq;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        std::uint64_t value = 0;
        for (;;) {
            if (ring.try_pop(value)) {
                if (value != expected) {
                    order_ok.store(false, std::memory_order_relaxed);
                }
                ++expected;
                received.fetch_add(1, std::memory_order_relaxed);
                if (expected == kCount) {
                    break;
                }
            } else if (producer_done.load(std::memory_order_acquire) && ring.empty()) {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    CHECK(order_ok.load());
    CHECK(received.load() == kCount);
    CHECK(ring.empty());
}

TEST_CASE("SpscRing: estructura de tramos (trivialmente copiable)") {
    SpscRing<Frame, 4> ring;
    Frame f;
    f.rms = 0.5f;
    f.seq = 42;
    f.bands[0] = 7;
    CHECK(ring.try_push(f));

    Frame out{};
    CHECK(ring.try_pop(out));
    CHECK(out.seq == 42);
    CHECK(out.rms == doctest::Approx(0.5f));
    CHECK(out.bands[0] == 7);
}
