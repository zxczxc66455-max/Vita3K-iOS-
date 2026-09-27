#include <util/ios_runtime_tuning.h>
#include <threads/queue.h>
#include <cassert>
#include <future>
#include <limits>
#include <vector>
#include <array>
#include <thread>
using namespace std::chrono_literals;
#define LOG_ERROR(...) ((void)0)
constexpr bool VK_TRUE = true;
// INSERT_FRAME_COUNT
static std::vector<int> events;
namespace vk {
enum class Result { eSuccess };
enum class CommandPoolResetFlagBits { eReleaseResources = 1 };
struct CommandPoolResetFlags {
    int value = 0;
    CommandPoolResetFlags &operator=(CommandPoolResetFlagBits bits) { value = static_cast<int>(bits); return *this; }
};
struct Device {
    Result waitForFences(const std::vector<int> &, bool, uint64_t) { events.push_back(1); return Result::eSuccess; }
    void resetFences(const std::vector<int> &) { events.push_back(2); }
    void resetCommandPool(int, CommandPoolResetFlags flags) { events.push_back(3 + flags.value); }
};
}
struct Descriptor { int descriptors_idx = 9; };
#ifdef VITA3K_PLATFORM_IOS
static int retired_descriptors = 0;
static void retire_frame_descriptors(vk::Device, Descriptor &descriptor, uint64_t, int) {
    // The fence and both command-pool resets must precede retirement.
    assert(events.size() == 4 && (events.back() == 3 || events.back() == 4));
    ++retired_descriptors;
    descriptor.descriptors_idx = 0;
}
#endif
struct FrameObject {
    std::vector<int> rendered_fences;
    int prerender_pool = 0, render_pool = 1;
    std::array<Descriptor,16> vert_descriptors, frag_descriptors;
    Descriptor color_descriptor;
    struct { void destroy_objects() { events.push_back(5); } } destroy_queue;
    uint64_t frame_timestamp = 0, cache_clock_seconds = 0;
};
struct FrameDoneRequest { uint64_t timestamp; };
struct State {
    struct { bool enable_memory_mapping = false; } features;
    struct { void push(FrameDoneRequest) { events.push_back(0); } } request_queue;
    struct { void clear_surfaces_changed() {} } surface_cache;
    struct {
        void retire_idle(uint64_t) {
            assert(events.back() == 5); // Newly queued images cannot be drained in this visit.
        }
    } texture_cache;
    int current_frame_idx = 0;
    vk::Device device;
    std::array<FrameObject, MAX_FRAMES_RENDERING> frames;
    FrameObject &frame() { return frames[current_frame_idx]; }
};
struct VKContext {
    State state;
    uint64_t frame_timestamp = 0, last_frame_waited = 0;
    std::mutex new_frame_mutex;
    std::condition_variable new_frame_condv;
    unsigned last_vert_texture_count = 0, last_frag_texture_count = 0;
};
// INSERT_NEW_FRAME
int main() {
    // Every slot releases periodically; always after completion and fence reset.
    for (bool enabled : {false, true}) {
        ios_runtime::tuning.trim_staging_buffers = enabled;
        VKContext context;
        for (unsigned frame = 1; frame <= 725; ++frame) {
            context.state.frames[frame % MAX_FRAMES_RENDERING].rendered_fences = {7};
            events.clear();
            new_frame(context);
            int reset = 3;
#ifdef VITA3K_PLATFORM_IOS
            if (enabled && (frame / MAX_FRAMES_RENDERING) % 120 == 0) reset = 4;
#endif
            assert(events == std::vector<int>({1, 2, reset, reset, 5}));
            assert(context.state.frame().rendered_fences.empty());
            assert(context.state.frame().color_descriptor.descriptors_idx == 0);
        }
        context.state.features.enable_memory_mapping = true;
        context.last_frame_waited = context.frame_timestamp;
        context.state.frames[726 % MAX_FRAMES_RENDERING].rendered_fences = {9};
        events.clear();
        new_frame(context);
        assert(events == std::vector<int>({0, 2, 3, 3, 5}));
    }
#ifdef VITA3K_PLATFORM_IOS
    assert(retired_descriptors == 2 * 726 * 33);
#endif
    Queue<int> queue;
    queue.maxPendingCount_ = 8;
    for (int i = 0; i < 8; ++i) queue.push(i);
    assert(queue.size() == 8);
    auto producer = std::async(std::launch::async, [&] { queue.push(8); });
    assert(producer.wait_for(30ms) == std::future_status::timeout);
    assert(*queue.pop() == 0);
    assert(producer.wait_for(2s) == std::future_status::ready);
    for (int i = 1; i <= 8; ++i) assert(*queue.pop() == i);
    assert(queue.size() == 0);
    // Shutdown must release both blocked producers and empty-queue consumers.
    for (int run = 0; run < 200; ++run) {
        queue.reset();
        auto consumer = std::async(std::launch::async, [&] { assert(!queue.pop()); });
        queue.abort();
        assert(consumer.wait_for(2s) == std::future_status::ready);
        queue.reset();
        for (int i = 0; i < 8; ++i) queue.push(i);
        auto blocked = std::async(std::launch::async, [&] { queue.push(9); });
        queue.abort();
        assert(blocked.wait_for(2s) == std::future_status::ready);
        assert(queue.size() == 8);
    }
    queue.reset();
    assert(queue.size() == 0 && !queue.is_aborted());
}
