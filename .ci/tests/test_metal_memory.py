"""Check actual VMA configuration and depth selection; Apple pool ownership on macOS."""
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MetalMemoryTests(unittest.TestCase):
    def test_allocator_configuration_and_depth_fallback(self):
        renderer = (ROOT / "vita3k/renderer/src/vulkan/renderer.cpp").read_text()
        start = renderer.index("        vma::AllocatorCreateInfo allocator_info = {")
        config = renderer[start:renderer.index("        allocator = vma::createAllocator", start)]
        screen = (ROOT / "vita3k/renderer/src/vulkan/screen_renderer.cpp").read_text()
        start = screen.index("    if (support_d32u8) {")
        depth = screen[start:screen.index("    create_render_pass();", start)]
        fixture = r'''
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VMA_HPP_NO_STRUCT_CONSTRUCTORS
#include <vk_mem_alloc.hpp>
#include <cassert>
constexpr uint64_t MiB(uint64_t n) { return n * 1024 * 1024; }
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO_ONCE(...) ((void)0)
#define LOG_WARN_ONCE(...) ((void)0)
static vk::Format selected;
bool select_depth(bool support_d32u8, bool support_d24u8) {
    struct { vk::Format deep_stencil_use{}; } state;
    // DEPTH
    selected = state.deep_stencil_use;
    return true;
}
int main() {
    vk::PhysicalDevice physical_device;
    vk::Device device;
    vk::Instance instance;
    vma::VulkanFunctions vulkan_functions{};
    for (bool support_dedicated_allocations : {false, true}) {
        for (int supported_mapping_methods_mask : {1, 3}) {
            // CONFIG
#ifdef VITA3K_PLATFORM_IOS
            assert(!(allocator_info.flags & vma::AllocatorCreateFlagBits::eExternallySynchronized));
            assert(allocator_info.preferredLargeHeapBlockSize == MiB(8));
#else
            assert(allocator_info.flags & vma::AllocatorCreateFlagBits::eExternallySynchronized);
            assert(allocator_info.preferredLargeHeapBlockSize == 0);
#endif
            assert(bool(allocator_info.flags & vma::AllocatorCreateFlagBits::eKhrDedicatedAllocation) == support_dedicated_allocations);
            assert(bool(allocator_info.flags & vma::AllocatorCreateFlagBits::eBufferDeviceAddress) == (supported_mapping_methods_mask > 1));
        }
    }
    assert(select_depth(true, true) && selected == vk::Format::eD32SfloatS8Uint);
    assert(select_depth(true, false) && selected == vk::Format::eD32SfloatS8Uint);
    assert(select_depth(false, true) && selected == vk::Format::eD24UnormS8Uint);
#ifdef VITA3K_PLATFORM_IOS
    assert(!select_depth(false, false));
#else
    assert(select_depth(false, false) && selected == vk::Format::eD16Unorm);
#endif
}
'''.replace("// CONFIG", config).replace("// DEPTH", depth)
        vma = ROOT / "external/VulkanMemoryAllocator-Hpp"
        if not (vma / "Vulkan-Headers/include/vulkan/vulkan.hpp").is_file():
            self.skipTest("Initialize external/VulkanMemoryAllocator-Hpp recursively")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "memory.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "memory"
            for ios in (False, True):
                subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                    "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-isystem", str(vma / "include"), "-isystem", str(vma / "Vulkan-Headers/include"),
                    "-isystem", str(vma / "VulkanMemoryAllocator/include"),
                ] + (["-DVITA3K_PLATFORM_IOS"] if ios else []) + [str(source), "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(platform.system() == "Darwin", "Requires Apple Foundation and Objective-C++")
    def test_autorelease_ownership_and_unwind(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pool"
            subprocess.run([
                "xcrun", "clang++", "-std=c++17", "-fno-objc-arc", "-DVITA3K_PLATFORM_IOS",
                "-I", str(ROOT / "vita3k/util/include"), "-framework", "Foundation",
                str(ROOT / "vita3k/util/src/autorelease_pool.mm"),
                str(ROOT / ".ci/tests/metal_autorelease.mm"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
