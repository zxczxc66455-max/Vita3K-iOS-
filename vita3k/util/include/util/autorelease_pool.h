// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

namespace util {
// A pool belongs to the calling thread and must drain on that same thread.
// This only releases temporary Objective-C ownership, never in-flight Vulkan
// resources. Those remain owned until the renderer's fence-based retirement.
class AutoreleasePool {
public:
#ifdef VITA3K_PLATFORM_IOS
    AutoreleasePool();
    ~AutoreleasePool();
#else
    AutoreleasePool() = default;
    ~AutoreleasePool() = default;
#endif
    AutoreleasePool(const AutoreleasePool &) = delete;
    AutoreleasePool &operator=(const AutoreleasePool &) = delete;
    AutoreleasePool(AutoreleasePool &&) = delete;
    AutoreleasePool &operator=(AutoreleasePool &&) = delete;

private:
#ifdef VITA3K_PLATFORM_IOS
    void *pool;
#endif
};
} // namespace util
