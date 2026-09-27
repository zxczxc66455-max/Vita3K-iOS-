# iOS A11 runtime tuning

## Controls

Settings → Performance & Memory offers an explicit A11 memory profile for
`iPhone10,1` through `iPhone10,6` (iPhone 8, 8 Plus and X). It saves a 768 MiB
guest allocation budget, 8 MiB JIT cache per guest thread, one shader compiler
worker, 128 texture entries, legacy Metal bindings, 45-second idle retirement
and no startup shader precompilation. CPU execution remains OS scheduled.
Restart the app to apply runtime preferences. Device & Runtime shows the active
binding, retirement and AvPlayer policies, rather than the newly saved values.

Automatic Metal bindings disable argument buffers on A11. Explicit Legacy and
Argument buffers choices allow comparison on the same game/scene. This uses
MoltenVK 1.4.2's Boolean `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS`, set before instance
creation and through the instance layer settings when available.

## Resource ownership

- iOS never enables `VK_EXT_external_memory_host`, even when advertised. Existing
  Disabled and DoubleBuffer mapping paths preserve owned buffers and copies.
- The iOS swapchain requests two images, clamped to the surface's supported
  minimum/maximum. The driver can return more; iOS Vulkan now uses two fence-protected frame slots.
- Descriptor sets are allocated on demand in packs per frame slot (32 texture
  sets, 16 attachment sets). They are reused every frame. Idle tail packs are
  destroyed only after that slot's fences complete and its command pools reset.
- Idle cached textures are checked at most once per second during rendering.
  Entries idle for 30/45/60 seconds leave the lookup/LRU cache and enter the
  existing deferred GPU destruction queue. The queue drains on a later visit
  to the frame slot. Off disables timed retirement. Hot textures remain cached.
- Compiled shaders and pipelines are not time-purged: compilation workers and
  in-flight draws retain references. Removing them needs a separate lifetime
  protocol; destructive periodic clearing would introduce races and stutter.
- Existing guest memory reservation, page allocation, alignment and free-page
  decommit behavior are preserved. `sceKernelAllocMemBlock` must satisfy the
  game's requested size; subdividing or truncating it changes the guest ABI.
  The guest budget excludes JIT, GPU, codec and UI memory and is not an iOS
  process footprint guarantee.

## Commands and scheduling

GXM already links deferred command chains into immediate lists. The renderer
now checks readiness and dequeues a whole ready list under one queue lock,
without allocating temporary list wrappers. A blocked sync stays at the head;
commands beyond a display boundary remain queued. Empty waits use an explicit
3-millisecond duration (the old `top(3)` waited three microseconds).

Vulkan recording and shared caches remain on a single consumer. Parallel
recording of those mutable objects is not safe without per-worker contexts and
an ordered merge protocol. Guest and render threads use user-initiated QoS;
shader workers use utility QoS. iOS chooses physical cores. No unsupported
`pthread_setaffinity_np` calls or deterministic physical-core claims are made.
Audio callback scheduling remains owned by its backend.

The optional HLE AvPlayer switch bypasses firmware AvPlayer in both module-ID
and module-name lookup. It uses the existing host decoder, not a new
AVFoundation/VideoToolbox implementation. MP4, ATRAC and other required firmware
modules keep their policies. The automatic module-name list now initializes
once, avoiding concurrent initialization writes.

## Verification

Run the repository's portable regression suite:

```sh
git submodule update --init --recursive external/dynarmic
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s .ci/tests -v
```

New tests compile the production descriptor allocation/retirement, texture
retirement, module policy and batch-consumption functions with recording
adapters. Real queue tests cover concurrent producers, backpressure, blocked
syncs, shutdown and frame boundaries. The frame recycling test checks fence
and destruction order. These do not emulate Metal or certify performance.

