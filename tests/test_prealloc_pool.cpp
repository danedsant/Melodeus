#include <doctest.h>

#include "core/prealloc_pool.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using melodeus::core::PreallocPool;

namespace {

struct Counted {
    static inline std::atomic<int> alive{0};
    int payload = 0;
    explicit Counted(int p = 0) : payload(p) {
        alive.fetch_add(1);
    }
    ~Counted() {
        alive.fetch_sub(1);
    }
};

} // namespace

TEST_CASE("PreallocPool: crea, destruye y recicla slots") {
    PreallocPool<Counted, 4> pool;

    CHECK(pool.capacity() == 4);
    CHECK(pool.available() == 4);

    Counted* a = pool.create(1);
    Counted* b = pool.create(2);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK(a->payload == 1);
    CHECK(pool.available() == 2);

    pool.destroy(a);
    CHECK(Counted::alive.load() == 1);
    CHECK(pool.available() == 3);

    Counted* c = pool.create(3);
    REQUIRE(c != nullptr);
    CHECK(c == a); // reciclado LIFO
    CHECK(c->payload == 3);

    pool.destroy(b);
    pool.destroy(c);
    CHECK(Counted::alive.load() == 0);
    CHECK(pool.available() == 4);
}

TEST_CASE("PreallocPool: se agota sin ir a heap ni bloquear") {
    PreallocPool<int, 8> pool;
    std::vector<int*> held;
    held.reserve(8);

    for (std::size_t i = 0; i < 8; ++i) {
        int* p = pool.alloc_raw();
        REQUIRE(p != nullptr);
        *p = static_cast<int>(i);
        held.push_back(p);
    }
    CHECK(pool.exhausted());
    CHECK(pool.alloc_raw() == nullptr); // nunca lanza, nunca bloquea
    CHECK(pool.create(0) == nullptr);

    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(pool.index_of(held[i]) < 8);
        CHECK(*held[i] == static_cast<int>(i));
        pool.free_raw(held[i]);
    }
    CHECK(pool.available() == 8);
}

TEST_CASE("PreallocPool: ningun indice duplicado bajo concurrencia") {
    constexpr std::size_t kCapacity = 64;
    constexpr int kThreads = 4;
    constexpr int kIterations = 500;

    PreallocPool<int, kCapacity> pool;
    std::vector<std::atomic<int>> owners(kCapacity);
    for (auto& o : owners) {
        o.store(-1, std::memory_order_relaxed);
    }
    std::atomic<bool> corruption{false};

    auto worker = [&](int tid) {
        for (int it = 0; it < kIterations; ++it) {
            int* p = pool.alloc_raw();
            if (p == nullptr) {
                std::this_thread::yield();
                continue;
            }
            const std::size_t idx = pool.index_of(p);
            const int previous = owners[idx].fetch_add(1, std::memory_order_acq_rel);
            if (previous != -1) {
                corruption.store(true, std::memory_order_relaxed);
            }
            *p = tid; // escritura real: si hubiera alias, se veria

            const int on_free = owners[idx].fetch_sub(1, std::memory_order_acq_rel);
            if (on_free != 0) {
                corruption.store(true, std::memory_order_relaxed);
            }
            pool.free_raw(p);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) {
        th.join();
    }

    CHECK(!corruption.load());
    CHECK(pool.available() == kCapacity);
    for (const auto& o : owners) {
        CHECK(o.load() == -1);
    }
}

TEST_CASE("PreallocPool: index_of devuelve el slot correcto") {
    PreallocPool<int, 4> pool;
    int* p = pool.alloc_raw();
    REQUIRE(p != nullptr);
    const std::size_t idx = pool.index_of(p);
    CHECK(idx < 4);
    pool.free_raw(p);

    int* q = pool.alloc_raw();
    REQUIRE(q != nullptr);
    CHECK(pool.index_of(q) == idx); // LIFO: mismo slot
    pool.free_raw(q);
}
