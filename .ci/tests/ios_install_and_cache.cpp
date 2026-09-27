#include <atomic>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <openssl/evp.h>
#include <packages/stream_copy.h>
#include <packages/stream_decrypt.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <util/ios_runtime_tuning.h>
#include <util/presentation_limiter.h>
#include <vector>
namespace fs {
using namespace std::filesystem;
using std::ifstream;
using std::ofstream;
} // namespace fs
namespace boost::system {
using error_code = std::error_code;
}
namespace fs_utils {
fs::path path_concat(const fs::path &p, const char *suffix) { return p.string() + suffix; }
} // namespace fs_utils
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
constexpr size_t HEADER_LENGTH = 0x1000;
const std::map<int, std::string> PUP_TYPES{ { 0x100, "version.txt" }, { 0x101, "license.xml" } };
constexpr uint32_t SCE_MAGIC = 0x454353;
namespace fmt {
template <class A, class B>
std::string format(const char *, A, B) { return "segment.pkg"; }
} // namespace fmt
// INSERT_PACKAGE_NAME
struct KeyStore {};
enum class SelfType { NONE };
struct MetadataInfo {
    static constexpr uint64_t Size = 64;
};
struct MetadataHeader {
    static constexpr uint64_t Size = 32;
};
struct SceHeader {
    static constexpr size_t Size = 16;
    uint64_t header_length, metadata_offset;
    explicit SceHeader(const char *p) {
        std::memcpy(&header_length, p, 8);
        std::memcpy(&metadata_offset, p + 8, 8);
    }
};
struct Segment {
    uint64_t offset, size;
    bool compressed;
    std::string key, iv;
};
static Segment fixture_segment;
static auto get_key_type(std::ifstream &, const SceHeader &) { return std::make_tuple(uint64_t(0), SelfType::NONE); }
static auto get_segments(const uint8_t *p, const SceHeader &h, KeyStore &, uint64_t, SelfType) {
    assert(p[h.header_length - 1] == 0); // Real metadata decoder is outside this fixture.
    return std::vector<Segment>{ fixture_segment };
}
// INSERT_PUP
// INSERT_ARCHIVE_OUTPUT
// INSERT_ARCHIVE_WRITE
static int fixture_cores = 6, fixture_ram = 3072;
static int SDL_GetNumLogicalCPUCores() { return fixture_cores; }
static int SDL_GetSystemRAM() { return fixture_ram; }
static int compiler_workers() {
    int nb_worker_threads = 0;
#define VITA3K_PLATFORM_IOS
// INSERT_WORKER_POLICY
#undef VITA3K_PLATFORM_IOS
    return nb_worker_threads;
}
using Sha256Hash = std::array<uint8_t, 32>;
struct ShadersHash {
    Sha256Hash frag, vert;
};
enum class Backend { Vulkan,
    OpenGL };