The actual app needs the Xcode 26 / iOS SDK workflow in
`.github/workflows/ios-upstream.yml`, including its pinned dependencies and
iOS 16.7 deployment check. On a device, compare Automatic and Legacy bindings
on the same scene, check audio/video and controller input, and run beyond the
selected idle timeout through scene changes and repeated launch/quit cycles.
Measure footprint and frame time; lower resource retention alone is not proof
of better FPS or freedom from jetsam.

Reference: [MoltenVK 1.4.2 configuration](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md#mvk_config_use_metal_argument_buffers).

## HLE/render follow-up

The GXM display queue is capped at one queued entry plus one executing callback
on iOS. Its existing condition-variable wait puts a producer to sleep when full.
The consumer retains the queued entry until its GXM sync waits complete. The
Vulkan backend separately reuses two frame slots only after waiting for their
GPU fences. This bounds these queues, not arbitrary memory a game can allocate.
`sceDisplaySetFrameBuf` coalesces into a latest-image snapshot; adding a wait
inside it could block the very callback needed to drain the GXM queue.
Initialization validates callback sizes without 32-bit multiplication overflow,
and failed callback-data allocation returns an error before copying.

Settings → Shader compilation → Prepare cached shaders in background is on by
default and takes effect after restart. Program creation submits only the hash
to one utility worker. The queue holds at most 32 active/pending hashes, suppresses
duplicates and skips speculation when full. It prepares existing SPIR-V modules;
it does not construct a complete PSO from an unpaired registered program. No
guest program pointer crosses this queue. Shutdown discards pending warmups and
joins the active load before destroying shader modules.

Async pipeline jobs are also capped at 32 on iOS. Overflow compiles on the render
thread, allowing existing command/display queue limits to apply backpressure.
Completed handles and cache-save deadlines use atomic publication. Failed jobs
clear their compiling sentinel and release guest references. Queue-allocation
failure keeps references pinned through synchronous fallback. Guest shader
release waits on a condition variable whose lifetime exceeds guest objects,
replacing polling/yield loops. Unrelated shader loads do not hold the shared
shader-map mutex across disk I/O or driver compilation.

True guest/Metal zero-copy remains unimplemented. MoltenVK 1.4.2 lists both host
and Metal external-memory extensions, but their existence is not proof that
this guest arena can safely be imported on an A11 device. The current arena
uses fixed guest addresses, page protection and page decommit; GPU imports would
need allocator ownership, alignment, mapping, CPU/GPU visibility and deferred
free handling tested together. `MTLStorageModeShared` alone does not replace that
protocol. External host import therefore stays disabled in this patch.

## Metal temporary ownership and allocator policy

The C++ render thread owns an iOS autorelease pool, with nested pools per frame,
ready GXM command list and startup shader. Background pipeline compiles, cached
shader warmups and GPU wait/callback requests each get their own pool. Vulkan
creation, late initialization and teardown also have local scopes. A small
Objective-C++ utility implements this using `NSAutoreleasePool`; only that file
uses manual reference counting. Pools cannot be copied/moved and drain on the
creating thread, including C++ exception unwinding. The existing frontend
`@autoreleasepool` scopes continue to cover its event loops.

Draining a pool releases temporary Objective-C ownership, not every Metal
object. Strongly owned or in-flight resources must remain alive. No guest RAM
pointer is bridged into a Metal object. `__bridge` is appropriate only for a
borrowed Objective-C pointer whose owner outlives the use; it cannot make an
asynchronous GPU use safe on its own. Retained bridging is an ownership transfer
that requires a balanced release, not inherently a leak.

On iOS VMA now prefers 8 MiB large-heap blocks (the pinned VMA default is 256 MiB).
This is a suballocation preference, not a maximum allocation or total heap cap;
large resources can still need larger/dedicated allocations. Internal VMA
synchronization remains enabled on iOS because an HLE queue lock does not prove
exclusive access to every allocator operation. No arbitrary 1.2 GB process cap
is claimed: heap limits exclude guest RAM, Metal driver allocations, JIT and UI,
and allocation failure would need coordinated recovery before enforcing one.

Descriptor sets remain reusable per fence-protected frame slot. They are not
reset while command buffers reference them. Texture retirement retains the
existing idle-time policy and GPU destruction delay; clearing after three frames
would both discard useful cache entries and require the same fence protocol.

Dynarmic already allocates a fixed code region per JIT instance. On a 3 GB device
the automatic budget is 8 MiB per guest thread; selectable budgets are smaller
than 128 MiB. The pinned ARM64 backend calls `ClearCache()` and rewinds the code
offset when full; it does not mmap another region for each recompile. The iOS
patch also clears block-range metadata. This is not a global 128 MiB limit across
all guest threads, nor an LRU block-eviction implementation. Changing linked JIT
block eviction needs invalidation/relink correctness work, not a raw ring buffer.

## MoltenVK graphics compatibility audit

On 2026-09-27 the upstream latest release is still
[MoltenVK 1.4.2](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2),
which the iOS workflow already pins with its archive SHA-256. Its release includes
SPIRV-Cross updates and fixes for color transfer channel corruption and primitive
restart state. No unversioned dependency update or custom binary is substituted.

Audited behavior in the pinned source:

- [Pixel format mapping](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKPixelFormats.mm)
  already provides D32Float+Stencil8 as the D24+Stencil8 substitute. Vita3K prefers
  supported Vulkan D32Float+Stencil8. On iOS it now fails initialization if neither
  stencil-capable format is supported, instead of silently losing stencil via D16.
- [Pipeline conversion](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm)
  chooses MSL version from device features and enables point size for point
  rendering. Hardcoding MSL 2.1 or enabling point-size output for every topology
  would bypass this logic.
- [Image views](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKImage.mm)
  already map component swizzles and select native versus shader swizzling.
  `fullImageViewSwizzle` is obsolete/ignored in 1.4.2; its redundant layer setting
  has been removed from Vita3K.
- [Queue submission configuration](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md#mvk_config_synchronous_queue_submits)
  uses `true` to encode on the submitting CPU thread, not to wait for GPU
  completion. iOS now sets this explicitly through both startup environment and
  instance layer settings, keeping encoding within the caller's pool scope.
  Fences and semaphores continue to govern GPU completion.

No `dynamicLines` configuration field is added: it is not an MVKConfiguration
member. Vulkan dynamic state must be supported, enabled and wired into pipeline
creation. Depth clipping and depth clamping are distinct controls; advertising
an extension or flipping Y again cannot generally fix missing geometry.

Portable tests compile the actual allocator settings against the pinned VMA and
Vulkan headers and exercise depth fallback, batch/worker pool boundaries, and
failure paths with recording adapters. The Objective-C++ ownership/unwind test
runs only on a Mac with Foundation. Visual correctness still needs captures of
specific failing games on A11; this patch does not certify all graphics or IPA
build success.

## FPS limit

Settings → Graphics & Display → Video offers 30 FPS, 60 FPS and Unlimited.
60 is the default; Unlimited is stored as zero. These options cap host
presentation requests without changing the Vita's 60 Hz vblank clock, CPU speed
or native game frame-rate limit. V-Sync and device presentation modes can still
limit Unlimited. The FPS overlay counts guest submissions, so it can differ
from the presentation cap.

Global values persist in `tsubomi.presentationFPS`; title overrides store
`presentationFPS` in their existing settings dictionary. A missing title field
inherits the global value. Invalid values fall back to 60. The selected limit
is applied after runtime initialization and before guest execution, and global
settings are restored after the session. Per-game changes apply at next launch.

The limiter keeps its pacing phase across small scheduling delays and avoids
catch-up bursts after a stall. A matching predicted frame no longer consumes a
presentation slot without requesting a display; a capped misprediction still
updates the latest image so it can be presented correctly later.

The 7e1f1d91 iOS build linked successfully in GitHub run 36320348804. Its test gate
failed on two Clang warnings in host fixtures. The pool adapters now exercise
their stored pointer, and VMA/Vulkan headers are marked as external system
includes in their fixture; `-Werror` remains enabled for our own test code.
