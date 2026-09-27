// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#ifdef VITA3K_PLATFORM_IOS
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace ios_runtime {
enum class ThreadRole { Guest,
    Render,
    ShaderCompiler };

inline void configure_thread(ThreadRole role) {
#ifdef VITA3K_PLATFORM_IOS
    // QoS expresses latency needs. iOS retains control over physical cores;
    // audio callback priority remains owned by the audio backend.
    const qos_class_t qos = role == ThreadRole::ShaderCompiler ? QOS_CLASS_UTILITY : QOS_CLASS_USER_INITIATED;
    (void)pthread_set_qos_class_self_np(qos, 0);
#else
    (void)role;
#endif
}
} // namespace ios_runtime
