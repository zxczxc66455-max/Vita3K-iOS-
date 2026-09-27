#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <threads/queue.h>
struct CommandList { int id = 0; bool display = false; bool blocked = false; };
struct MemState { bool signaled = false; };
struct FeatureState {};
struct Config { struct { bool v_sync = false; } current_config; };
enum class Backend { Vulkan, OpenGL };
namespace renderer {
enum class SyncWaitResult { Ready, TimedOut, Shutdown };
struct State {
    bool should_display = false;
    std::atomic<bool> render_abort{false}, async_flip_requested{false};
    std::atomic<int> batches_processed{0};
    void *context = this;
    Backend current_backend = Backend::Vulkan;
    Queue<CommandList> command_buffer_queue;
    std::vector<int> processed;
};
}
static bool shutdown_wait = false;
static bool is_cmd_ready(MemState &mem, CommandList &list) { return !list.blocked || mem.signaled; }
static renderer::SyncWaitResult wait_cmd(MemState &mem, CommandList &) {
    if (shutdown_wait) return renderer::SyncWaitResult::Shutdown;
    mem.signaled = true;
    return renderer::SyncWaitResult::Ready;
}
static void process_batch(renderer::State &state, const FeatureState &, MemState &, Config &, CommandList &list) {
    state.processed.push_back(list.id);
    state.should_display = list.display;
}
// INSERT_BATCHES
int main() {
    Queue<int> queue;
    using R = Queue<int>::PopResult;
    int value = -1;
    assert(queue.pop_if(value, [](int) { return true; }, std::chrono::microseconds(1)) == R::Empty);
    queue.push(4);
    assert(queue.pop_if(value, [](int) { return false; }, std::chrono::microseconds(1)) == R::Blocked);
    assert(value == 4 && queue.size() == 1);
    queue.abort();
    assert(queue.pop_if(value, [](int) { return true; }, std::chrono::microseconds(1)) == R::Aborted);
    queue.reset();
    queue.maxPendingCount_ = 8;
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) producers.emplace_back([&, p] {
        for (int i = 0; i < 1000; ++i) queue.push(p * 1000 + i);
    });
    int next[4] = {};
    for (int n = 0; n < 4000;) {
        if (queue.pop_if(value, [](int) { return true; }, std::chrono::microseconds(1000)) != R::Ready) continue;
        const int producer = value / 1000;
        assert(value % 1000 == next[producer]++);
        ++n;
    }
    for (auto &thread : producers) thread.join();
    queue.wait_empty();
    for (int count : next) assert(count == 1000);

    renderer::State state;
    FeatureState features;
    MemState mem;
    Config config;
    state.command_buffer_queue.push({1, false, true});
    state.command_buffer_queue.push({2, true, false});
    state.command_buffer_queue.push({3, true, false});
    process_batches(state, features, mem, config, 100);
    assert((state.processed == std::vector<int>{1, 2}));
    assert(state.command_buffer_queue.size() == 1 && state.batches_processed == 2);
    state.should_display = false;
    state.async_flip_requested = true;
    process_batches(state, features, mem, config, 100);
    assert(state.command_buffer_queue.size() == 1);
    state.async_flip_requested = false;
    process_batches(state, features, mem, config, 100);
    assert((state.processed == std::vector<int>{1, 2, 3}));
    state.should_display = false;
    mem.signaled = false;
    shutdown_wait = true;
    state.command_buffer_queue.push({4, false, true});
    process_batches(state, features, mem, config, 100);
    assert(state.command_buffer_queue.size() == 1 && state.batches_processed == 3);
}
