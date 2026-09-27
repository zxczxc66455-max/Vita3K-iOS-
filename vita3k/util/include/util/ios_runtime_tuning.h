// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <cstdint>
#include <string_view>

namespace ios_runtime {
// Loaded once by the iOS frontend before renderer/JIT initialization. Settings
// edits affect the next process: existing JIT pool regions must keep their size.
enum class CPUBackend { Jit = 0,
    IRInterpreter = 1 };

struct Tuning {
    CPUBackend cpu_backend = CPUBackend::Jit;
    int guest_memory_mib = 768;
    int jit_cache_mib = 0;
    int cpu_execution_threads = 0;
    int shader_workers = 0;
    int texture_entries = 0;
    bool trim_staging_buffers = true;
    bool precompile_shaders = false;
    bool conservative_culling = false;
    bool metal_hud_requested = false;
    bool metal_argument_buffers = true;
    // Zero disables idle cache retirement. Read once at startup.
    int idle_cache_seconds = 45;
    bool a11_device = false;
    bool prefer_hle_avplayer = false;
    bool prewarm_shader_cache = true;
};
inline Tuning tuning;

constexpr bool is_a11(std::string_view machine) {
    return machine == "iPhone10,1" || machine == "iPhone10,2"
        || machine == "iPhone10,3" || machine == "iPhone10,4"
        || machine == "iPhone10,5" || machine == "iPhone10,6";
}
// 0 = automatic, 1 = legacy bindings, 2 = argument buffers.
constexpr bool metal_argument_buffers(int requested, bool a11) {
    return requested == 1 ? false : requested == 2 ? true
                                                   : !a11;
}
constexpr int idle_cache_seconds(int requested) {
    return requested == 0 || requested == 30 || requested == 45 || requested == 60 ? requested : 45;
}
constexpr bool cache_expired(uint64_t now, uint64_t last_used, int seconds) {
    return seconds > 0 && now >= last_used && now - last_used >= static_cast<uint64_t>(seconds);
}
// One entry may already be executing its display callback outside the queue.
constexpr uint32_t display_queue_capacity(uint32_t requested, bool ios) {
    const uint32_t total = requested < 1 ? 1 : requested;
    const uint32_t cap = ios ? 2 : 3;
    const uint32_t bounded = total < cap ? total : cap;
    return bounded > 1 ? bounded - 1 : 1;
}
constexpr uint32_t swapchain_images(uint32_t minimum, uint32_t maximum) {
    const uint32_t preferred = minimum > 2 ? minimum : 2;
    return maximum != 0 && preferred > maximum ? maximum : preferred;
}

constexpr CPUBackend cpu_backend(int requested) {
    return requested == 1 ? CPUBackend::IRInterpreter : CPUBackend::Jit;
}
inline bool uses_jit() { return tuning.cpu_backend == CPUBackend::Jit; }
constexpr int guest_memory_mib(int requested) {
    return requested == 512 || requested == 768 || requested == 1024 ? requested : 768;
}
constexpr int jit_cache_mib(int requested) {
    return requested == 4 || requested == 8 || requested == 12 || requested == 16 || requested == 24 || requested == 32 ? requested : 0;
}
// Zero preserves unrestricted host scheduling. Explicit limits cap concurrent
// guest JIT runs, not physical cores or the number of guest threads created.
constexpr int cpu_execution_threads(int requested, int logical_cores) {
    const int available = logical_cores > 0 ? logical_cores : 1;
    return requested >= 1 && requested <= 8 ? (requested < available ? requested : available) : 0;
}
constexpr int shader_workers(int requested, int logical_cores) {
    const int available = logical_cores > 0 ? logical_cores : 1;
    return requested >= 1 && requested <= 4 ? (requested < available ? requested : available) : 0;
}
constexpr int texture_entries(int requested, int memory_mib) {
    if (requested == 128 || requested == 256 || requested == 512)
        return requested;
    return memory_mib > 0 && memory_mib <= 3072 ? 128 : 512;
}
// Hysteresis prevents frequent allocation/free when texture sizes fluctuate.
constexpr bool shrink_staging(unsigned long long capacity, unsigned long long required,
    unsigned long long frame, unsigned long long last_resize) {
    return capacity > 4ULL * 1024 * 1024 && required <= capacity / 4
        && frame >= last_resize && frame - last_resize >= 120;
}
} // namespace ios_runtime
