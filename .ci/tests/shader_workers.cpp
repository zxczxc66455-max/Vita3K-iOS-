// Synthetic adapters around the actual PipelineCache acquisition functions.
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
using Sha256Hash = std::array<uint8_t, 32>;
namespace vk {
struct ShaderModule {
    uint64_t value = 0;
    ShaderModule() = default;
    ShaderModule(std::nullptr_t) {}
    explicit ShaderModule(uint64_t value)
        : value(value) {}
    explicit operator bool() const { return value != 0; }
    bool operator==(const ShaderModule &) const = default;
};
struct SpecializationInfo {};
enum class ShaderStageFlagBits { eVertex,
    eFragment };
struct PipelineShaderStageCreateInfo {
    ShaderStageFlagBits stage;
    ShaderModule module;
    const char *pName;
    const SpecializationInfo *pSpecializationInfo;
};
struct ShaderModuleCreateInfo {
    size_t codeSize;
    const uint32_t *pCode;
};
} // namespace vk
namespace shader {
struct Hints {};
constexpr int CURRENT_VERSION = 1;
namespace usse {
using SpirvCode = std::vector<uint32_t>;
}
} // namespace shader
namespace fmt {
template <typename... T>
std::string format(const char *, T...) { return "fixture"; }
} // namespace fmt
#define LOG_INFO(...) ((void)0)
#define LOG_WARN_ONCE(...) ((void)0)
std::string hex_string(const Sha256Hash &) { return "fixture"; }
const vk::SpecializationInfo srgb_info_true{}, srgb_info_false{};
struct MemState {};
struct SceGxmProgram {
    bool is_frag_color_used() const { return true; }
};
struct Features {
    bool should_use_shader_interlock() const { return true; }
};
template <typename... T>
shader::usse::SpirvCode load_spirv_shader(T &&...) { return { 1 }; }

// Count entry to a real condition-variable wait to make contention deterministic.
struct ObservedCondition {
    std::condition_variable cv;
    std::atomic<int> waiting{ 0 };
    template <typename Predicate>
    void wait(std::unique_lock<std::mutex> &lock, Predicate predicate) {
        if (!predicate()) {
            ++waiting;
            cv.wait(lock, predicate);
            --waiting;
        }
    }
    void notify_all() { cv.notify_all(); }
};
struct Device {
    std::atomic<int> creates{ 0 }, destroys{ 0 };
    vk::ShaderModule createShaderModule(vk::ShaderModuleCreateInfo) {
        return vk::ShaderModule(100 + ++creates);
    }
    void destroy(vk::ShaderModule module) {
        if (module)
            ++destroys;
    }
};
struct State {
    Features features;
    Device device;
    std::atomic<bool> use_disk_shader_cache{ true };
    std::string shaders_path, shaders_log_path;
    std::vector<std::array<Sha256Hash, 2>> shaders_cache_hashs;
    int get_features_mask() const { return 0; }
};
class PipelineCache {
public:
    State state;
    std::mutex shaders_mutex;
    ObservedCondition shaders_ready;
    std::map<Sha256Hash, vk::ShaderModule> shaders;
    std::atomic<int> loads{ 0 };
    bool disk_hit = false, fail_first = false;
    std::promise<void> entered, release;
    std::shared_future<void> released = release.get_future().share();
    vk::ShaderModule load_shader_from_disk(const Sha256Hash &hash) {
        int attempt = ++loads;
        if (hash[0] == 1 && attempt == 1) {
            entered.set_value();
            released.wait();
            if (fail_first)
                throw std::runtime_error("synthetic compile failure");
        }
        return disk_hit ? vk::ShaderModule(42) : vk::ShaderModule(nullptr);
    }
    vk::PipelineShaderStageCreateInfo retrieve_shader(const SceGxmProgram *, const Sha256Hash &, bool, bool, MemState &, const shader::Hints &, bool = false);
    vk::ShaderModule precompile_shader(const Sha256Hash &);
};
// PRODUCTION_FUNCTIONS

void await_waiters(PipelineCache &cache, int count) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (cache.shaders_ready.waiting != count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(cache.shaders_ready.waiting == count);
}

