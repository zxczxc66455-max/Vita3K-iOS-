"""Exercise production iOS retirement and queue code with recording GPU handles."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class A11ResourceTests(unittest.TestCase):
    def test_descriptor_texture_and_device_policy(self):
        root = Path(__file__).resolve().parents[2]
        context = (root / "vita3k/renderer/src/vulkan/context.cpp").read_text()
        texture = (root / "vita3k/renderer/src/vulkan/texture.cpp").read_text()
        source = (Path(__file__).parent / "a11_resources.cpp").read_text()
        start = context.index("vk::DescriptorSet retrieve_frame_descriptor(")
        source = source.replace("// INSERT_DESCRIPTORS", context[start:context.index("\n#endif", start)])
        start = texture.index("void VKTextureCache::retire_idle(")
        source = source.replace("// INSERT_TEXTURE_RETIREMENT", texture[start:texture.index("\n#endif", start)])
        # Retirement must occur only after the GPU barrier and command reset;
        # newly queued images must survive until the slot is revisited.
        frame = context[context.index("void new_frame(VKContext &context)"):]
        self.assertLess(frame.index("device.waitForFences"), frame.index("retire_frame_descriptors"))
        self.assertLess(frame.index("device.resetCommandPool"), frame.index("retire_frame_descriptors"))
        self.assertLess(frame.index("frame.destroy_queue.destroy_objects()"), frame.index("texture_cache.retire_idle"))
        self.compile_run(root, source, "resources")

    def test_concurrent_queue_and_ordered_batches(self):
        root = Path(__file__).resolve().parents[2]
        batch = (root / "vita3k/renderer/src/batch.cpp").read_text()
        start = batch.index("void process_batches(")
        source = (Path(__file__).parent / "a11_batches.cpp").read_text().replace(
            "// INSERT_BATCHES", batch[start:batch.index("\nvoid reset_command_list", start)])
        self.compile_run(root, source, "batches")

    def test_media_module_override_preserves_other_firmware(self):
        root = Path(__file__).resolve().parents[2]
        module = (root / "vita3k/module/src/load_module.cpp").read_text()
        start = module.index("bool is_lle_module(SceSysmoduleModuleId")
        by_id = module[start:module.index("\nstatic std::vector<std::string> init_auto_lle_module_names", start)]
        start = module.index("bool is_lle_module(const std::string &")
        by_name = module[start:module.index("\nbool is_module_loaded", start)]
        fixture = (Path(__file__).parent / "a11_modules.cpp").read_text()
        for ios in (False, True):
            source = ("#define VITA3K_PLATFORM_IOS\n" if ios else "") + fixture.replace("// INSERT_MODULES", by_id + by_name)
            self.compile_run(root, source, "modules")

    def compile_run(self, root, source, name):
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / f"{name}.cpp"
            binary = Path(directory) / name
            cpp.write_text(source)
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(root / "vita3k/util/include"),
                "-I", str(root / "vita3k/threads/include"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)
