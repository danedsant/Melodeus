#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "core/rt_guard.hpp"

int main(int argc, char** argv) {
    // En debug CRT, tambien captura malloc/free crudos dentro del hilo RT.
    melodeus::core::rt::install_debug_heap_hook();

    doctest::Context context(argc, argv);
    return context.run();
}
