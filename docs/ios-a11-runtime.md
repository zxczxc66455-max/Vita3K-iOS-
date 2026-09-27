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
  minimum/maximum. The driver can return more; CPU frame slots remain unchanged.
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