namespace shader {
constexpr uint32_t CURRENT_VERSION = 7;
}
struct State {
    virtual ~State() = default;
    fs::path shaders_path, shaders_log_path;
    std::vector<ShadersHash> shaders_cache_hashs;
    Backend current_backend = Backend::Vulkan;
    uint32_t get_features_mask() const { return 42; }
};
namespace vulkan {
struct VKState : State {
    struct Cache {
        int reads = 0;
        void read_pipeline_cache() { ++reads; }
    } pipeline_cache;
};
} // namespace vulkan
namespace fmt {
template <class T>
std::string format(const char *, T value) { return std::string("hashs-") + value + ".dat"; }
} // namespace fmt
namespace renderer {
void save_shaders_cache_hashs(State &, std::vector<ShadersHash> &);
}
// INSERT_CACHE
namespace renderer {
void save_shaders_cache_hashs(State &s, std::vector<ShadersHash> &h) { ::save_shaders_cache_hashs(s, h); }
} // namespace renderer
struct SettingsFields {
    float resolution_multiplier = 1;
    bool v_sync = true, shader_cache = true, fps_hack = false, cpu_opt = true, ngs_enable = true;
    bool async_pipeline_compilation = true, high_accuracy = false, disable_surface_sync = false;
    int anisotropic_filtering = 1;
    std::string memory_mapping = "disabled", audio_backend = "SDL", hidden_setting = "default";
};
struct Config : SettingsFields {
    using CurrentConfig = SettingsFields;
    CurrentConfig current_config;
    std::vector<int> controller_binds{ 0, 1, 2, 3 };
    // Production Config assignment copies CONFIG_LIST but excludes current_config.
    Config &operator=(const Config &rhs) {
        SettingsFields::operator=(rhs);
        controller_binds = rhs.controller_binds;
        return *this;
    }
};
struct Vita3KIOSSettings : SettingsFields {
    bool surface_sync = true, double_buffer = false;
    int fps_limit = 60;
    int bind_cross = 0, bind_circle = 1, bind_square = 2, bind_triangle = 3;
};
struct EmuEnvState {
    Config cfg;
    struct Display {
        bool fps_hack = false;
        std::atomic<int> fps_limit{ 60 };
    } display;
};
constexpr int SDL_GAMEPAD_BUTTON_SOUTH = 0, SDL_GAMEPAD_BUTTON_EAST = 1, SDL_GAMEPAD_BUTTON_WEST = 2, SDL_GAMEPAD_BUTTON_NORTH = 3;
static int face_button_physical_for_slot(int slot) { return slot; }
static std::string ios_memory_mapping_for(const Vita3KIOSSettings &s) { return s.double_buffer ? "double-buffer" : "disabled"; }
static std::string restart_setting_name(int) { return "fixture"; }
static int saved_fps_limit = 60;
static void vita3k_ios_save_fps_limit(int value) { saved_fps_limit = util::normalize_fps_limit(value); }
static void vita3k_ios_report_settings_result(const std::vector<std::string> &) {}
namespace app {
struct Result {
    std::vector<int> restart_required_settings;
    bool runtime_settings_applied = true;
};
static Result commit_settings(EmuEnvState &env, const Config &desired) {
    env.cfg.current_config = desired.current_config;
    return {};
}
} // namespace app
// INSERT_SETTINGS
// Serialize through the production file-commit code; the fixture emitter only
// replaces YAML formatting, which this patch does not change.
namespace YAML {
constexpr int BeginDoc = 0, EndDoc = 0;
struct Emitter {
    std::string data;
    Emitter &operator<<(int) { return *this; }
    Emitter &operator<<(const std::string &s) {
        data += s;
        return *this;
    }
    const char *c_str() const { return data.c_str(); }
};
} // namespace YAML
enum ExitCode { Success,
    InvalidApplicationPath };
