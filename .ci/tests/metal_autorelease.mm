#import <Foundation/Foundation.h>
#include <util/autorelease_pool.h>
#include <cassert>
#include <stdexcept>
#include <thread>
#include <type_traits>

static thread_local int destroyed = 0;
@interface PoolProbe : NSObject
@end
@implementation PoolProbe
- (void)dealloc {
    ++destroyed;
    [super dealloc];
}
@end

int main() {
    static_assert(!std::is_move_constructible_v<util::AutoreleasePool>);
    static_assert(!std::is_copy_constructible_v<util::AutoreleasePool>);
    std::thread worker([] {
        PoolProbe *retained;
        {
            util::AutoreleasePool frame;
            [[[PoolProbe alloc] init] autorelease];
            retained = [[PoolProbe alloc] init];
            try {
                util::AutoreleasePool batch;
                [[[PoolProbe alloc] init] autorelease];
                throw std::runtime_error("compile failed");
            } catch (const std::runtime_error &) {}
            assert(destroyed == 1);
        }
        assert(destroyed == 2); // Strong ownership survives pool draining.
        [retained release];
        assert(destroyed == 3);
    });
    worker.join();
    assert(destroyed == 0);
}
