"""Exercise production streaming PUP and cache-index code on a host compiler.

Platform filesystem aliases and SCE metadata parsing are fixture adapters;
AES-CTR and production payload/decrypt/copy control flow execute for real.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class IOSInstallAndCacheTests(unittest.TestCase):
    def test_streaming_pup_and_cache_index(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Requires a host C++ compiler and OpenSSL development library")
        crypto_flags = ["-lcrypto"]
        if shutil.which("pkg-config"):
            flags = subprocess.run(["pkg-config", "--cflags", "--libs", "openssl"], capture_output=True, text=True)
            if flags.returncode == 0:
                crypto_flags = shlex.split(flags.stdout)
        if crypto_flags == ["-lcrypto"] and shutil.which("brew"):
            prefix = subprocess.run(["brew", "--prefix", "openssl@3"], capture_output=True, text=True)
            if prefix.returncode == 0:
                crypto_flags = ["-I" + prefix.stdout.strip() + "/include", "-L" + prefix.stdout.strip() + "/lib", "-lcrypto"]
        # A missing host crypto SDK is a prerequisite skip, not a compiler
        # failure in the production fixture. iOS vcpkg libraries cannot link to
        # a macOS host test executable.
        with tempfile.TemporaryDirectory() as directory:
            probe = subprocess.run(compiler + ["-x", "c++", "-", *crypto_flags,
                "-o", str(Path(directory) / "probe")], input="#include <openssl/evp.h>\nint main() { EVP_CIPHER_free(EVP_CIPHER_fetch(nullptr, \"AES-128-CTR\", nullptr)); }\n",
                capture_output=True, text=True)
            if probe.returncode != 0:
                self.skipTest("Requires host OpenSSL 3 headers and libcrypto (pkg-config or Homebrew)")
        root = Path(__file__).resolve().parents[2]
        pup = (root / "vita3k/packages/src/pup.cpp").read_text()
        shaders = (root / "vita3k/renderer/src/shaders.cpp").read_text()
        source = (Path(__file__).parent / "ios_install_and_cache.cpp").read_text()
        source = source.replace("// INSERT_PACKAGE_NAME", pup[pup.index("static const char *FSTYPE"):pup.index("static void extract_pup_files")])
        source = source.replace("// INSERT_PUP", pup[pup.index("static void extract_pup_files"):pup.index("static void decrypt_pup_packages")])
        archive = (root / "vita3k/packages/src/archive.cpp").read_text()
        struct_start = archive.index("struct InstallOutput {")
        source = source.replace("// INSERT_ARCHIVE_OUTPUT", archive[struct_start:archive.index("\n};", struct_start) + 3])
        start = archive.index("size_t write_install_file(")
        source = source.replace("// INSERT_ARCHIVE_WRITE", archive[start:archive.index("\nstd::string install_target", start)])
        pipeline = (root / "vita3k/renderer/src/vulkan/pipeline_cache.cpp").read_text()
        start = pipeline.index("    const int nb_logical_threads =")
        source = source.replace("// INSERT_WORKER_POLICY", pipeline[start:pipeline.index("\n#ifdef VITA3K_PLATFORM_IOS\n    if (ios_runtime::tuning.prewarm_shader_cache)", start)])
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        source = source.replace("// INSERT_SETTINGS", main[main.index("void apply_native_settings("):main.index("std::optional<AppLaunchRequest> choose_boot_title")])
        config = (root / "vita3k/config/src/config.cpp").read_text()
        start = config.index("ExitCode serialize_config(")
        source = source.replace("// INSERT_CONFIG_SAVE", config[start:config.index("\n}", start) + 2])
        source = source.replace("// INSERT_CACHE", shaders[shaders.index("bool get_shaders_cache_hashs"):shaders.index("static Sha256Hash get_shader_hash")])
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "fixture.cpp"
            binary = Path(directory) / "fixture"
            fixture.write_text(source)
            miniz = Path(directory) / "miniz.o"
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-c",
                str(root / "external/miniz/miniz.c"), "-o", str(miniz)], check=True)
            subprocess.run(compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "vita3k/packages/include"), "-I", str(root / "vita3k/util/include"), "-I", str(root / "external/miniz"), str(fixture), str(miniz), *crypto_flags, "-o", str(binary)], check=True)
            subprocess.run([str(binary), directory], check=True)
