#include <util/presentation_limiter.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <thread>
using namespace std::chrono_literals;
using Address = unsigned;
#define LOG_TRACE(...) ((void)0)
struct DisplayFrameInfo { int base = 0; };
struct PredictedDisplayFrame { DisplayFrameInfo frame_info; Address sync_object; };
struct Display {
    std::mutex display_info_mutex;
    Address current_sync_object = 1;
    bool predicting = true;
    DisplayFrameInfo next_rendered_frame;
    std::vector<PredictedDisplayFrame> predicted_frames;
    unsigned predicted_cycles_seen = 3;
    bool allow = true;
    int gate_calls = 0;
    std::atomic<int> fps_limit{60};
    bool presentation_due_now() { ++gate_calls; return allow; }
};
struct Renderer {
// RENDER_SIGNAL
};
struct EmuEnvState {
    Display display;
    struct { int current_config = 0; } cfg;
    std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>();
};
// PREDICTION
static int global_limit = 60;
static int vita3k_ios_load_fps_limit() { return global_limit; }
static void check_launch_and_restore() {
    auto emuenv = std::make_unique<EmuEnvState>();
    struct Settings { int fps_limit; };
    std::optional<Settings> session_settings;
    const auto saved_current_config = 7;
    // RESTORE
    for (int global : {0, 30, 60}) {
        global_limit = global;
        for (int override : {-1, 0, 30, 60}) {
            session_settings = override < 0 ? std::nullopt : std::make_optional(Settings{override});
            emuenv->display.fps_limit = 60; // Simulate runtime initialization/reset.
            // LAUNCH
            assert(emuenv->display.fps_limit == (override < 0 ? global : override));
            restore_global_config();
            assert(emuenv->display.fps_limit == global);
            if (session_settings) assert(emuenv->cfg.current_config == saved_current_config);
        }
    }
}
namespace vk {
enum class Result { eSuccess, eSuboptimalKHR, eErrorOutOfDateKHR, eErrorSurfaceLostKHR, eErrorDeviceLost };
}
static void check_present_counter() {
    struct {
        struct {
            vk::Result result;
            vk::Result presentKHR(int *) { return result; }
        } general_queue;
        std::atomic<uint64_t> swapchain_presentations{0};
    } state{};
    int present_info = 0;
    for (auto status : {vk::Result::eSuccess, vk::Result::eSuboptimalKHR,
             vk::Result::eErrorOutOfDateKHR, vk::Result::eErrorSurfaceLostKHR, vk::Result::eErrorDeviceLost}) {
        state.general_queue.result = status;
        // PRESENT_COUNTER
    }
    assert(state.swapchain_presentations == 2);
}
int main() {
    using Clock = util::PresentationLimiter::Clock;
    const auto start = Clock::time_point{};
    for (int limit : {30, 60}) {
        util::PresentationLimiter limiter;
        int presented = 0;
        for (int us = 0; us < 1000000; us += 1000)
            presented += limiter.due(limit, start + std::chrono::microseconds(us));
        assert(presented == limit);
        assert(limiter.due(limit, start + 10s));
        assert(!limiter.due(limit, start + 10s + 1us)); // No catch-up burst.
        assert(limiter.due(0, start + 10s + 2us));
        assert(limiter.due(0, start + 10s + 2us));
        assert(limiter.due(limit, start + 10s + 3us));
        assert(!limiter.due(limit, start + 10s + 4us));
        assert(limiter.due(limit == 30 ? 60 : 30, start + 10s + 5us));
    }
    for (int bad : {-1, 1, 15, 120, 2147483647}) assert(util::normalize_fps_limit(bad) == 60);
    EmuEnvState env;
    DisplayFrameInfo frame{42};
    env.display.predicted_frames.push_back({frame, 1});
    update_prediction(env, frame);
    assert(env.display.gate_calls == 0 && !env.renderer->should_display);
    // A correct prediction must not consume the next presentation opportunity.
    frame.base = 43;
    env.display.allow = false;
    update_prediction(env, frame);
    assert(env.display.gate_calls == 1 && !env.renderer->should_display);
    assert(env.display.next_rendered_frame.base == 43); // Latest correction survives a capped frame.
    assert(env.display.predicted_cycles_seen == 1);
    env.display.predicting = false;
    env.display.allow = true;
    update_prediction(env, frame);
    assert(env.display.gate_calls == 2 && env.renderer->should_display);
    // Reported 20 FPS workload: none of the supported caps may throttle it.
    for (int limit : {0, 30, 60}) {
        util::PresentationLimiter limiter;
        for (int frame = 0; frame < 20; ++frame)
            assert(limiter.due(limit, start + std::chrono::milliseconds(50 * frame)));
    }
    Renderer shared;
    assert(!shared.should_display.load());
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) {
            while (shared.should_display.load()) std::this_thread::yield();
            shared.should_display.store(true);
        }
    });
    for (int i = 0; i < 1000; ++i)
        while (!shared.should_display.exchange(false)) std::this_thread::yield();
    producer.join();
    check_present_counter();
    check_launch_and_restore();
}
