#include "core/rt_guard.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <intrin.h>
#include <malloc.h>
#else
#include <cstdlib>
#endif

#if defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

// Ruta única de alojamiento: todo el proceso (C y C++) va por aquí y se
// libera por `sys_free`, así cualquier `operator delete` coincide con su
// `operator new` sin mezclar malloc()/_aligned_malloc().
void* sys_alloc(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0) {
        size = 1;
    }
    if (alignment < alignof(std::max_align_t)) {
        alignment = alignof(std::max_align_t);
    }
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment); // alineación: potencia de dos >= 8
#else
    const std::size_t rounded = ((size + alignment - 1) / alignment) * alignment;
    return std::aligned_alloc(alignment, rounded);
#endif
}

void sys_free(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace

namespace melodeus::core::rt {
namespace {

std::atomic<ViolationHandler> g_handler{nullptr};
std::atomic<std::size_t> g_violations{0};

thread_local bool g_in_rt = false;
thread_local bool g_in_violation = false; // protege contra reentrada del handler

void default_handler(const char* what, const void* /*caller*/) noexcept {
    // Sólo se llega aquí con el programa a punto de abortar: escribir en
    // stderr es un syscall, pero ya no estamos en el camino normal.
    std::fprintf(stderr, "[melodeus] RT-GUARD: operacion de heap en el hilo de audio: %s\n", what);
    std::fflush(stderr);
    std::abort();
}

} // namespace

bool in_rt() noexcept {
    return g_in_rt;
}

ViolationHandler set_violation_handler(ViolationHandler handler) noexcept {
    return g_handler.exchange(handler, std::memory_order_acq_rel);
}

void note_violation(const char* what) noexcept {
    if (!g_in_rt) {
        return; // camino normal del proceso: nada que reportar
    }
    g_violations.fetch_add(1, std::memory_order_relaxed);
    if (g_in_violation) {
        return; // reentrada desde el propio handler (CRT/stderr)
    }
    g_in_violation = true;
    ViolationHandler handler = g_handler.load(std::memory_order_acquire);
    if (handler == nullptr) {
        handler = &default_handler;
    }
    const void* caller =
#if defined(_MSC_VER)
        _ReturnAddress();
#else
        nullptr;
#endif
    handler(what, caller);
    g_in_violation = false;
}

RtContext::RtContext() noexcept : was_rt_(g_in_rt) {
    g_in_rt = true;
}

RtContext::~RtContext() {
    g_in_rt = was_rt_;
}

std::size_t violation_count() noexcept {
    return g_violations.load(std::memory_order_relaxed);
}

bool install_debug_heap_hook() noexcept {
#if defined(_DEBUG) && defined(_MSC_VER)
    _CrtSetAllocHook([](int alloc_type, void* /*userData*/, std::size_t /*size*/, int /*blockType*/,
                        long /*request*/, const unsigned char* /*file*/, int /*line*/) -> int {
        if (alloc_type == FREE_BLOCK) {
            if (g_in_rt) {
                note_violation("free");
            }
        } else if (g_in_rt) {
            note_violation(alloc_type == REALLOC_BLOCK ? "realloc" : "malloc");
        }
        return TRUE;
    });
    return true;
#else
    return false;
#endif
}

} // namespace melodeus::core::rt

// ---------------------------------------------------------------------------
// Overrides globales de new/delete (todas las variantes: si faltara alguna,
// el CRT usaría su versión y el delete correspondiente liberase memoria de
// otra allocator => UB).
// ---------------------------------------------------------------------------

void* operator new(std::size_t size) {
    melodeus::core::rt::note_violation("operator new");
    void* p = sys_alloc(size, alignof(std::max_align_t));
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t align) {
    melodeus::core::rt::note_violation("operator new(aligned)");
    void* p = sys_alloc(size, static_cast<std::size_t>(align));
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size, std::align_val_t align) {
    return ::operator new(size, align);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    melodeus::core::rt::note_violation("operator new(nothrow)");
    return sys_alloc(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    melodeus::core::rt::note_violation("operator new(aligned,nothrow)");
    return sys_alloc(size, static_cast<std::size_t>(align));
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return ::operator new(size, align, std::nothrow);
}

void operator delete(void* ptr) noexcept {
    melodeus::core::rt::note_violation("operator delete");
    sys_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}
