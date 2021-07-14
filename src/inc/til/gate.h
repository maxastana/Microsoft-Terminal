// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "atomic.h"

namespace til
{
    template<std::memory_order AcquireOrder = std::memory_order_acquire, std::memory_order ReleaseOrder = std::memory_order_release>
    struct gate
    {
        constexpr explicit gate(const bool desired) noexcept :
            _state(desired)
        {
        }

        gate(const gate&) = delete;
        gate& operator=(const gate&) = delete;

        // acquire/release provide classical semaphore semantics.
        void acquire() noexcept
        {
            while (!_state.exchange(false, AcquireOrder))
            {
                til::atomic_wait(_state, false);
            }
        }

        // acquire/release provide classical semaphore semantics.
        void release() noexcept
        {
            _state.store(true, ReleaseOrder);
            til::atomic_notify_all(_state);
        }

        // open/close respectively signal that this "real world gate" is open or closed.
        // They deviate from the classical semaphore, by not being blocking operations.
        inline void open() noexcept
        {
            release();
        }

        // open/close respectively signal that this "real world gate" is open or closed.
        // They deviate from the classical semaphore, by not being blocking operations.
        void close() noexcept
        {
            _state.store(false, ReleaseOrder);
        }

        // Block until this "real world gate" can be passed, by being open.
        // This works just like acquiring a semaphore, but doesn't decrease the count.
        void pass() noexcept
        {
            while (!_state.load(AcquireOrder))
            {
                til::atomic_wait(_state, false);
            }
        }

    private:
        std::atomic<bool> _state;
    };

    using gate_relaxed = gate<std::memory_order_relaxed, std::memory_order_relaxed>;
}
