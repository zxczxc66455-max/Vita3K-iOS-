#import <Foundation/Foundation.h>
#include <vita3k_ios/NativeFrontend.h>
#include <util/presentation_limiter.h>
#include <cassert>
static NSUserDefaults *test_defaults;
static NSString *to_ns(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()]; }
static std::string to_std(NSString *s) { return s.UTF8String ?: ""; }
// PERSISTENCE
// BRIDGE
int main() {
    @autoreleasepool {
        NSString *suite = [@"tsubomi.fps-test." stringByAppendingString:NSUUID.UUID.UUIDString];
        test_defaults = [[NSUserDefaults alloc] initWithSuiteName:suite];
        assert(vita3k_ios_load_fps_limit() == 60);
        Vita3KIOSSettings fallback;
        for (int limit : {0, 30, 60, -1, 120}) {
            const int expected = util::normalize_fps_limit(limit);
            vita3k_ios_save_fps_limit(limit);
            assert(vita3k_ios_load_fps_limit() == expected);
            fallback.fps_limit = limit;
            TsubomiSettings *settings = [[TsubomiSettings alloc] initWithCoreSettings:fallback];
            assert(settings.fpsLimit == expected);
            TsubomiSettings *copy = [settings copy];
            assert([copy coreSettings].fps_limit == expected);
            store_game_settings(@"TEST00001", [copy coreSettings]);
            fallback.fps_limit = 60;
            assert(game_settings_or(@"TEST00001", fallback).fps_limit == expected);
            assert(vita3k_ios_load_fps_limit() == expected);
        }
        [test_defaults setObject:@{@"vsync": @YES} forKey:game_settings_key(@"TEST00001")];
        fallback.fps_limit = 30;
        assert(game_settings_or(@"TEST00001", fallback).fps_limit == 30);
        [test_defaults removeObjectForKey:game_settings_key(@"TEST00001")];
        assert(game_settings_or(@"TEST00001", fallback).fps_limit == 30);
        [test_defaults removePersistentDomainForName:suite];
    }
}
