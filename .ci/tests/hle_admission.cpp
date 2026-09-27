#include <util/shader_lifetime.h>
#include <util/shader_warmup_queue.h>
#include <util/ios_runtime_tuning.h>
#include <threads/queue.h>
#include <cassert>
#include <future>
#include <memory>
using namespace std::chrono_literals;
int main() {
    using util::ShaderWarmupQueue;
    ShaderWarmupQueue queue;
    const ShaderWarmupQueue::Hash first{1};
    assert(!queue.try_enqueue(first));
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> jobs{0};
    queue.start([&](const auto &) { ++jobs; entered.set_value(); released.wait(); });
    assert(queue.try_enqueue(first));
    assert(entered.get_future().wait_for(3s) == std::future_status::ready);
    assert(!queue.try_enqueue(first)); // The active job remains part of the bound/dedupe set.
    for (uint8_t i = 2; i <= ShaderWarmupQueue::capacity; ++i)
        assert(queue.try_enqueue(ShaderWarmupQueue::Hash{i}));
    assert(!queue.try_enqueue(ShaderWarmupQueue::Hash{99}));
    queue.request_stop();
    auto stop = std::async(std::launch::async, [&] { queue.stop(); });
    assert(stop.wait_for(20ms) == std::future_status::timeout);
    release.set_value();
    assert(stop.wait_for(3s) == std::future_status::ready);
    stop.get();
    assert(jobs == 1 && !queue.try_enqueue(first)); // Pending speculation is discarded.
    std::promise<void> restarted;
    queue.start([&](const auto &) { restarted.set_value(); });
    assert(queue.try_enqueue(first));
    assert(restarted.get_future().wait_for(3s) == std::future_status::ready);
    queue.stop();

    // Free immediately on wake; the notifier must not access the freed counter.
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto users = std::make_unique<std::atomic<uint32_t>>(2);
        auto *counter = users.get();
        auto guest = std::async(std::launch::async, [&] {
            util::wait_for_compilation(*users);
            users.reset();
        });
        util::finish_compilation(*counter);
        util::shader_lifetime_changed.notify_all();
        assert(guest.wait_for(1ms) == std::future_status::timeout);
        util::finish_compilation(*counter);
        assert(guest.wait_for(3s) == std::future_status::ready);
        guest.get();
    }
    assert(ios_runtime::display_queue_capacity(0, true) == 1);
    assert(ios_runtime::display_queue_capacity(1, true) == 1);
    assert(ios_runtime::display_queue_capacity(9, true) == 1);
    assert(ios_runtime::display_queue_capacity(9, false) == 2);
    Queue<int> display;
    display.maxPendingCount_ = ios_runtime::display_queue_capacity(3, true);
    display.push(1);
    auto producer = std::async(std::launch::async, [&] { display.push(2); });
    assert(producer.wait_for(20ms) == std::future_status::timeout);
    assert(*display.top() == 1); // Waiting on GXM syncs still consumes the slot.
    assert(producer.wait_for(20ms) == std::future_status::timeout);
    assert(*display.pop() == 1);
    assert(producer.wait_for(3s) == std::future_status::ready);
    producer.get();
    assert(*display.pop() == 2);
}
