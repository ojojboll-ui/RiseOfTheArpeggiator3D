#pragma once

#include <array>
#include <atomic>
#include <cstddef>

/**
    One writer, one reader, no locks, no allocation. The game posts commands to
    the audio thread through one of these and the audio thread posts what it
    played back through another. When full, a push is dropped rather than
    waited on — neither thread may ever block on the other.
*/
namespace np::audio
{
    template <typename T, std::size_t Capacity>
    class SpscQueue
    {
        static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    public:
        bool push (const T& item) noexcept
        {
            const auto w = write.load (std::memory_order_relaxed);
            const auto r = read.load (std::memory_order_acquire);
            if (w - r >= Capacity) return false;
            slots[w & (Capacity - 1)] = item;
            write.store (w + 1, std::memory_order_release);
            return true;
        }

        bool pop (T& item) noexcept
        {
            const auto r = read.load (std::memory_order_relaxed);
            const auto w = write.load (std::memory_order_acquire);
            if (r == w) return false;
            item = slots[r & (Capacity - 1)];
            read.store (r + 1, std::memory_order_release);
            return true;
        }

    private:
        std::array<T, Capacity> slots {};
        std::atomic<std::size_t> write { 0 }, read { 0 };
    };
}