static fs::path check_path(const fs::path &path) { return path; }
static std::string get(const Config &cfg) { return cfg.hidden_setting; }
// INSERT_CONFIG_SAVE
static void write(const fs::path &p, const std::string &s) {
    std::ofstream out(p, std::ios::binary);
    out.write(s.data(), s.size());
    assert(out);
}
static std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), {} };
}
static void rejected(const std::function<void()> &fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::runtime_error &) {
        failed = true;
    }
    assert(failed);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const fs::path root = argv[1];
    EmuEnvState env;
    env.cfg.current_config.hidden_setting = "keep me";
    Vita3KIOSSettings settings;
    settings.shader_cache = false;
    settings.resolution_multiplier = 0.5f;
    apply_native_settings(env, settings);
    assert(env.cfg.current_config.hidden_setting == "keep me");
    assert(!env.cfg.current_config.shader_cache);
    settings.shader_cache = true;
    apply_game_session_settings(env, settings);
    assert(env.cfg.current_config.shader_cache);
    settings.shader_cache = false;
    apply_game_session_settings(env, settings);
    assert(!env.cfg.current_config.shader_cache);
    for (bool enabled : { false, true }) {
        settings.v_sync = enabled;
        settings.shader_cache = enabled;
        settings.cpu_opt = enabled;
        settings.ngs_enable = enabled;
        settings.async_pipeline_compilation = enabled;
        settings.high_accuracy = enabled;
        settings.surface_sync = enabled;
        settings.double_buffer = enabled;
        settings.anisotropic_filtering = enabled ? 16 : 1;
        settings.resolution_multiplier = enabled ? 2.0f : 0.5f;
        for (bool per_game : { false, true }) {
            if (per_game)
                apply_game_session_settings(env, settings);
            else
                apply_native_settings(env, settings);
            const auto &current = env.cfg.current_config;
            assert(current.v_sync == enabled && current.shader_cache == enabled);
            assert(current.cpu_opt == enabled && current.ngs_enable == enabled);
            assert(current.async_pipeline_compilation == enabled && current.high_accuracy == enabled);
            assert(current.disable_surface_sync == !enabled);
            assert(current.memory_mapping == (enabled ? "double-buffer" : "disabled"));
            assert(current.anisotropic_filtering == settings.anisotropic_filtering);
            assert(current.resolution_multiplier == settings.resolution_multiplier);
            assert(!current.fps_hack && env.display.fps_limit.load() == 60);
        }
    }
    for (int limit : {0, 30, 60, -1, 120}) {
        settings.fps_limit = limit;
        apply_native_settings(env, settings);
        assert(saved_fps_limit == util::normalize_fps_limit(limit));
        assert(env.display.fps_limit == saved_fps_limit);
        settings.fps_limit = limit == 30 ? 0 : 30;
        apply_game_session_settings(env, settings);
        assert(env.display.fps_limit == settings.fps_limit);
        assert(saved_fps_limit == util::normalize_fps_limit(limit)); // Per-game never persists globally.
    }
    Config cfg;
    cfg.hidden_setting = "saved";
    const auto config_path = root / "config.yml";
    assert(serialize_config(cfg, config_path) == Success);
    assert(read(config_path) == "saved");
    fs::create_directory(root / "config.yml.tmp");
    cfg.hidden_setting = "replacement";
    assert(serialize_config(cfg, config_path) == InvalidApplicationPath);
    assert(read(config_path) == "saved");
    fs::remove(root / "config.yml.tmp");
    assert(serialize_config(cfg, config_path) == Success);
    assert(read(config_path) == "replacement");
    const std::string payload(200003, 'x');
    assert(compiler_workers() == 2);
    ios_runtime::tuning.shader_workers = 1;
    assert(compiler_workers() == 1);
    ios_runtime::tuning.shader_workers = 4;
    assert(compiler_workers() == 4);
    ios_runtime::tuning.shader_workers = 0;
    fixture_cores = 2;
    assert(compiler_workers() == 1);
    fixture_cores = 6;
    fixture_ram = 4096;
    assert(compiler_workers() == 2);
    fixture_ram = 0;
    assert(compiler_workers() == 2);
    fixture_cores = 16;
    assert(compiler_workers() == 4);
    fixture_ram = 3072;
    assert(compiler_workers() == 2);
    fixture_cores = 2;
    fixture_ram = 4096;
    assert(compiler_workers() == 1);
    std::vector<uint64_t> progress;
    InstallOutput archive_output{ .stream = std::ofstream(root / "archive-payload"),
        .progress = [&](uint64_t bytes) { progress.push_back(bytes); } };
    assert(write_install_file(&archive_output, 0, payload.data(), 100000) == 100000);
    assert(write_install_file(&archive_output, 100000, payload.data() + 100000, payload.size() - 100000) == payload.size() - 100000);
    assert(progress == std::vector<uint64_t>({ 100000, payload.size() }));
    assert(write_install_file(&archive_output, 0, payload.data(), 1) == 0);
    archive_output.stream.close();
    assert(read(root / "archive-payload") == payload);
    std::istringstream input(payload);
    std::ostringstream output;
    assert(packages::copy_stream_exact(input, output, payload.size()));
    assert(output.str() == payload);
    std::istringstream short_input("abc");
    std::ostringstream short_output;
    assert(!packages::copy_stream_exact(short_input, short_output, 4));
    std::istringstream source("abc");
    std::ostringstream bad_output;
    bad_output.setstate(std::ios::badbit);
    assert(!packages::copy_stream_exact(source, bad_output, 3));
    std::istringstream empty;
    std::ostringstream empty_out;
    assert(packages::copy_stream_exact(empty, empty_out, 0));

    std::string pup(0xa0, '\0');
    std::memcpy(pup.data(), "SCEUF", 5);
    const uint32_t count = 1;
    std::memcpy(pup.data() + 0x18, &count, 4);
    uint64_t record[]{ 0x100, 0xa0, payload.size(), 0 };
    std::memcpy(pup.data() + 0x80, record, sizeof(record));
    pup += payload;
    fs::create_directory(root / "pup");
    write(root / "input.pup", pup);
    extract_pup_files(root / "input.pup", root / "pup");
    assert(read(root / "pup/version.txt") == payload);
    write(root / "input.pup", pup.substr(0, pup.size() - 1));
    rejected([&] { extract_pup_files(root / "input.pup", root / "pup"); });
    write(root / "input.pup", "SCEUF");
    rejected([&] { extract_pup_files(root / "input.pup", root / "pup"); });
    // Small unknown/SCE payloads must not be rejected just for being <4 KiB.
    auto small_package = [&](size_t length, uint64_t metadata_offset) {
        std::string entry(length, '\0');
        if (length >= 24) {
            const uint32_t fields[]{ SCE_MAGIC, 3, 0x30040, 0 };
            std::memcpy(entry.data(), fields, sizeof(fields));
            std::memcpy(entry.data() + 16, &metadata_offset, 8);
        }
        std::string container = pup.substr(0, 0xa0);
        const uint64_t small_record[]{ 0x300, 0xa0, length, 0 };
        std::memcpy(container.data() + 0x80, small_record, sizeof(small_record));
        container += entry;
        write(root / "small.pup", container);
        return entry;
    };
    const auto small = small_package(64, 59); // metadata tag at last valid byte
    extract_pup_files(root / "small.pup", root / "pup");
    assert(read(root / "pup/segment.pkg") == small);
    small_package(64, 60); // tag would read beyond this entry
    rejected([&] { extract_pup_files(root / "small.pup", root / "pup"); });
    small_package(23, 0);
    rejected([&] { extract_pup_files(root / "small.pup", root / "pup"); });
    small_package(64, std::numeric_limits<uint64_t>::max());
    rejected([&] { extract_pup_files(root / "small.pup", root / "pup"); });
    write(root / "pup/os0-01", "second");
    write(root / "pup/os0-00", payload);
    join_files(root / "pup", "os0-", root / "joined.img");
    assert(read(root / "joined.img") == payload + "second");
    assert(!fs::exists(root / "pup/os0-00"));

    // Real OpenSSL AES-CTR, including a segment not aligned to a cipher block.
    const std::string key(16, '\0');
    fixture_segment = { 256, payload.size(), false, key, key };
    std::vector<unsigned char> encrypted(payload.size() + EVP_MAX_BLOCK_LENGTH);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int amount = 0, final = 0;
    assert(EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(key.data())) == 1);
    assert(EVP_EncryptUpdate(ctx, encrypted.data(), &amount, reinterpret_cast<const unsigned char *>(payload.data()), payload.size()) == 1);
    assert(EVP_EncryptFinal_ex(ctx, encrypted.data() + amount, &final) == 1);
    EVP_CIPHER_CTX_free(ctx);
    std::string sce(256, '\0');
    uint64_t header_length = 256;
    std::memcpy(sce.data(), &header_length, 8);
    sce.append(reinterpret_cast<char *>(encrypted.data()), amount + final);
    write(root / "segment", sce);
    std::ifstream segment(root / "segment", std::ios::binary);
    KeyStore keys;
    decrypt_segments(segment, root, "decrypted", keys);
    assert(read(root / "decrypted.seg02") == payload);
    segment.close();
    fixture_segment.size += 1;
    segment.open(root / "segment", std::ios::binary);
    rejected([&] { decrypt_segments(segment, root, "invalid", keys); });

    // Stream through real miniz and AES across both input and output boundaries.
    auto encrypted_stream = [&](const std::string &plain, bool compressed, bool truncate_input = false, bool fail_output = false) {
        auto cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        std::string bytes(plain.size() + EVP_MAX_BLOCK_LENGTH, '\0');
        int count = 0, tail = 0;
        assert(EVP_EncryptInit_ex(cipher.get(), EVP_aes_128_ctr(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(key.data())) == 1);
        assert(EVP_EncryptUpdate(cipher.get(), reinterpret_cast<unsigned char *>(bytes.data()), &count, reinterpret_cast<const unsigned char *>(plain.data()), plain.size()) == 1);
        assert(EVP_EncryptFinal_ex(cipher.get(), reinterpret_cast<unsigned char *>(bytes.data()) + count, &tail) == 1);
        bytes.resize(count + tail);
        const auto declared = bytes.size();
        if (truncate_input)
            bytes.pop_back();
        std::istringstream source(bytes);
        std::ostringstream dest;
        if (fail_output)
            dest.setstate(std::ios::badbit);
        assert(EVP_DecryptInit_ex(cipher.get(), EVP_aes_128_ctr(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(key.data())) == 1);
        packages::decrypt_stream_exact(source, dest, cipher.get(), declared, compressed);
        return dest.str();
    };
    auto compress_fixture = [](const std::string &plain) {
        mz_ulong size = mz_compressBound(plain.size());
        std::string bytes(size, '\0');
        assert(mz_compress(reinterpret_cast<unsigned char *>(bytes.data()), &size,
                   reinterpret_cast<const unsigned char *>(plain.data()), plain.size())
            == MZ_OK);
        bytes.resize(size);
        return bytes;
    };
    std::string random_bytes(400007, '\0');
    uint32_t seed = 31337;
    for (auto &byte : random_bytes) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        byte = static_cast<char>(seed);
    }
    auto compressed = compress_fixture(random_bytes);
    assert(compressed.size() > 3 * 65536);
    assert(encrypted_stream(compressed, true) == random_bytes);
    const auto dense = compress_fixture(std::string(4 * 1024 * 1024, 'x'));
    assert(encrypted_stream(dense, true) == std::string(4 * 1024 * 1024, 'x'));
    assert(encrypted_stream(compress_fixture(""), true).empty());
    assert(encrypted_stream("", false).empty());
    assert(encrypted_stream(compressed + std::string(23, '\0'), true) == random_bytes);
    rejected([&] { encrypted_stream(compressed, true, true); });
    rejected([&] { encrypted_stream(compressed.substr(0, compressed.size() - 1), true); });
    rejected([&] { encrypted_stream("not zlib", true); });
    compressed.back() ^= 1; // checksum corruption
    rejected([&] { encrypted_stream(compressed, true); });
    rejected([&] { encrypted_stream(dense, true, false, true); });
    rejected([&] { encrypted_stream(payload, false, false, true); });

    vulkan::VKState state;
    state.shaders_path = root / "cache";
    state.shaders_log_path = root / "shaderlog";
    std::vector<ShadersHash> hashes(2);
    hashes[0].frag.fill(23);
    hashes[1].vert.fill(41);
    save_shaders_cache_hashs(state, hashes);
    assert(get_shaders_cache_hashs(state));
    assert(state.shaders_cache_hashs.size() == 2);
    assert(state.shaders_cache_hashs[1].vert == hashes[1].vert);
    assert(state.pipeline_cache.reads == 1);
    const auto index = state.shaders_path / "hashs-vk.dat";
    auto bytes = read(index);
    write(index, bytes.substr(0, bytes.size() - 1));
    assert(!get_shaders_cache_hashs(state));
    assert(state.pipeline_cache.reads == 1);
    size_t huge = std::numeric_limits<size_t>::max();
    std::memcpy(bytes.data(), &huge, sizeof(huge));
    write(index, bytes);
    assert(!get_shaders_cache_hashs(state));
    assert(state.pipeline_cache.reads == 1);
    write(index, "x");
    assert(!get_shaders_cache_hashs(state));
    assert(state.pipeline_cache.reads == 1);
}
