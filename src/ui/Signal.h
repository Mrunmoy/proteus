#pragma once

// ── Qt-style signals/slots, heap-free ──────────────────────────────────────
// A Signal<Args...> holds up to Capacity slots in a fixed inline array. A slot
// is a plain function pointer plus an opaque context pointer — the same shape
// the rest of the codebase uses for callbacks (no std::function, so nothing
// allocates and the type stays trivially copyable). Connecting past Capacity is
// reported, not silently dropped, so callers can size the template correctly.

#include <cstddef>
#include <cstdint>

namespace ui
{
    template <std::size_t Capacity, typename... Args>
    class Signal
    {
        // Capacity 0 would make connect() pointless and the array ill-formed.
        static_assert(Capacity > 0, "Signal capacity must be at least 1");

    public:
        using SlotFn = void (*)(void*, Args...);

        Signal() = default;

        Signal(const Signal&)            = delete;
        Signal& operator=(const Signal&) = delete;
        Signal(Signal&&)                 = delete;
        Signal& operator=(Signal&&)      = delete;

        // ── Wiring ─────────────────────────────────────────────────────────
        // Returns false when the slot table is full (or fn is null) so a
        // mis-sized Signal fails loudly at the call site instead of dropping
        // edges at runtime.
        [[nodiscard]] bool connect(SlotFn fn, void* ctx)
        {
            if (fn == nullptr || m_count >= Capacity)
            {
                return false;
            }
            m_slots[m_count] = Slot{fn, ctx};
            ++m_count;
            return true;
        }

        void disconnectAll()
        {
            m_count = 0;
        }

        [[nodiscard]] std::size_t slotCount() const
        {
            return m_count;
        }

        [[nodiscard]] static constexpr std::size_t capacity()
        {
            return Capacity;
        }

        // ── Emission ─────────────────────────────────────────────────────────
        // Slots fire in connection order. Args are taken by value/const-ref via
        // the parameter pack; for the small scalar payloads we emit (an int
        // index) this is the cheapest, branch-free path.
        void emit(Args... args) const
        {
            for (std::size_t i = 0; i < m_count; ++i)
            {
                m_slots[i].fn(m_slots[i].ctx, args...);
            }
        }

        void operator()(Args... args) const
        {
            emit(args...);
        }

    private:
        struct Slot
        {
            SlotFn fn  = nullptr;
            void*  ctx = nullptr;
        };

        Slot        m_slots[Capacity] = {};
        std::size_t m_count           = 0;
    };
}