int main() {
    SceGxmProgram program;
    MemState mem;
    shader::Hints hints;
    const Sha256Hash first{ 1 }, second{ 2 };
    for (bool disk_hit : { false, true }) {
        PipelineCache cache;
        cache.disk_hit = disk_hit;
        auto request = [&](const Sha256Hash &hash) {
            return cache.retrieve_shader(&program, hash, true, false, mem, hints).module;
        };
        auto owner = std::async(std::launch::async, [&] { return request(first); });
        assert(cache.entered.get_future().wait_for(3s) == std::future_status::ready);
        std::vector<std::future<vk::ShaderModule>> waiters;
        for (int i = 0; i < 4; ++i)
            waiters.push_back(std::async(std::launch::async, [&] { return request(first); }));
        // Startup and runtime callers must both wait for the completed handle.
        waiters.push_back(std::async(std::launch::async, [&] { return cache.precompile_shader(first); }));
        await_waiters(cache, 5);
        cache.shaders_ready.notify_all(); // Spurious wakeup must not return a sentinel.
        for (auto &waiter : waiters)
            assert(waiter.wait_for(5ms) == std::future_status::timeout);
        // A different hash must complete even with the first shader blocked.
        auto independent = std::async(std::launch::async, [&] { return request(second); });
        assert(independent.wait_for(3s) == std::future_status::ready);
        assert(independent.get());
        cache.release.set_value();
        const auto expected = owner.get();
        for (auto &waiter : waiters)
            assert(waiter.get() == expected);
        assert(request(first) == expected);
        assert(cache.precompile_shader(first) == expected);
        assert(cache.loads == 2);
        assert(cache.state.device.creates == (disk_hit ? 0 : 2));
        assert(cache.state.shaders_cache_hashs.size() == (disk_hit ? 0u : 2u));
        const auto fragment = cache.retrieve_shader(&program, first, false, false, mem, hints, true);
        assert(fragment.module == expected && fragment.stage == vk::ShaderStageFlagBits::eFragment);
        assert(fragment.pSpecializationInfo == &srgb_info_true);
    }
    // A failed owner clears its claim and wakes a waiting worker, which retries.
    {
        PipelineCache cache;
        cache.fail_first = true;
        auto request = [&] { return cache.retrieve_shader(&program, first, true, false, mem, hints).module; };
        auto owner = std::async(std::launch::async, request);
        assert(cache.entered.get_future().wait_for(3s) == std::future_status::ready);
        auto waiter = std::async(std::launch::async, request);
        await_waiters(cache, 1);
        cache.release.set_value();
        bool failed = false;
        try {
            owner.get();
        } catch (const std::runtime_error &) {
            failed = true;
        }
        assert(failed);
        assert(waiter.wait_for(3s) == std::future_status::ready);
        assert(waiter.get());
        assert(cache.loads == 2 && cache.state.device.creates == 1);
    }
    // Background warmup must not serialize an unrelated runtime shader's I/O.
    {
        PipelineCache cache;
        cache.disk_hit = true;
        auto warmup = std::async(std::launch::async, [&] { return cache.precompile_shader(first); });
        assert(cache.entered.get_future().wait_for(3s) == std::future_status::ready);
        auto draw = std::async(std::launch::async, [&] {
            return cache.retrieve_shader(&program, second, true, false, mem, hints).module;
        });
        assert(draw.wait_for(3s) == std::future_status::ready && draw.get());
        cache.release.set_value();
        assert(warmup.get());
    }
    // A failed speculative load releases the claim and lets a draw retry.
    {
        PipelineCache cache;
        cache.fail_first = true;
        auto warmup = std::async(std::launch::async, [&] { return cache.precompile_shader(first); });
        assert(cache.entered.get_future().wait_for(3s) == std::future_status::ready);
        auto draw = std::async(std::launch::async, [&] {
            return cache.retrieve_shader(&program, first, true, false, mem, hints).module;
        });
        await_waiters(cache, 1);
        cache.release.set_value();
        try { warmup.get(); assert(false); } catch (const std::runtime_error &) {}
        assert(draw.wait_for(3s) == std::future_status::ready && draw.get());
    }
    // A startup disk miss must leave the entry available for runtime generation.
    {
        PipelineCache cache;
        assert(!cache.precompile_shader(second));
        assert(cache.retrieve_shader(&program, second, true, false, mem, hints).module);
        assert(cache.loads == 2 && cache.state.device.creates == 1);
    }
}
