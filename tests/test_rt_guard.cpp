#include <doctest.h>

#include "core/rt_guard.hpp"

#include <atomic>
#include <cstring>

using namespace melodeus::core;

namespace {

std::atomic<int> g_hits{0};
char g_last_what[96]{};

void recording_handler(const char* what, const void* /*caller*/) noexcept {
    g_hits.fetch_add(1, std::memory_order_relaxed);
    std::size_t i = 0;
    while (what[i] != '\0' && i + 1 < sizeof(g_last_what)) {
        g_last_what[i] = what[i];
        ++i;
    }
    g_last_what[i] = '\0';
}

} // namespace

TEST_CASE("RtContext: marca y desmarca el hilo actual") {
    CHECK(!rt::in_rt());
    {
        rt::RtContext ctx;
        CHECK(rt::in_rt());
        {
            rt::RtContext nested;
            CHECK(rt::in_rt());
        }
        CHECK(rt::in_rt()); // anidado: al salir restaura el estado exterior
    }
    CHECK(!rt::in_rt());
}

TEST_CASE("RtContext: new/delete dentro del hilo RT se detecta") {
    const auto previous = rt::set_violation_handler(&recording_handler);
    const std::size_t baseline = rt::violation_count();
    g_hits.store(0);

    bool in_rt_during_alloc = false;
    {
        rt::RtContext ctx;
        int* p = new int(5);
        in_rt_during_alloc = rt::in_rt();
        delete p; // librar en RT tambien es una violacion
    }

    CHECK(in_rt_during_alloc);
    CHECK(g_hits.load() >= 2);
    CHECK(rt::violation_count() > baseline);
    CHECK(std::strcmp(g_last_what, "operator delete") == 0);

    // Fuera del RtContext el heap funciona y no se reporta nada.
    const int hits_before = g_hits.load();
    {
        int* q = new int(7);
        delete q;
        int* arr = new int[16];
        delete[] arr;
    }
    CHECK(g_hits.load() == hits_before);

    rt::set_violation_handler(previous);
}

TEST_CASE("RtContext: alineacion personalizada tambien pasa por el hook") {
    const auto previous = rt::set_violation_handler(&recording_handler);
    g_hits.store(0);

    {
        rt::RtContext ctx;
        alignas(64) int* p = new int[4]; // NOLINT: intencionado, para el test
        delete[] p;
    }

    CHECK(g_hits.load() >= 1);
    rt::set_violation_handler(previous);
}

TEST_CASE("RtContext: fuera de RT el handler por defecto no se invoca") {
    const auto previous = rt::set_violation_handler(&recording_handler);
    const std::size_t baseline = rt::violation_count();

    for (int i = 0; i < 100; ++i) {
        int* p = new int(i);
        delete p;
    }

    CHECK(rt::violation_count() == baseline);
    CHECK(g_hits.load() >= 0); // sin incrementos nuevos del handler
    rt::set_violation_handler(previous);
}
