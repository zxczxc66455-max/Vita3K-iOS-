"""Host concurrency checks for HLE/render resource admission and shader lifetime."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class HLERenderTests(unittest.TestCase):
    def compile_run(self, root, source, name):
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / (name + ".cpp")
            binary = Path(directory) / name
            cpp.write_text(source)
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(root / "vita3k/util/include"),
                "-I", str(root / "vita3k/threads/include"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=25)

    def test_bounded_warmup_and_guest_release(self):
        root = Path(__file__).resolve().parents[2]
        self.compile_run(root, (Path(__file__).parent / "hle_admission.cpp").read_text(), "admission")

    def test_display_callback_allocation_guards(self):
        root = Path(__file__).resolve().parents[2]
        gxm = (root / "vita3k/modules/SceGxm/SceGxm.cpp").read_text()
        start = gxm.index("    const auto callback_size = emuenv.gxm.params.displayQueueCallbackDataSize;")
        allocation = gxm[start:gxm.index("\n    DisplayFrameInfo *frame =", start)]
        start = gxm.index("    // Widen before multiplying: both values are guest controlled.")
        validation = gxm[start:gxm.index("\n    emuenv.gxm.params = *params;", start)]
        fixture = (Path(__file__).parent / "hle_display.cpp").read_text()
        self.compile_run(root, fixture.replace("// INSERT_ALLOCATION", allocation).replace("// INSERT_VALIDATION", validation), "display")

    def test_pipeline_publication_failure_and_backlog(self):
        root = Path(__file__).resolve().parents[2]
        pipeline = (root / "vita3k/renderer/src/vulkan/pipeline_cache.cpp").read_text()
        start = pipeline.index("void PipelineCache::compiler_thread(")
        worker = pipeline[start:pipeline.index("\nstatic vk::StencilOpState", start)]
        start = pipeline.index("vk::Pipeline PipelineCache::retrieve_pipeline(")
        retrieve = pipeline[start:pipeline.index("\nvoid PipelineCache::enqueue_shader_warmup", start)]
        fixture = (Path(__file__).parent / "hle_pipeline.cpp").read_text()
        start = pipeline.index("struct CompileRequest {")
        fixture = fixture.replace("// INSERT_REQUEST", pipeline[start:pipeline.index("\nPipelineCache::PipelineCache", start)])
        self.compile_run(root, fixture.replace("// INSERT_PIPELINE", worker + retrieve), "pipeline")
