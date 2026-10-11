#pragma once

#include <cstddef>

namespace melodeus::core {

/// Guardas de tiempo real: hacen cumplir la regla dura nº1 del proyecto
/// ("prohibido asignar memoria en el hilo de audio") en tiempo de ejecución.
///
/// Mecanismo:
///  * `RtContext` marca el hilo actual como RT (flag thread-local).
///  * Los `operator new/delete` globales (rt_guard.cpp) consultan ese flag y
///    disparan el handler de violación si alguien aloja dentro de un `RtContext`.
///  * En debug, `install_debug_heap_hook()` añade además el hook del CRT de
///    MSVC para capturar también `malloc`/`realloc` crudos.
///
/// El handler por defecto imprime y hace `abort()`. Los tests lo sustituyen
/// por uno que sólo registra, para poder verificar el comportamiento sin
/// matar el proceso.

namespace rt {

using ViolationHandler = void (*)(const char* what, const void* caller) noexcept;

/// ¿Está el hilo actual dentro de un RtContext?
[[nodiscard]] bool in_rt() noexcept;

/// Instala el handler de violaciones (nullptr = handler por defecto).
/// Devuelve el anterior. No debe usarse en producción fuera de tests.
ViolationHandler set_violation_handler(ViolationHandler handler) noexcept;

/// Invocado por los hooks de heap. RT-safe (no aloja, no locks).
void note_violation(const char* what) noexcept;

/// Marca el hilo actual como RT mientras viva el objeto.
class RtContext {
public:
    RtContext() noexcept;
    ~RtContext();

    RtContext(const RtContext&) = delete;
    RtContext& operator=(const RtContext&) = delete;

private:
    bool was_rt_ = false;
};

/// Instala el hook de alloc del CRT de MSVC (debug únicamente).
/// Fuera de un build de debug CRT es un no-op que devuelve false.
/// Debe llamarse una vez, desde el hilo principal, tras el arranque.
bool install_debug_heap_hook() noexcept;

/// Número de violaciones detectadas desde el arranque.
[[nodiscard]] std::size_t violation_count() noexcept;

} // namespace rt
} // namespace melodeus::core
