// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace util {
// Best-effort disk-cache work. Retain only hashes, never guest shader pointers.
// A saturated queue drops speculative work; normal draw-time loading still works.
class ShaderWarmupQueue {
public:
    using Hash = std::array<uint8_t, 32>;
    static constexpr std::size_t capacity = 32;

    ~ShaderWarmupQueue() { stop(); }

    void start(std::function<void(const Hash &)> process) {
        stop();
        std::lock_guard<std::mutex> lock(mutex);
        accepting = true;
        try {
            worker = std::thread([this, process = std::move(process)] {
                while (true) {
                    Hash hash;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        ready.wait(lock, [&] { return !accepting || !pending.empty(); });
                        if (!accepting)
                            break;
                        hash = pending.front();
                        pending.pop_front();
                    }
                    process(hash); // Owner catches driver/I/O failures.
                    std::lock_guard<std::mutex> lock(mutex);
                    outstanding.erase(hash);
                }
            });
        } catch (...) {
            accepting = false;
            throw;
        }
    }

    bool try_enqueue(const Hash &hash) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!accepting || outstanding.size() >= capacity || outstanding.count(hash))
            return false;
        try {
            outstanding.insert(hash);
            pending.push_back(hash);
        } catch (...) {
            outstanding.erase(hash);
            return false;
        }
        ready.notify_one();
        return true;
    }

    void request_stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            pending.clear();
        }
        ready.notify_all();
    }

    void stop() {
        request_stop();
        if (worker.joinable())
            worker.join();
        std::lock_guard<std::mutex> lock(mutex);
        outstanding.clear();
    }

private:
    std::mutex mutex;
    std::condition_variable ready;
    bool accepting = false;
    std::deque<Hash> pending;
    std::set<Hash> outstanding;
    std::thread worker;
};
} // namespace util
