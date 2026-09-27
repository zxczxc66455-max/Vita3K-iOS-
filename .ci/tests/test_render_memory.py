"""Exercise production frame recycling with a recording Vulkan device and real queues."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class RenderMemoryTests(unittest.TestCase):
    def test_frame_recycling_and_backpressure(self):
        root = Path(__file__).resolve().parents[2]
        context = (root / "vita3k/renderer/src/vulkan/context.cpp").read_text()
        function = context[context.index("void new_frame("):context.index("void signal_sync_object(")]
        source = (Path(__file__).parent / "render_memory.cpp").read_text().replace("// INSERT_NEW_FRAME", function)
        types = (root / "vita3k/renderer/include/renderer/vulkan/types.h").read_text()
        start = types.index("#ifdef VITA3K_PLATFORM_IOS\nconstexpr int MAX_FRAMES_RENDERING")
        source = source.replace("// INSERT_FRAME_COUNT", types[start:types.index("\n#endif", start) + len("\n#endif")])
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "fixture.cpp"
            fixture.write_text(source)
            binary = Path(directory) / "fixture"
            for defines in [[], ["-DVITA3K_PLATFORM_IOS"]]:
                subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                    "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror", *defines,
                    "-I", str(root / "vita3k/util/include"),
                    "-I", str(root / "vita3k/threads/include"),
                    str(fixture), "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=30)
