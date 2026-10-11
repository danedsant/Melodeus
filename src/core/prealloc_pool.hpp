#pragma once

#include "core/hardware.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace melodeus::core {

/// Pool de objetos/buffers de capacidad fija, pre-asignado al cargar la pista.
///
/// Esta es la única fuente de memoria del camino de audio: la regla dura del
/// proyecto prohíbe `new`/`malloc` dentro del callback, así que decks, buffers
/// de efectos y rings se obtienen de aquí ANTES de empezar a reproducir y se
/// devuelven al terminar.
///
/// La lista libre es una pila de Treiber con cabecera etiquetada (índice +
/// tag de 32 bits): operación lock-free y a prueba de ABA. Si el pool se
/// agota, `create()` devuelve nullptr en vez de bloquear o ir a heap.
///
/// T puede tener constructor/destructor (se usan construct_at/destroy_at vía
/// placement new / llamada explícita), pero el tamaño es fijo en compilación.
template <typename T, std::size_t Capacity> class PreallocPool {
    static_assert(Capacity > 0);
    static_assert(Capacity < 0xFFFFFFFFull, "Capacity debe caber en uint32");
    static_assert(sizeof(T) % alignof(T) == 0, "invariante de stride cumplido siempre");

public:
    PreallocPool() noexcept {
        for (std::uint32_t i = 0; i < Capacity; ++i) {
            nodes_[i].next = (i + 1 < Capacity) ? (i + 1) : kNil;
        }
        head_.store(pack(0, 0), std::memory_order_relaxed);
        free_count_.store(static_cast<std::uint32_t>(Capacity), std::memory_order_relaxed);
    }

    PreallocPool(const PreallocPool&) = delete;
    PreallocPool& operator=(const PreallocPool&) = delete;
    PreallocPool(PreallocPool&&) = delete;
    PreallocPool& operator=(PreallocPool&&) = delete;

    ~PreallocPool() = default;

    /// Reserva storage sin construir (buffers de audio crudos).
    /// RT-safe: lock-free, sin asignaciones. nullptr si está agotado.
    [[nodiscard]] T* alloc_raw() noexcept {
        std::uint64_t cur = head_.load(std::memory_order_acquire);
        for (;;) {
            const std::uint32_t idx = unpack_index(cur);
            if (idx == kNil) {
                return nullptr;
            }
            const std::uint32_t tag = unpack_tag(cur);
            const std::uint32_t next = nodes_[idx].next;
            const std::uint64_t desired = pack(next, tag + 1);
            if (head_.compare_exchange_weak(cur, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                free_count_.fetch_sub(1, std::memory_order_relaxed);
                return slot(idx);
            }
        }
    }

    /// Reserva y construye un T. nullptr si el pool está agotado.
    template <typename... Args> [[nodiscard]] T* create(Args&&... args) {
        T* raw = alloc_raw();
        if (raw == nullptr) {
            return nullptr;
        }
        return ::new (raw) T(std::forward<Args>(args)...);
    }

    /// Destruye el objeto y devuelve el slot al pool.
    void destroy(T* obj) noexcept {
        if (obj == nullptr) {
            return;
        }
        obj->~T();
        free_raw(obj);
    }

    /// Devuelve al pool un slot obtenido con alloc_raw() (sin destruir nada).
    /// `obj` debe pertenecer a este pool; cualquier otro puntero es UB.
    void free_raw(T* obj) noexcept {
        const auto* base = reinterpret_cast<const std::byte*>(storage_);
        const auto* p = reinterpret_cast<const std::byte*>(obj);
        const auto idx = static_cast<std::uint32_t>((p - base) / sizeof(T));
        for (;;) {
            std::uint64_t cur = head_.load(std::memory_order_acquire);
            const std::uint32_t tag = unpack_tag(cur);
            nodes_[idx].next = unpack_index(cur);
            const std::uint64_t desired = pack(idx, tag + 1);
            if (head_.compare_exchange_weak(cur, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                free_count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    /// Índice [0, Capacity) del slot que contiene `obj` (pertenece al pool).
    [[nodiscard]] std::size_t index_of(const T* obj) const noexcept {
        const auto* base = reinterpret_cast<const std::byte*>(storage_);
        const auto* p = reinterpret_cast<const std::byte*>(obj);
        return static_cast<std::size_t>((p - base) / sizeof(T));
    }

    /// Slots libres (aproximado si hay operaciones concurrentes).
    [[nodiscard]] std::size_t available() const noexcept {
        return free_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool exhausted() const noexcept {
        return available() == 0;
    }

private:
    struct Node {
        std::uint32_t next;
    };

    static constexpr std::uint32_t kNil = 0xFFFFFFFFu;

    static constexpr std::uint64_t pack(std::uint32_t index, std::uint32_t tag) noexcept {
        return (static_cast<std::uint64_t>(tag) << 32) | index;
    }
    static constexpr std::uint32_t unpack_index(std::uint64_t v) noexcept {
        return static_cast<std::uint32_t>(v & 0xFFFFFFFFull);
    }
    static constexpr std::uint32_t unpack_tag(std::uint64_t v) noexcept {
        return static_cast<std::uint32_t>(v >> 32);
    }

    T* slot(std::uint32_t idx) noexcept {
        return reinterpret_cast<T*>(storage_ + static_cast<std::size_t>(idx) * sizeof(T));
    }

    // Storage alineado a T; stride = sizeof(T) (múltiplo de alignof(T)).
    alignas(T) std::byte storage_[Capacity * sizeof(T)];
    Node nodes_[Capacity];
    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};
    alignas(kCacheLine) std::atomic<std::uint32_t> free_count_{0};
};

} // namespace melodeus::core
