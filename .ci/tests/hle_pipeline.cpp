#define VITA3K_PLATFORM_IOS
#include <util/shader_lifetime.h>
#include <util/autorelease_pool.h>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
// Track production worker pool scopes without an Objective-C runtime on Linux.
static thread_local int pool_depth = 0;
static std::atomic<int> pools_drained{0};
util::AutoreleasePool::AutoreleasePool() { ++pool_depth; }
util::AutoreleasePool::~AutoreleasePool() { assert(pool_depth == 1); --pool_depth; ++pools_drained; }
#define LOG_ERROR(...) ((void)0)
namespace ios_runtime {
enum class ThreadRole { ShaderCompiler };
void configure_thread(ThreadRole) {}
}
namespace render_diagnostics {
enum Counter { PipelineQueued };
void add(Counter) {}
}
namespace vk {
struct Pipeline {
    uint64_t value = 0;
    Pipeline() = default;
    Pipeline(std::nullptr_t) {}
    explicit Pipeline(uint64_t value) : value(value) {}
    bool operator==(const Pipeline &) const = default;
};
using RenderPass = int;
}
struct MemState {};
struct SceGxmProgram { bool is_frag_color_used() const { return false; } };
struct VKFragmentProgram { uint64_t blending_hash = 0; };
struct SceGxmFragmentProgram {
    std::unique_ptr<VKFragmentProgram> renderer_data = std::make_unique<VKFragmentProgram>();
    struct {
        SceGxmProgram value;
        const SceGxmProgram *get(MemState &) { return &value; }
    } program;
    std::atomic<uint32_t> compile_threads_on{0};
};
struct SceGxmVertexProgram {
    uint64_t key_hash = 0;
    std::vector<int> attributes;
    std::atomic<uint32_t> compile_threads_on{0};
};
template <typename T> struct Ptr {
    T *value;
    T *get(MemState &) const { return value; }
};
struct GxmRecordState {
    uint64_t key = 0;
    Ptr<SceGxmVertexProgram> vertex_program;
    Ptr<SceGxmFragmentProgram> fragment_program;
    struct { int colorFormat = 0; } color_surface;
};
constexpr auto record_pipeline_len = sizeof(GxmRecordState);
uint64_t XXH3_64bits(const void *record, size_t) { return static_cast<const GxmRecordState *>(record)->key; }
enum SceGxmPrimitiveType : uint32_t { Triangles = 0 };
namespace shader {
struct Hints { int color_format = 0; const std::vector<int> *attributes = nullptr; };
}
struct VKContext {
    GxmRecordState record;
    int current_shader_interlock_pass = 0, current_render_pass = 0;
    shader::Hints shader_hints;
};
// INSERT_REQUEST
namespace moodycamel {
struct ConsumerToken { template <typename T> explicit ConsumerToken(T &) {} };
}
struct CompileQueue {
    bool fail_enqueue = false;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<CompileRequest *> jobs;
    bool enqueue(int, CompileRequest *request) {
        if (fail_enqueue) return false;
        { std::lock_guard<std::mutex> lock(mutex); jobs.push_back(request); }
        ready.notify_one();
        return true;
    }
    void wait_dequeue(moodycamel::ConsumerToken &, CompileRequest *&request) {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return !jobs.empty(); });
        request = jobs.front();
        jobs.pop_front();
    }
};
struct PipelineCache {
    struct {
        struct { bool support_shader_interlock = false; } features;
        std::atomic<int> shaders_count_compiled{0};
    } state;
    CompileQueue pipeline_compile_queue;
    int pipeline_compile_queue_token = 0;
    std::map<uint64_t, std::atomic<vk::Pipeline>> pipelines;
    std::atomic<uint32_t> pending_pipeline_compiles{0};
    std::atomic<uint64_t> next_pipeline_cache_save{0};
    static constexpr int pipeline_cache_save_delay = 15;
    bool use_async_compilation = true;
    bool fail_compile = false;
    std::atomic<int> compiles{0};
    void compiler_thread(MemState &);
    vk::Pipeline retrieve_pipeline(VKContext &, SceGxmPrimitiveType &, bool, MemState &);
    vk::Pipeline compile_pipeline(SceGxmPrimitiveType, vk::RenderPass, const SceGxmVertexProgram &vertex,
        const SceGxmFragmentProgram &fragment, const GxmRecordState &record, const shader::Hints &, MemState &) {
        ++compiles;
        if (pipeline_compile_queue.fail_enqueue) {
            assert(vertex.compile_threads_on > 0 && fragment.compile_threads_on > 0);
        }
        if (fail_compile) throw std::runtime_error("driver failure");
        return vk::Pipeline(record.key + 1000);
    }
};
// INSERT_PIPELINE
int main() {
    MemState mem;
    SceGxmPrimitiveType type = Triangles;
    SceGxmVertexProgram vertex;
    SceGxmFragmentProgram fragment;
    VKContext context{};
    context.record.vertex_program.value = &vertex;
    context.record.fragment_program.value = &fragment;
    PipelineCache cache;
    for (uint64_t i = 1; i <= 32; ++i) {
        context.record.key = i;
        assert(cache.retrieve_pipeline(context, type, true, mem) == nullptr);
    }
    assert(cache.pending_pipeline_compiles == 32 && vertex.compile_threads_on == 32);
    context.record.key = 33;
    assert(cache.retrieve_pipeline(context, type, true, mem) == vk::Pipeline(1033));
    assert(cache.pending_pipeline_compiles == 32 && cache.compiles == 1);
    std::thread worker([&] { cache.compiler_thread(mem); });
    util::wait_for_compilation(vertex.compile_threads_on);
    util::wait_for_compilation(fragment.compile_threads_on);
    cache.pipeline_compile_queue.enqueue(0, nullptr);
    worker.join();
    assert(pools_drained == 32);
    assert(cache.pending_pipeline_compiles == 0 && cache.compiles == 33);
    for (uint64_t i = 1; i <= 33; ++i) {
        context.record.key = i;
        assert(cache.retrieve_pipeline(context, type, true, mem) == vk::Pipeline(i + 1000));
    }
    assert(cache.compiles == 33);
    // Failed enqueue falls back to synchronous work without leaking guest pins.
    context.record.key = 34;
    cache.pipeline_compile_queue.fail_enqueue = true;
    assert(cache.retrieve_pipeline(context, type, true, mem) == vk::Pipeline(1034));
    assert(cache.pending_pipeline_compiles == 0 && vertex.compile_threads_on == 0 && fragment.compile_threads_on == 0);
    cache.pipeline_compile_queue.fail_enqueue = false;
    // A driver exception must clear the compiling sentinel and wake release waiters.
    context.record.key = 35;
    cache.fail_compile = true;
    assert(cache.retrieve_pipeline(context, type, true, mem) == nullptr);
    std::thread failing_worker([&] { cache.compiler_thread(mem); });
    util::wait_for_compilation(vertex.compile_threads_on);
    util::wait_for_compilation(fragment.compile_threads_on);
    cache.pipeline_compile_queue.enqueue(0, nullptr);
    failing_worker.join();
    assert(pools_drained == 33);
    assert(cache.pending_pipeline_compiles == 0 && cache.pipelines.at(35).load() == nullptr);
    cache.fail_compile = false;
    assert(cache.retrieve_pipeline(context, type, true, mem) == vk::Pipeline(1035));
    context.record.key = 36;
    cache.pipeline_compile_queue.fail_enqueue = true;
    cache.fail_compile = true;
    try { cache.retrieve_pipeline(context, type, true, mem); assert(false); }
    catch (const std::runtime_error &) {}
    assert(vertex.compile_threads_on == 0 && fragment.compile_threads_on == 0);
    assert(cache.pending_pipeline_compiles == 0 && cache.pipelines.at(36).load() == nullptr);
}
