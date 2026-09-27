#include <algorithm>
#include <cassert>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <util/ios_runtime_tuning.h>
// Host GCC 11 lacks C++23 ranges::contains used by the production module loader.
namespace std::ranges {
template <typename Range, typename T>
bool contains(const Range &range, const T &value) {
    return std::find(range.begin(), range.end(), value) != range.end();
}
}
enum SceSysmoduleModuleId { SCE_SYSMODULE_AVPLAYER, SCE_SYSMODULE_MP4, SCE_SYSMODULE_ATRAC };
enum class ModulesMode { AUTOMATIC, MANUAL, HYBRID };
struct EmuEnvState {
    struct {
        struct {
            ModulesMode modules_mode = ModulesMode::AUTOMATIC;
            std::vector<std::string> lle_modules;
        } current_config;
    } cfg;
};
static std::map<SceSysmoduleModuleId, std::vector<std::string>> sysmodule_paths{
    {SCE_SYSMODULE_AVPLAYER, {"libsceavplayer", "libscemp4"}},
    {SCE_SYSMODULE_MP4, {"libscemp4"}}, {SCE_SYSMODULE_ATRAC, {"libatrac"}}};
static const std::vector<SceSysmoduleModuleId> auto_lle_modules{
    SCE_SYSMODULE_AVPLAYER, SCE_SYSMODULE_MP4, SCE_SYSMODULE_ATRAC};
static std::vector<std::string> init_auto_lle_module_names() { return {"libsceavplayer", "libscemp4", "libatrac"}; }
// INSERT_MODULES
int main() {
    EmuEnvState env;
    // Concurrent first use initializes the immutable name list exactly once.
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i) readers.emplace_back([&] { assert(is_lle_module("libsceavplayer", env)); });
    for (auto &reader : readers) reader.join();
    assert(is_lle_module(SCE_SYSMODULE_AVPLAYER, env));
    ios_runtime::tuning.prefer_hle_avplayer = true;
#ifdef VITA3K_PLATFORM_IOS
    constexpr bool firmware_avplayer = false;
#else
    constexpr bool firmware_avplayer = true;
#endif
    assert(is_lle_module(SCE_SYSMODULE_AVPLAYER, env) == firmware_avplayer);
    assert(is_lle_module("libsceavplayer", env) == firmware_avplayer);
    assert(is_lle_module(SCE_SYSMODULE_MP4, env) && is_lle_module("libscemp4", env));
    assert(is_lle_module(SCE_SYSMODULE_ATRAC, env) && is_lle_module("libatrac", env));
    env.cfg.current_config.modules_mode = ModulesMode::MANUAL;
    env.cfg.current_config.lle_modules = {"libsceavplayer"};
    assert(is_lle_module(SCE_SYSMODULE_AVPLAYER, env) == firmware_avplayer);
    assert(is_lle_module("libsceavplayer", env) == firmware_avplayer);
    assert(!is_lle_module(SCE_SYSMODULE_ATRAC, env));
    ios_runtime::tuning.prefer_hle_avplayer = false;
    assert(is_lle_module(SCE_SYSMODULE_AVPLAYER, env));
}
