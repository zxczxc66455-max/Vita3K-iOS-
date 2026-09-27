// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace util {
// Synchronization outlives guest shader objects. Notifying an atomic embedded
// in a shader after decrementing to zero could race with the shader's free.
inline std::mutex shader_lifetime_mutex;
inline std::condition_variable shader_lifetime_changed;

inline void wait_for_compilation(const std::atomic<uint32_t> &users) {
    std::unique_lock<std::mutex> lock(shader_lifetime_mutex);
    shader_lifetime_changed.wait(lock, [&] { return users.load(std::memory_order_acquire) == 0; });
}

inline void finish_compilation(std::atomic<uint32_t> &users) {
    {
        std::lock_guard<std::mutex> lock(shader_lifetime_mutex);
        users.fetch_sub(1, std::memory_order_release);
    }
    // Never access users after unlocking: the guest may have freed it already.
    shader_lifetime_changed.notify_all();
}
} // namespace util
