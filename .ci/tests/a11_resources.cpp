#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include <util/ios_runtime_tuning.h>

namespace vk {
using DescriptorSet = int;
using DescriptorSetLayout = int;
using DescriptorPool = int;
enum class DescriptorType { Image };
struct DescriptorPoolSize { DescriptorType type; uint32_t count; };
struct DescriptorPoolCreateInfo {
    uint32_t maxSets = 0;
    void setPoolSizes(DescriptorPoolSize) {}
};
struct DescriptorSetAllocateInfo {
    int descriptorPool = 0;
    size_t count = 0;
    void setSetLayouts(const std::vector<int> &layouts) { count = layouts.size(); }
};
static int next_handle = 0;
static bool fail_allocate = false;
static std::set<int> pools;
struct Device {
    int createDescriptorPool(DescriptorPoolCreateInfo info) {
        assert(info.maxSets > 0);
        pools.insert(++next_handle);
        return next_handle;
    }
    std::vector<int> allocateDescriptorSets(DescriptorSetAllocateInfo allocation) {
        if (fail_allocate) throw std::runtime_error("allocation failed");
        std::vector<int> sets;
        for (size_t i = 0; i < allocation.count; ++i) sets.push_back(++next_handle);
        return sets;
    }
    void destroyDescriptorPool(int pool) { assert(pools.erase(pool) == 1); }
};
}
struct DescriptorPack { int pool; size_t end_index; uint64_t last_used_seconds; };
struct FrameDescriptor {
    std::vector<int> sets;
    int descriptors_idx = 0;
    std::vector<DescriptorPack> packs;
};
struct Image { int image = 0; };
struct DestroyQueue {
    std::set<int> retired;
    void add_image(Image &image) {
        if (image.image) { assert(retired.insert(image.image).second); image.image = 0; }
    }
};
struct Frame { uint64_t cache_clock_seconds = 100; DestroyQueue destroy_queue; };
struct VKState { vk::Device device; Frame slot; Frame &frame() { return slot; } };
// INSERT_DESCRIPTORS
struct TextureCacheInfo { int index; int texture_size = 64; bool is_imported = true; };
struct Entry { Image texture; uint64_t last_used_seconds; };
struct LRU {
    std::set<int> expired;
    void set_as_lru(TextureCacheInfo *info) { expired.insert(info->index); }
};
struct VKTextureCache {
    VKState &state;
    uint64_t last_idle_sweep_seconds = 0;
    std::map<int, TextureCacheInfo *> texture_lookup;
    std::vector<Entry> textures;
    LRU texture_queue;
    void retire_idle(uint64_t now);
};
// INSERT_TEXTURE_RETIREMENT
int main() {
    using namespace ios_runtime;
    assert(is_a11("iPhone10,1") && is_a11("iPhone10,6"));
    assert(!is_a11("iPhone11,1") && !is_a11("iPhone10,7") && !is_a11("arm64"));
    assert(!metal_argument_buffers(0, true) && metal_argument_buffers(0, false));
    assert(metal_argument_buffers(2, true) && !metal_argument_buffers(1, false));
    assert(!metal_argument_buffers(99, true));
    assert(idle_cache_seconds(-1) == 45 && idle_cache_seconds(0) == 0);
    assert(idle_cache_seconds(30) == 30 && idle_cache_seconds(60) == 60);
    assert(!cache_expired(20, 30, 45) && !cache_expired(100, 0, 0));
    assert(!cache_expired(144, 100, 45) && cache_expired(145, 100, 45));
    assert(swapchain_images(1, 0) == 2 && swapchain_images(2, 0) == 2);
    assert(swapchain_images(3, 4) == 3 && swapchain_images(1, 1) == 1);

    VKState state;
    FrameDescriptor first, other_slot;
    std::set<int> handles;
    for (int i = 0; i < 65; ++i)
        assert(handles.insert(retrieve_frame_descriptor(state, first, 1, vk::DescriptorType::Image, 4, 32)).second);
    assert(first.packs.size() == 3 && first.sets.size() == 96);
    retrieve_frame_descriptor(state, other_slot, 1, vk::DescriptorType::Image, 4, 32);
    assert(vk::pools.size() == 4);
    retire_frame_descriptors(state.device, first, 144, 45);
    assert(first.packs.size() == 3 && first.descriptors_idx == 0);
    state.slot.cache_clock_seconds = 144;
    const auto original = first.sets.front();
    assert(retrieve_frame_descriptor(state, first, 1, vk::DescriptorType::Image, 4, 32) == original);
    retire_frame_descriptors(state.device, first, 145, 45);
    assert(first.packs.size() == 1 && first.sets.size() == 32);
    assert(other_slot.sets.size() == 32 && vk::pools.size() == 2);
    retire_frame_descriptors(state.device, first, 1000, 0);
    assert(first.packs.size() == 1); // Off retains cached handles.
    retire_frame_descriptors(state.device, first, UINT64_MAX, 1);
    retire_frame_descriptors(state.device, other_slot, UINT64_MAX, 1);
    assert(vk::pools.empty());
    vk::fail_allocate = true;
    try { retrieve_frame_descriptor(state, first, 1, vk::DescriptorType::Image, 1, 16); assert(false); }
    catch (const std::runtime_error &) {}
    assert(vk::pools.empty() && first.sets.empty() && first.packs.empty());
    vk::fail_allocate = false;

    TextureCacheInfo cold{0}, hot{1};
    VKTextureCache cache{state, 0, {{1, &cold}, {2, &hot}}, {{{10}, 100}, {{20}, 140}}, {}};
    tuning.idle_cache_seconds = 45;
    cache.retire_idle(144);
    assert(cache.texture_lookup.size() == 2);
    cache.retire_idle(145);
    assert(cache.texture_lookup.size() == 1 && cache.texture_lookup.count(2));
    assert(cold.texture_size == 0 && !cold.is_imported && hot.texture_size == 64);
    assert(cache.textures[0].texture.image == 0 && cache.textures[1].texture.image == 20);
    assert(state.slot.destroy_queue.retired == std::set<int>{10}); // Queued, never destroyed immediately.
    cache.retire_idle(145);
    assert(state.slot.destroy_queue.retired.size() == 1);
    tuning.idle_cache_seconds = 0;
    cache.retire_idle(1000);
    assert(cache.texture_lookup.size() == 1);
    tuning.idle_cache_seconds = 30;
    cache.retire_idle(1001);
    assert(cache.texture_lookup.empty() && state.slot.destroy_queue.retired.size() == 2);
}
