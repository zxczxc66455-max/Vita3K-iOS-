"""Deterministic presentation pacing and production prediction/settings paths."""
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FPSSettingsTests(unittest.TestCase):
    def test_pacing_prediction_and_launch(self):
        display = (ROOT / "vita3k/display/src/display.cpp").read_text()
        start = display.index("void update_prediction(")
        prediction = display[start:display.index("\nvoid DisplayState::deinit", start)]
        main = (ROOT / "ios/src/UpstreamMain.cpp").read_text()
        start = main.index("                    emuenv->display.fps_limit.store(session_settings")
        launch = main[start:main.index('                    SDL_Log("Vita3K iOS: load_and_run");', start)]
        start = main.index("    const auto restore_global_config = [&] {")
        restore = main[start:main.index("\n    take_cpu_backend_error();", start)]
        fixture = (Path(__file__).parent / "fps_settings.cpp").read_text()
        state = (ROOT / "vita3k/renderer/include/renderer/state.h").read_text()
        signal = next(line for line in state.splitlines() if "should_display{" in line)
        fixture = fixture.replace("// RENDER_SIGNAL", signal)
        screen = (ROOT / "vita3k/renderer/src/vulkan/screen_renderer.cpp").read_text()
        start = screen.index("    auto result = state.general_queue.presentKHR(&present_info);")
        counter = screen[start:screen.index("    if (result == vk::Result::eSuboptimalKHR)", start)]
        fixture = fixture.replace("// PRESENT_COUNTER", counter)
        fixture = fixture.replace("// PREDICTION", prediction).replace("// LAUNCH", launch).replace("// RESTORE", restore)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "fps.cpp"
            cpp.write_text(fixture)
            binary = Path(directory) / "fps"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(ROOT / "vita3k/util/include"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(platform.system() == "Darwin", "Requires Apple Foundation/Objective-C++")
    def test_persistence_and_bridge_copy(self):
        native = (ROOT / "ios/src/NativeFrontend.mm").read_text()
        start = native.index("NSString *game_settings_key(")
        persistence = native[start:native.index("\n} // namespace", start)]
        start = native.index("int vita3k_ios_load_fps_limit()")
        persistence += native[start:native.index("\nbool vita3k_ios_consume_double_buffer", start)]
        # Isolate defaults in a disposable suite; execute the production reads/writes.
        persistence = persistence.replace("NSUserDefaults.standardUserDefaults", "test_defaults")
        header = (ROOT / "ios/include/vita3k_ios/TsubomiBridge.h").read_text()
        start = header.index("@interface TsubomiSettings :")
        interface = header[start:header.index("@end", start) + 4]
        bridge = (ROOT / "ios/src/TsubomiBridge.mm").read_text()
        start = bridge.index("@interface TsubomiSettings ()")
        end = bridge.index("@end", bridge.index("@implementation TsubomiSettings", start)) + 4
        implementation = bridge[start:end]
        fixture = (Path(__file__).parent / "fps_persistence.mm").read_text()
        fixture = fixture.replace("// PERSISTENCE", persistence).replace("// BRIDGE", interface + implementation)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "settings.mm"
            source.write_text(fixture)
            binary = Path(directory) / "settings"
            subprocess.run(["xcrun", "clang++", "-std=c++17", "-fobjc-arc", "-framework", "Foundation",
                            "-I", str(ROOT / "vita3k/util/include"), "-I", str(ROOT / "ios/include"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
