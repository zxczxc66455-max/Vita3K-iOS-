// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// This translation unit deliberately uses manual reference counting.
#import <Foundation/Foundation.h>
#include <util/autorelease_pool.h>

namespace util {
AutoreleasePool::AutoreleasePool()
    : pool([[NSAutoreleasePool alloc] init]) {
}

AutoreleasePool::~AutoreleasePool() {
    [static_cast<NSAutoreleasePool *>(pool) drain];
}
} // namespace util
