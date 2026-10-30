The October 20 efficiency pass supersedes the earlier Low-End preset. Worker counts, cache capacities, shader/pipeline warm-up coverage, resource residency, GPU memory placement, frame pacing and visual settings have been restored to their original policies. Setup no longer creates a separate launcher. No new resource caps or age-based eviction remain.

The retained implementation changes remove repeated work:

- Detile/retile prepares invariant Latte addressing once per transfer, uses constant-sized element copies, and validates full microtiles instead of recalculating every texel address twice. An exact bounds-check fallback preserves partial allocations and failure-before-write behavior. Thread counts are unchanged.
- Shader-module hits allocate no unused promise. Both existing FNV fingerprints are evaluated together, and a miss reuses its identity for disk lookup instead of hashing again. Existing SPIR-V filenames remain valid; concurrent requests still share one compilation.
- Pipeline-cache loading sizes its buffer once and reads the file in bulk instead of growing a vector through byte iterators.
- Normal window close now exports the driver pipeline cache before process termination. Valid `VK_INCOMPLETE` snapshots are retained, and failed writes do not replace a good cache. No per-frame export, GPU idle wait or worker join was added. Vulkan permits reloading these partial snapshots: [Khronos API contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html).

Release component benchmarks, median of seven repeats on this host:

| Same workload | Before | After |
|---|---:|---:|
| 1024x1024 RGBA8 detile, mode 4, three helpers | 3.801 ms | 1.511 ms |
| Same retile | 26.617 ms | 6.590 ms |
| 100,000 module hits, 128-byte source | 39.129 ms / 100,000 allocations | 13.694 ms / zero allocations |
| 2,000 module hits, 64 KiB source | 215.200 ms | 109.141 ms |
| Load and verify a 16 MiB pipeline-cache file | 59.768 ms / 42 allocations | 12.154 ms / one allocation |

The independent AddrLib comparison covers 680 tiling/depth/slice/element-size combinations, plus truncated and partial allocations. Tests also cover serial/parallel conversion, persistent cache filename/output compatibility, concurrent module requests, GPU texture/context reuse without reuploads, upload residency, queue ordering and compiler demand priority. Configure `-DRBREW_BUILD_TESTS=ON`; run CTest in `build/game` with Release. The final incremental Release rebuild succeeded and all 14 isolated regression tests passed. Component measurements and test logs are under `build/efficiency-*` and are intentionally untracked.

Earlier 65-second probes covered startup/loading only; screenshots did not establish plaza gameplay. The longer passive run reached the title screen and waited for input. These intervals provide no plaza FPS or 1% low result. The owner subsequently prohibited game launches: the active isolated probe was stopped, and all temporary recording/input hooks were removed. Further validation uses builds and Nintendo-free automated tests only. The plaza's original 30 FPS timing is unchanged.

An earlier normal-close probe confirmed a 55,455,507-byte driver pipeline-cache export; the baseline exported none. The owner's save hash was unchanged. Raw evidence is in `build/efficiency-live-20261020`.

A submit-mutex removal experiment was rejected: its handoff benchmark was slower. Full gameplay, level transitions, audio under load and actual low-end/integrated-GPU hardware remain unverified. Vulkan validation layers are unavailable locally.


The subsequent display-resolution update selects physical desktop pixels at launch (SDL desktop mode, per-monitor DPI awareness) and starts in borderless desktop fullscreen. Screen-sized TV color/depth attachments and their 1280x720 downsample chain allocate at the selected resolution; raster viewports/scissors, fragment-coordinate and texel-fetch scales, numeric depth transfers, aliases and scan encoding follow those physical sizes. Guest descriptors/CPU copies keep their original layout. Independent shadow maps, integer block encoders, asset textures and GamePad-sized resources retain their own dimensions. The game retains its existing camera/UI projection; this does not add alternate aspect-ratio/FOV logic. Resolution is selected at startup; moving a window or changing desktop modes during a running session does not recreate cached scene targets.

The new Nintendo-free GPU regression failed on the old allocation path, then passed at 960x540, 1024x768, 1280x720, 1366x768, 1920x1080, 2560x1440 and 3840x2160. It draws alternating physical-pixel rows/columns beyond the old console edges and verifies scissored clears, depth conversion, sampled target scales, GPU/CPU surface copies, array aliases and full-size sRGB scan-out. A window-free SDL query detected display 0 at 2560x1440 / 165 Hz and display 1 at 1920x1080 / 75 Hz. No game launch was used for this update. The plaza's 30 FPS timing is untouched.

Final display-resolution validation: incremental Release build succeeded; all 15 isolated CTest cases passed. `Game/Splatoon.exe` was updated from that build without launching it.

The October 21 native optimization pass caches stable host synchronization objects
per thread, specializes paired-single stores by quantization type, and resolves
shader/program/pipeline futures once. An exact byte-key front cache reuses the
last pipeline entry while retaining the complete pipeline map. MSVC Release
host-runtime LTCG is enabled after compiler-support and benchmark checks;
generated guest sources/objects keep their existing build policy. Floating-point
semantics, ordered guest writes/reservation observers, cache capacities, worker
counts, prewarming, render settings and timing policies are preserved.

Interleaved Release component benchmarks on the i7-12650H (medians of three
alternating runs, each reporting seven repetitions):

| Same workload | Before | After |
|---|---:|---:|
| 1,000,000 paired float stores | 17.603 ms | 8.331 ms |
| 1,000,000 quantized paired stores, u8/u16/s8/s16 | 19.908–21.721 ms | 12.472–14.316 ms |
| 200,000 independent guest lock/unlock pairs, four threads | 18.911 ms | 1.073 ms |

The production cache helpers' lookup model (1,000,000 lookups, 637-byte state keys,
16-draw batches, seven repetitions) measured 689.346 ms for the previous hash/future
path and 109.707 ms with exact-key/resolved-value reuse. These are component
measurements, not game FPS. Repeat detile and render-handoff measurements found
no material regression. Logs, baseline executables and the interleaved benchmark
runner are under `build/advanced-*` and `build/run_advanced_benchmarks.py`.

All 23 automated tests pass on both RTX 4050 and Intel UHD. Added coverage includes
58,240 quantized-store cases against an independent `ldexp` reference, strict
reservation invalidation, ordered write observers, recursive/contended locks,
condition-variable handoffs, cache collisions and object reinitialization.
Real Vulkan pipelines verify repeated-key reuse, distinct blend/topology state,
warm-up job reuse and returning to an earlier key. Existing tests retain texture,
display-resolution, upload-residency and shared-memory coverage. Validation layers
remain unavailable; stable 60 FPS and gameplay frame-time distributions have not
been established. These checks did not launch the game or load its saves.

The source comparison used [Cemu's pipeline compiler](https://github.com/cemu-project/Cemu/blob/a53cf0ece75964aa361056edab46f281dcc0bc44/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.cpp),
[Dolphin's quantized-store specialization](https://github.com/dolphin-emu/dolphin/blob/cf5cb7ef81c3e8e7af5c9d566334219ffda9f68b/Source/Core/Core/PowerPC/Jit64/Jit_LoadStorePaired.cpp),
[Dolphin's state tracker](https://github.com/dolphin-emu/dolphin/blob/cf5cb7ef81c3e8e7af5c9d566334219ffda9f68b/Source/Core/VideoBackends/Vulkan/StateTracker.cpp)
and [PCSX2's selective-LTO build policy](https://github.com/PCSX2/pcsx2/blob/1f4dd94db0633679e49bdec7faa0dd5246123ad0/cmake/BuildParameters.cmake).
Dolphin's Windows memory arena and block linker, plus PCSX2's fastmem mappings,
were also inspected. The port already has direct base-plus-address guest RAM,
native known-function calls, Windows write tracking, asynchronous compilation,
dynamic rendering, timeline submissions and mapped rings. Replacing those with
another MMU/JIT layer offers no identified saving. Extensions for graphics
pipeline libraries and descriptor indexing are advertised by both drivers,
but changing shader/resource binding architecture
needs workload evidence; it was not enabled speculatively. PGO was not trained
on synthetic workloads, and no process affinity, security or priority policy was
changed.

## Standalone cold shader pass, October 21

No game executable was executed for this pass. `gambit_shader_bench` links only
the host runtime and authored shaders/bytecode. It measures actual GX2 decoding,
GLSL emission, glslang compilation, real graphics pipelines, offscreen shader
pixels, draw recording, ring uploads, Vulkan allocations, worker scheduling and
native dispatch. It never loads title assets or calls a game entry point.

Implemented changes:

- Parse original GLSL once. The glslang C wrapper otherwise serializes expanded
  GLSL and preprocesses it again during parsing. Macro/version/extension handling
  and compiler checks remain enabled. Already initialized workers use an atomic
  fast path; disk hits do not initialize the compiler.
  File-include shaders retain the original preprocessing fallback.
- Append formatted shader text directly, with a `to_chars`/string fast path for
  plain single replacements and the original general formatter for other forms.
  Reserve decoded clause capacity once. Let transient text grow from 16 KiB
  instead of reserving 12 MiB per shader and shrinking immediately before copying.
  This changes scratch capacity, not cache size or shader limits.
- Assemble returned source directly into its final allocation. Avoid zeroing
  private bytecode/register storage immediately before overwriting every byte.
  A missing register snapshot still initializes all registers to zero.
- Scan geometry output declarations once instead of constructing/running regexes
  over every source. The original regex implementation remains a test oracle.
- Include the Cemu-derived translator in checked MSVC Release LTCG. Retain precise
  floating point and reuse all generated guest objects incrementally.
- Prepare the shader-cache directory once. Check cache file length before
  trusting its word count, and check completed writes before replacing a cache.

Seven alternating before/after process runs on this i7-12650H, using identical
inputs and unchanged worker counts, produced these median totals:

| Authored synthetic workload | Before | After |
|---|---:|---:|
| 60 uncached GLSL compilations, serial | 117.765 ms | 91.673 ms |
| Same 60 compilations, four workers | 32.230 ms | 20.675 ms |
| 60 requests into an empty SPIR-V disk cache | 156.480 ms | 108.476 ms |
| 240 GX2-to-GLSL translations | 10.104 ms | 5.245 ms |
| 160 rectangle geometry translations, 32 varyings | 26.644 ms | 3.564 ms |
| 50,000 tiny worker tasks | 43.561 ms | 43.143 ms |

Cold serial per-shader median/P95/P99 changed from 1.3443/2.5787/28.1774 ms to
0.8981/2.0471/26.9539 ms. Stage built-in initialization contributes the P99 outlier.
Process-first compiler initialization remained about 74 ms; it is not
claimed as a significant improvement. The disk corpus contains 58 unique sources
and two duplicates; both versions use identical deduplication. The cache-disabled
serial and parallel phases really compile all 60 requests. Warm disk hit rate is
100%; warm I/O fluctuated around 6–8 ms and is not claimed as a speedup. In the
final comparison it increased from 5.825 to 7.240 ms for 60 requests. A separate
same-process reader comparison isolates the small cost of validating actual file
length, retained to prevent corrupted headers from requesting enormous memory.
This is a startup-cache correctness tradeoff, not a memory-saving speed claim.

Translator-only peak process commit was 14,946,304 -> 2,326,528 bytes; resident
working set stayed about 9 MB. Most of that change is removal of the speculative
12 MiB transient reserve. Compiler-process peak memory remained about 59 MB.
No shader/pipeline cache, precompilation coverage, worker count, graphical
setting, memory budget or GPU memory placement was reduced.

Pipeline timings use a fresh **application** `VkPipelineCache`; vendor driver
caches are opaque and were not cleared. No driver-cold pipeline acceleration or
in-game FPS is claimed. Tiny draw-recording totals varied by microseconds between
the two benchmark binaries; these are not frame times. Driver heap usage/budget
sampling runs separately in `gpu_memory`, since sampling can perturb timing.
These are driver allocation estimates, not measured dedicated-VRAM residency;
an integrated GPU's local heap is shared RAM. `checkpoint_peak_bytes` records
owned allocation checkpoints, and process peak commit/working set come from
Windows. CPU time uses process accounting and has Windows accounting granularity.

Reusable commands, which execute only authored standalone tests:

```powershell
build/game/Release/gambit_shader_bench.exe compiler
build/game/Release/gambit_shader_bench.exe translation
build/game/Release/gambit_shader_bench.exe scheduling
build/game/Release/gambit_shader_bench.exe engine
build/game/Release/gambit_shader_bench.exe gpu
build/game/Release/gambit_shader_bench.exe gpu_memory
python recompiler/tests/run_shader_benchmarks.py --baseline build/shader-before/gambit_shader_bench.exe
```

The runner accepts only executables named `gambit_shader_bench.exe`, alternates
run order, preserves raw JSON samples, and compares output checksums. `--case`
selects a workload; `--gpu` selects either/both installed adapters or CPU-only
testing. Raw comparisons are in `build/shader-final-benchmark-results.json`.

Output checks include exact SPIR-V equality against the original two-pass
compiler, macro/line directive handling, invalid-source errors, geometry output
equality, integer/hex/string formatting, null-register snapshots, cache corruption
recovery, and concurrent queue consumers/exception/reference/move-only results.
Real offscreen GPU checks verify all 32 authored R700 pixel shaders and every
byte of a 16 MiB streamed upload. Other suites retain paired-store, detile,
texture/context reuse and shared/UMA memory fallback coverage.

The task-allocation/queue-layout rewrite was rejected after tiny-task throughput
regressed. The renderer already bypasses completed jobs, so adding a ready-result
shortcut to the queue itself was also dropped; its direct API benchmark was not
a representative renderer saving. A native Windows cache-reader experiment was
removed because it did not beat the existing buffered reader reliably. Source research compared
[Dolphin's single-parse compiler](https://github.com/dolphin-emu/dolphin/blob/cf5cb7ef81c3e8e7af5c9d566334219ffda9f68b/Source/Core/VideoCommon/Spirv.cpp),
[Cemu's shader compiler](https://github.com/cemu-project/Cemu/blob/a53cf0ece75964aa361056edab46f281dcc0bc44/src/Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.cpp)
and [PCSX2's Vulkan cache](https://github.com/PCSX2/pcsx2/blob/1f4dd94db0633679e49bdec7faa0dd5246123ad0/pcsx2/GS/Renderers/Vulkan/VKShaderCache.cpp).
Only applicable techniques were adapted; shader optimizers, additional pipeline
architectures and synthetic PGO were not enabled without supporting evidence.

The final incremental Release build succeeded with all generated guest objects
reused. All 29 CTest cases pass on both RTX 4050 and Intel UHD. Final test logs and
driver heap samples are in `build/shader-final-{nvidia,intel}-test-details.log`;
`build/shader-verification.json` records source hashes and the delivered binary.
`Game/Splatoon.exe` SHA-256 is
`264426d155880d92bd655b56f737f685c8af07f25427f7563a7f4a9965a35dd4`.
No game launch or actual in-game FPS, physics, gameplay or audio validation was
performed.

## Rendering follow-up (2026-10-21)

The ordinary-descriptor fallback now caches exact resource bindings within each
submission, streams uniform offsets through dynamic UBO descriptors up to the
device's advertised limit, and resets retired pools only after GPU completion.
Push descriptors retain their supported descriptor types. Index bindings are
shadowed, immutable vertex semantics are collected once, identical sampler
parameters bypass repeated normalization, and raster validation is repeated
inside a draw only when command state was invalidated. Idle completion queries
and the unused pre-submission completion poll were removed. Shader prewarming,
worker counts, cache capacities, rendering quality and memory policy are unchanged.

`gambit_render_bench` links only the runtime and authored shaders. It submits
16,384 indexed draws with texture changes, four pipeline variants, uniform
changes, scissor changes and two render targets, then checks 4,096 pixels.
Seven alternating baseline/candidate runs produced these median total times:

| Workload | RTX 4050 before → after | Intel UHD before → after |
| --- | --- | --- |
| Reused fallback descriptors | 17.124 → 12.783 ms | 22.470 → 15.550 ms |
| Push descriptors | 15.767 → 11.572 ms | 17.062 → 13.003 ms |
| Changing uniforms with fallback descriptors | 17.861 → 13.998 ms | 23.982 → 16.627 ms |

These are synthetic CPU recording/submission/completion times, not frame times
or measured game FPS. Descriptor updates fall from 16,384 to 512 in the fallback
workloads and index binds from 16,384 to 16; draw and submission counts and
barriers remain equal. `build/render-benchmark-results.json` retains every run.
`recompiler/tests/run_render_benchmarks.py` repeats the comparison and accepts
only the standalone benchmark executable name.

The separate `contracts` case checks dynamic-offset binding order, cache keys,
pool-exhaustion recovery, bounded device-OOM failure, sampler identity and state
invalidation. `lifetime` holds 16 submissions behind a host-signalled timeline
semaphore while replacing/uploading textures; it proves that live pools are
retained and reused after completion, with correct output. These cases also run
on both GPUs. The cache-reader experiment was rejected: controlled reads improved
but total warm-cache requests did not reliably improve, so integrity checks and
the existing reader are retained.

Source comparisons used [Cemu's dynamic uniform descriptors](https://github.com/cemu-project/Cemu/blob/a53cf0ece75964aa361056edab46f281dcc0bc44/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRendererCore.cpp),
[Dolphin's state invalidation](https://github.com/dolphin-emu/dolphin/blob/cf5cb7ef81c3e8e7af5c9d566334219ffda9f68b/Source/Core/VideoBackends/Vulkan/StateTracker.cpp)
and [PCSX2's Vulkan state tracking](https://github.com/PCSX2/pcsx2/blob/1f4dd94db0633679e49bdec7faa0dd5246123ad0/pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp).
No game executable was launched.

The incremental Release build and all 34 tests passed on each adapter, with all
five new rendering cases also passing with serial command recording. The built
binary was copied to `Game/Splatoon.exe` and its hash verified against the build.
`build/render-verification.json` records the binary/source hashes and final logs.

## Texture and uniform follow-up (2026-10-22)

Macro-tiled decoding and retiling now derive the eight-by-eight pixel address
offsets once and calculate bank/pipe/tile addresses once per eight texels. Bounds
checks, partial rows, depth and thick-tile addressing remain intact. Textures
narrower than eight texels retain their scalar path. Texture levels decode into
the existing mapped upload ring, avoiding the temporary linear allocation,
zero-initialization and full-image staging memcpy. Missing mips and failed
decodes retain their established fallback behavior. Uniform-register updates
convert directly into native state using baseline SSE2 with a scalar fallback,
removing the temporary vector and second copy.

Seven alternating runs of distinct, explicitly rebuilt baseline and candidate
binaries measured 16-upload bursts through the actual texture-binding path:

| 1024×1024 RGBA8, 16 uploads | RTX 4050 before → after | Intel UHD before → after |
| --- | --- | --- |
| Linear | 37.058 → 13.573 ms | 39.773 → 16.275 ms |
| Macro tile 4 | 58.689 → 14.877 ms | 56.839 → 17.273 ms |
| Thick tile 7 | 84.057 → 35.963 ms | 80.757 → 43.177 ms |

Across the ten tested sizes/layouts, burst time fell 26–75% on RTX and 14–72%
on Intel. These totals include hashing, CPU conversion, command recording,
submission and final completion. They are not GPU timestamps or game frame
times. Uniform API tests (100,000 updates, 16–1,024 words) take 85–93% less time;
a volatile function pointer prevents IPO from eliminating repeated writes.
Raw runs, P95/P99 recording latency, memory samples and executable hashes are
in `build/next-texture-results.json`. The initial comparison accidentally reused
identical binaries due to preserved source timestamps and was discarded; the
runner now rejects identical hashes.

Separate CPU tests of 1024×1024 RGBA8 macro layouts 4, 8 and 12 show 53–62%
less detile time with the existing serial and three-helper configurations, and
69–74% less retile time. Three alternating runs, each using seven internal
timing samples, are retained in `build/next-detile-results.json`.

The standalone `gambit_texture_bench` adds 1,452 independent AddrLib layout cases,
unaligned uniform conversion, asynchronous texture bursts and byte-exact GPU
checks of compressed mips, selected array slices, cube faces, 3D mips, absent
mips, short-source fallback and zero image-size descriptors. Existing
16,384-draw and resource-lifetime tests
remain enabled. Worker counts, precompilation, caches, resolution, Vulkan memory
type ranking, and required barriers are unchanged.

Engineering references: [Cemu's staging reservations and flushes](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.cpp),
[Dolphin's upload stream](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/Vulkan/VKTexture.cpp),
and [PCSX2's directly mapped texture upload path](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/Vulkan/GSTextureVK.cpp).
No game executable is used by these benchmarks.

All 38 tests passed on both RTX 4050 and Intel UHD; all nine rendering/streaming
cases also passed on each adapter with serial command recording. The full
Release rebuild and final incremental build succeeded. The incremental build
reused all 193 generated guest-code objects. The delivered `Game/Splatoon.exe`
matches the built executable; `build/next-verification.json` records its hash,
source hashes and validation logs. One full-build recursion warning matches a
linked self-call verified statically in the original RPX; its behavior was
preserved. The actual game was never launched, so in-game FPS and gameplay,
physics, graphics and audio correctness remain unmeasured.

## Populated-scene follow-up (2026-10-22)

Large, unchanged geometry ranges reuse their exact write generation within the
existing render-batch epoch. Renderer stores and collections by other threads
invalidate those answers conservatively; outside-batch callers still collect
Windows write watch normally. Small uniform ranges retain their direct scan.
Vertex-fetch plans retain the decoded format/location mapping while checking
fetch-program revisions and the complete semantic-import register list. Stream
availability, strides, write generations, buffer lifetime and instance offsets
remain live on each draw.

Equivalent Vulkan shader interfaces now share descriptor and pipeline layouts.
The exact key includes descriptor type/count, shader visibility, immutable
samplers and push-layout flags. Compiler workers synchronize only during layout
lookup/creation; draws acquire no new lock. Shared ownership and the device
cache retain layouts through shader compilation, submitted commands and final
renderer shutdown. This also permits descriptor reuse across compatible material
programs, preserving draw order.

The authored `heavy`, `heavy_push` and `overdraw` workloads use 1,024 objects,
2,304 indexed draws per simulated frame, four geometry streams, 64 material
textures, eight color programs, a shadow program, changing pose uniforms,
render-target changes and repeated alpha blending. Twelve frames are queued
without per-frame GPU waits. GPU timestamps bracket rendering; a separate
readback history checks every submitted frame, followed by 2,048 object/depth
samples. The overdraw variant doubles the vertex count per draw and increases
fragment integer work. These are workload proxies, not captured Plaza frames.

Standalone contracts cover overlapping memory ranges, producer writes,
cross-thread collection, changed imports/strides/fetch programs, missing
streams, malformed formats, instance divisors, layout compatibility and
concurrent compiler creation. `run_heavy_benchmarks.py` alternates distinct
baseline/candidate binaries, verifies output and identical workload counts,
and records CPU/GPU median, P95/P99, throughput and memory samples separately.

Engineering references: [Cemu's minimal state and vertex-layout separation](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRendererCore.cpp),
[Dolphin's cached vertex loaders](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexLoaderManager.cpp),
and [PCSX2's Vulkan state tracking](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp).
Scene-level culling, sorting and indirect batching were not guessed from GX2
draws; the translator lacks the visibility and ordering proofs those require.

Command recording also publishes each draw's existing packets as one batch,
reducing cross-thread publication and wakeups. Nested scopes, direct operations
and queue-capacity pressure still publish/drain in order. A 500,001-command
fixture forces ring wrap, publication during an open batch and submission
inside nested scopes. Draw order, Vulkan commands and GPU submission policy
are unchanged. Legacy/synchronization2 timestamps and query resets now use the
same recording queue, allowing valid GPU timings with split recording enabled.
Initial timestamp probes bypassed that queue and were discarded after Vulkan
validation identified the test-harness race. Corrected baseline and candidate
runs are checked with Khronos synchronization validation.

## Plaza-scale draw path (2026-10-22)

The earlier heavy proxies call the draw helper directly, so they skip guest
program-key resolution and guest texture binds. The new authored `plaza` case
goes through `gx2_render_draw_indexed`: 1,664 draws per frame (shadow, opaque,
blended), 32 register-selected R700 program pairs of 1.5–3.3 KiB, alpha-test and
far-clip register changes, big-endian guest indices, per-draw uniforms and four
`gx2_render_bind_texture` calls per colour draw over 64 tile-mode-4 textures. The
owner's shader cache holds about 49,800 exact keys for about 2,400 SPIR-V modules,
so program and state combinations vary heavily between draws.

Changes (draw order, workload, shaders, caches and visuals are unchanged):

- The exact-key memo keeps 256 slots chosen by a hash of all key inputs and
  checks the previous draw's slot first. A hit still compares every input.
  Bytecode is compared byte for byte unless the page write generations
  (already trusted for index, vertex and uniform reuse) prove no write since
  the bytes were last compared. Untracked memory always compares.
- The SPI_PS_INPUT_CNTL clear no longer searches the register table per word
  (8% of main-thread time), key registers use one flat gather, and the legacy
  key's texture words are built only when that key is used.
- Per-program masks of live uniform banks and texture slots; sampler compare
  state is touched only when it differs.
- Pipeline and descriptor-set maps hash with the renderer's KeyHash instead of
  MSVC's byte-serial FNV; full-key equality is unchanged.
- An exact descriptor-bytes index maps a bind to its `tex_map` entry, using the
  per-slot cache's rule for every slot, instead of rebuilding the 33-word key
  and walking the tree.

Seven alternating runs of distinct baseline/candidate executables, identical
workloads and output checksums (`build/plaza-benchmark-results.json`):

| Synthetic CPU time per frame | RTX 4050 before → after | Intel UHD before → after |
| --- | --- | --- |
| `plaza` median | 3.920 → 1.345 ms | 4.117 → 1.523 ms |
| `plaza` P99 | 4.231 → 1.584 ms | 4.366 → 1.727 ms |
| `heavy` median | 3.052 → 2.225 ms | 3.753 → 2.893 ms |
| `heavy_push` median | 2.556 → 1.923 ms | 2.607 → 2.288 ms |
| `overdraw` median | 3.358 → 2.398 ms | 3.614 → 2.786 ms |

`plaza` throughput rose from 409k to 1.14M draws/s (RTX) and from 382k to 961k
draws/s (Intel). In `heavy`/`overdraw` the GPU time dominates (RTX 4.3/13.5 ms,
Intel 10.6/38.8 ms) and did not change, so their wall time is unchanged. These
are CPU translation-layer measurements, not game FPS. A cached SPSC ring tail was
reverted: it was not measurably faster.

`gambit_render_profile` (MSVC) is the same benchmark with an opt-in sampler:
`RBREW_BENCH_SAMPLE=1`, `RBREW_BENCH_SAMPLE_LINES=<function>`,
`RBREW_BENCH_SAMPLE_CALLERS=1`, `RBREW_BENCH_FRAMES=<n>`. Configure
`-DRBREW_GX2_DEBUG_INFO=ON` for renderer function and line names. All 45 tests
passed on both adapters, the render/streaming suite passed with serial recording,
and `render_plaza`, `heavy`, `heavy_push`, `contracts`, `lifetime` and `geometry`
passed Khronos synchronization validation on both. The game was not launched.

### Second draw-path pass and FPS overlay (2026-10-22)

Further changes, with draw order, workload and visuals unchanged:

- Closed draw batches are published to the recording thread in 16 KiB chunks
  rather than per draw. `drain()` still publishes everything before each
  submission, direct command or wait, so recorded commands are unchanged.
- The descriptor-set key is built in a stack buffer and looked up through a
  transparent `string_view` hash; only a new set's key is copied into the map.
- Vertex setup visits only the fetch plan's live streams, and the pipeline key
  buffer is no longer zero-filled before each draw.

Seven alternating runs against the original baseline executable, with
identical workloads and checksums (`build/plaza2-benchmark-results.json`):

| Synthetic CPU time per frame | RTX 4050 before → after | Intel UHD before → after |
| --- | --- | --- |
| `plaza` median / P99 | 3.847 → 1.194 / 4.116 → 1.434 ms | 4.080 → 1.287 / 4.884 → 1.610 ms |
| `heavy` median | 3.067 → 1.911 ms | 3.728 → 2.613 ms |
| `heavy_push` median | 2.578 → 1.698 ms | 2.514 → 2.131 ms |
| `overdraw` median | 3.301 → 2.056 ms | 3.484 → 2.534 ms |

`plaza` reaches 1.25M draws/s on the RTX 4050 and 943k on the Intel UHD. GPU time
in `heavy`/`overdraw` is unchanged, so their wall time is still GPU-bound. These
are CPU translation-layer measurements, not game FPS.

**FPS overlay.** Every presented frame shows FPS, average frame time and the
slowest frame interval of the last half second. These are measured at actual
swapchain presentation. The text is rasterised on the CPU with a 5×7 font and
copied into the top-left corner after the frame blit. It needs no shaders or
render passes and costs one small buffer-to-image copy per frame. Text is
scaled with window height. The overlay starts hidden, **F3** toggles it, and
`RBREW_FPS_OVERLAY=1` starts it visible. The settings menu (Esc) can also show
a simple FPS / frame-time / max-FPS readout (`simple_overlay` in settings.ini,
`RBREW_SIMPLE_OVERLAY=1`), which draws without window chrome and takes no
input. Captured presents used by image
tests omit it. `render_overlay` checks text, statistics and glyph
rasterisation. `render_overlay_present` presents through a real hidden Vulkan
window on each adapter and reads back the swapchain image
(`docs/fps-overlay-example.png`). All 47 tests passed on both adapters, render
cases also passed with serial recording, and the validated render cases
(including `overlay_present`) passed synchronization validation. The game was
not launched.

## Character (NPC) pass (2026-10-23)

No game executable was run. Characters (Plaza NPCs, Octarians, other
skinned models) reach the runtime in two places: nn::g3d's per-bone math runs
as recompiled paired-single code, and every shape draw binds per-character
uniform blocks (matrix palette, shape block) plus shared material and scene
blocks through `GX2Set*UniformBlock`. Skinning itself runs in the vertex
shader; no CPU-side vertex transformation was found to remove. The earlier
benches used uniform registers only, so the block path was unmeasured.

The new `npc_<1|10|25|50|100>` and `npc_fallback_50` cases model that: 64-bone
palettes and eight 64-byte shape blocks per character, rewritten by the CPU
every frame. Real four-bone skinning in the vertex shader. Shadow casters are
drawn character by character, the colour pass material by material, so
characters interleave. Every shape owns a tile whose colour encodes the exact
palette, shape, material and scene words the GPU read. All tiles plus shadow
depth are checked exactly after the final frame. Contracts check a shorter
block at a cached address (distinct upload, zero padding) and in-place palette
writes. These are workload proxies, not captured Plaza frames.

Changes (draw order, workload, shaders, caches and visuals are unchanged):

- **Uniform-block residency by guest range.** Tracked blocks are cached by
  guest address, size and padded size, like index spans, instead of by
  stage/bank slot. The slot cache re-copied a palette every time draws
  switched characters or passes, twice (guest → vector → ring). Unchanged
  ranges now reuse their ring span; changed ones are copied once, straight
  into the ring. A per-slot memo skips the lookup when the same range is
  bound again in the same recording. Untracked memory keeps the old
  compare-bytes path.
- **Batched write-watch collection.** A render batch collects the aligned
  64 KiB window around an uncollected range in one `GetWriteWatch` call.
  Neighbouring palettes and shape blocks were each costing a reset query;
  results enter the same page history, and outside a batch nothing changes.
- **Exact-key memo fix (correctness).** `ExactKeyMemo` trusted
  "generation ≤ proven" without checking that the proven generation
  belonged to the same guest range. Pages collected by one query share a
  generation, so a different program of the same size and register inputs
  could reuse the previous draw's key: the wrong shader. The original code
  fails the new plaza contract (pairs 0/24 after a write); it now skips the
  byte comparison only for the proven range.
- Stream-out state (eight buffer records) is built only for draws that enable
  stream-out. The guest-range cache hashes use a full 64-bit finalizer;
  fixed-stride blocks shared low bits, and therefore buckets.

Seven alternating runs of distinct baseline/candidate executables, identical
workloads, identical output checksums (`build/npc-benchmark-results.json`).
CPU time per synthetic frame (median), uniform uploads and write-watch OS
queries per frame:

| Case | RTX 4050 before → after | Intel UHD before → after | Uploads | OS queries |
| --- | --- | --- | --- | --- |
| `npc_10` | 0.166 → 0.107 ms | 0.168 → 0.133 ms | 262 → 100 | 19 → 4 |
| `npc_25` | 0.380 → 0.242 ms | 0.372 → 0.273 ms | 637 → 235 | 32 → 13 |
| `npc_50` | 0.760 → 0.468 ms | 0.673 → 0.523 ms | 1,262 → 460 | 54 → 9 |
| `npc_100` | 1.530 → 0.954 ms (P99 8.40 → 1.23) | 1.327 → 1.120 ms (P99 3.85 → 2.27) | 2,512 → 910 | 98 → 10 |
| `npc_fallback_50` | 0.752 → 0.483 ms | 0.669 → 0.526 ms | 1,262 → 460 | 54 → 9 |
| `plaza` | 1.215 → 1.129 ms | 1.261 → 1.161 ms | – | – |
| `heavy` / `heavy_push` | 1.699 → 1.671 / 1.772 → 1.765 ms | 2.796 → 2.762 / 2.394 → 2.476 ms | – | – |

`heavy`/`heavy_push` use no uniform blocks and are within noise. The Intel P95
of several mid-size NPC cases varied in both directions between runs. GPU time
is unchanged; the NPC cases are GPU-bound in wall time. The baseline
executable was built from the original renderer with these bench sources;
its `plaza` uses the original bench file, since the original code fails the
new memo contract. These are CPU translation-layer measurements, not game FPS.

All 56 CTest cases pass on both RTX 4050 and Intel UHD; the NPC, plaza,
contracts, lifetime and heavy render cases also pass with serial recording.
Engineering references: Cemu keys uniform/buffer data by guest range
([LatteBufferCache](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/HW/Latte/Core/LatteBufferCache.cpp)).
Not done: GPU instancing and cross-character batching. GX2 draws carry no
proof that two characters' draws are reorderable or share state, so the
original order is kept.

## Guest-code memory helpers for character animation (2026-10-23)

No game executable was run. Character animation (nn::g3d curve sampling,
quaternion/matrix math, palette writes) and all other guest logic run as
recompiled C++ that calls a runtime helper for every guest load and store.
In the shipped objects those helpers were out of line in large functions:
a sampled part (`Gambit_part0100.obj`) made 1,771 calls to `rbrew_write32`
and 681 to `rbrew_read32`, each calling `rbrew_memory_accessible` and, for
stores, `rbrew_write_memory` in turn.

Changes (no forced inlining; compile time per part unchanged, 6.6/10.4/7.5 s
vs 7.5/13.7/6.6 s for parts 0100/0183/0010, objects within ±5%):

- **Smaller load/store helpers** (`rebrewu_runtime.h`). The helper is the
  application-arena test plus the access; any other address calls a
  non-inlined `*_checked` function holding the original logic, including
  out-of-bounds behaviour. The compiler now inlines them itself: the same part
  has ~0 hot-path `rbrew_read32` calls and 221 `rbrew_write32` calls.
- **Plain stores copy directly.** A thread-local flag (kept by
  `rbrew_set_write_observer`) and the strict-reservation flag select
  `rbrew_write_memory` exactly when it was needed (observer or strict mode).
  `/Zc:tlsGuards-` on the `Gambit` target removes the per-access TLS guard;
  the flag is constant-initialized static TLS.
- **`cntlzw`** uses `_BitScanReverse` on MSVC instead of a bit loop;
  byte swaps use the compiler intrinsics.
- **Dispatch fast path** (`rebrewu_runtime.cpp`): virtual calls go straight to
  a known target when the entry trace / import profiler are off (always in
  the shipped game): 3.0 → 2.1 ns per call.

`gambit_guest_bench` (new) runs authored guest skeletal animation in the form
RebrewU emits: 96 bones, 10 Hermite curves per bone through virtual calls,
paired-single transform and concatenation, 64-bone byte-reversed palette.
Three alternating runs against a baseline built from the original runtime,
identical palette and state checksums, zero allocations:

| Characters | Before (median) | After (median) |
| --- | --- | --- |
| 1 | 0.14 ms | 0.12 ms |
| 10 | 1.48–1.52 ms | 1.23–1.30 ms |
| 25 | 3.81–4.01 ms | 3.09–3.26 ms |
| 50 | 7.57–8.56 ms | 6.18–6.32 ms |
| 100 | 15.46–16.47 ms (P99 17.5–32.1) | 12.40–12.91 ms (P99 14.3–15.4) |

Curve sampling (scalar FP) went from 53 to 37 ns per call. Paired-single math
is unchanged: `rbrew_ps` stays out of line; faster paths inside it were
measured and reverted (layout noise exceeded any gain). The NPC render bench
(`npc_100`) is GPU-bound (3.2 ms GPU vs 1.8 ms CPU per synthetic frame); its
CPU translation is ~1.1 µs per draw. These are synthetic measurements, not
game FPS. 60/60 CTest cases pass on RTX 4050; render, advanced and guest
cases (31) pass on Intel UHD.

## Game/render-thread serialization (2026-10-24)

A 12-second CPU profile of the tutorial (RTX 3060 host, 49 FPS average,
19.9 ms game-thread work per frame), symbolized with `build/game/Splatoon.map`
(`scripts/analyze_playtest.py` does this automatically):

| Thread | Share of samples | Where |
|---|---:|---|
| Game thread | 49% | waiting in `GX2QueryGetOcclusionResult` for the render queue to drain |
| Game thread | 3% | `GX2DrawDone` |
| Render thread | 13% | `flush_streamouts` waiting for the GPU (`gx2vk::finish`) before the next draw |
| Render thread | 42% | idle (spin/sleep) |

Splatoon keeps its occlusion queries in a ring and polls the oldest one each
frame (`027E99AC`); a FALSE result simply retries next frame, as on the console
where `GX2QueryGetOcclusionResult` reads the counter block without blocking.
The port waited whenever that query's begin was still queued, which made the
game thread wait for every queued draw, many times per frame.

Changes:

- A read whose begin is still queued now returns "not ready" immediately
  (later reverted, see the follow-up below). If the queue is idle but the
  renderer dropped the begin, the synchronous path still answers.
- Stream-out results are copied back only when a later draw reads an
  overlapping vertex, index, uniform or texture range, at `GX2Flush` /
  `GX2DrawDone`, or when the 32-entry batch is full (`RBREW_STREAMOUT_EAGER=1`
  restores the per-draw readback). Offsets that a newer export or a new
  `GX2SetStreamOutContext` already replaced are not overwritten by the late
  publication.

Expected effect: the game thread's ~10 ms of own work per frame can overlap the
render thread again. This must be confirmed with a new recording
(`scripts\dev.bat play`, then `scripts\dev.bat analyze`).

### Follow-up (2026-10-24, first recording after the fixes)

Measured (tutorial, `2026-10-24_13-17-40`): 71.4 FPS average (was 49), P95
23.8 ms, 1% low 13.6 FPS. But the tutorial prompts disappeared, ink and physics
misbehaved, and the game ran at 90-100 FPS in menus and gameplay.

- Occlusion queries: the immediate "not ready" broke UI, ink and physics (A/B:
  correct with the old wait), so waiting became the default again. With the
  wait the game still ran above 60 FPS. (Refined in the next section.)
- Frame pacing: `GX2SetSwapInterval` was a stub and a swap flipped as soon as
  it was presented, so the game, which paces on the flip counter, ran
  unthrottled and its logic and camera sped up. A swap now flips at the first
  vblank `interval` vblanks after the previous flip; a late frame flips at once
  (no drop to 30 FPS). `RBREW_FRAME_PACING=0` restores the immediate flip.
- Uniform cache (from the GitHub merge): `RBREW_UNIFORM_CACHE=0` compares guest
  bytes on every bind instead of trusting write-watch generations, and
  `RBREW_WRITE_WATCH_DISABLE=1` (slow) now also works in release builds, for
  A/B tests of the write-watch caches.

### Boot time, scene-change flashes, camera, overlay (2026-10-24, second recording)

Measured (tutorial, `2026-10-24_13-55-07`, with the fixes above): 57.0 FPS
average, P95 20.5 ms, 1% low 24.7 FPS, 74% of frames <= 17.5 ms; title and
character setup a steady 59.7 FPS. 53% of the game thread still waited in
`GX2QueryGetOcclusionResult`.

- Boot loading screen ~28 s. A loading-screen profile showed every thread
  idle; `RBREW_IMPORT_TRACE` / `RBREW_SLEEP_TRACE` (new, release builds too)
  showed `nn::ac::Connect` succeeding while `IsApplicationConnected` said
  offline, after which the game's NEX/Pia login (`AcquireNexServiceToken`,
  scheduler threads polling every 10 ms) ran until its 30 s timeout
  (`OSCancelAlarms` at 36.4 s). `Connect` now fails like a console without
  internet and `AcquireNexServiceToken` fails for the offline account: the
  character setup appears ~5 s after the first frame instead of ~33 s.
  `RBREW_AC_CONNECT=ok` / `RBREW_NEX_TOKEN=stub` restore the old results.
- Black flashes on scene changes: the presented "scan buffer" was the game's
  colour buffer itself, so a frame without a new scan copy showed whatever the
  game was clearing/drawing. The scan copy is now a real copy, as on the
  console (`RBREW_SCAN_COPY=0` restores the reference).
- Camera too fast horizontally: the right stick turned the camera through the
  game and again by yawing the virtual GamePad. Stick X no longer yaws the
  virtual pad (`RBREW_STICK_GYRO_YAW=1` restores it); stick Y still pitches it.
- Occlusion queries, refined: Gambit's only caller (`027E99AC`) polls a ring
  and retries FALSE next frame. The first attempt also answered "not ready" for
  queries that were ever begun from a display list (sticky `listed` flag, no
  issue count) whenever the queue was busy, which stalled the ring. Now only
  immediate begins answer "not ready"; listed ones still wait.
  `RBREW_QUERY_ASYNC=0` waits for all, `=1` is the broken first attempt.
- FPS overlay: an anti-aliased system-font panel (FPS in traffic-light colours,
  frame time, 1% low, max, frame-time graph with a 16.7 ms line) replaces the
  5x7 text line (`RBREW_FPS_OVERLAY_STYLE=classic`).

To be confirmed with the next recording.

### Button labels, GamePad screen, stream-out (2026-10-24, third recording)

Measured (`2026-10-24_14-31-38`): title/character setup 59.0 FPS (only 579
frames: the boot fix works), tutorial 55.3 FPS (P95 22.1 ms, 60% <= 17.5 ms),
first plaza scene 34.9 FPS. Tutorial prompts, ink and physics were correct
with the refined query mode, but 50% of the game thread still waited for
occlusion queries: nearly all of Gambit's queries are begun from display
lists, and the render thread spent 22% publishing stream-out data on each
`GX2Flush` (a GPU wait the query reads then waited for).

- Missing "Girl"/"Boy"/"Yeah!"/"Hold on!" on the character-setup splats
  (`Plm_CharacterBtn_00`): the game renders the label into a 250x150 RGBA8
  capture, encodes it to BC5 on the GPU (63x38 R32G32B32A32 target over the
  texture's memory) and samples that. The text pass blends alpha with
  COMB_FCN 3 (MAX); the port mapped GX2 blend combine 2/3/4 to
  reverse-subtract/MIN/MAX instead of MIN/MAX/reverse-subtract, so alpha
  stayed 0 and the encoder produced empty blocks. Fixed for every blend
  (`RBREW_BLEND_COMBINE_OLD=1`). Also: the block copy into a non-multiple-of-4
  BC texture now goes through a buffer (an image copy of whole blocks overran
  the image; `RBREW_BLOCK_COPY_DIRECT=1`), and CB_BLEND0_CONTROL /
  CB_COLOR_CONTROL written by display lists are applied (`RBREW_DL_BLEND=0`).
- GamePad (DRC) screen never reached the scan buffer: the game passes its DRC
  buffer with viewNumSlices 0 but renders it with 1, so the exact lookup always
  missed (`[scan] missing source target=4`). 0 and 1 slices now match
  (`RBREW_SCAN_SLICE_MATCH=0`). This affects the GamePad view (TAB), the map
  and GamePad pictures.
- `GX2Flush` no longer publishes pending stream-out results; draws that read
  them and `GX2DrawDone` still do (`RBREW_STREAMOUT_FLUSH_PUBLISH=1`).
- The black screen with the swimming squid after character setup is the game's
  own loading screen (~4 s).

### Plaza pacing, tutorial transition, audio filters (2026-10-24, fourth recording)

Measured (`2026-10-24_17-22-39`): character setup 59.5 FPS; tutorial 58.7 FPS
(P95 18.5 ms, 91% <= 17.5 ms; was 55.3 / 60%); plaza (scene 2) 35.7 FPS with
P50 26.1 ms, P95 40.8 ms. Occlusion-query waits fell to 28% of the game thread.

- Plaza: the console runs it at 30 FPS because a frame that misses a vblank
  flips at the next one. The first pacing flipped late frames at once, so
  ~26 ms frames alternated 26/33 ms (35.7 FPS average, uneven). Flips are now
  vblank-quantized like the console's (`RBREW_FRAME_PACING=adaptive` restores
  the first version). Gambit sets the swap interval only at boot (02818190).
- Tutorial entry: the user's video (previous build) showed ~4 frames of the
  half-loaded stage before the squid wipe; the black cover is a MIN blend,
  which the old blend-combine mapping ran as dst-src (fixed above).
- Audio: `AXSetVoiceLpf`/`LpfCoefs`/`Biquad`/`BiquadCoefs`/`AXComputeLpfCoefs`
  were STUBs; voice filters are now applied after sample-rate conversion
  (biquad drops itself if it turns unstable). `RBREW_AX_FILTERS=0` ignores them.
- The playtest CPU profile is now taken in the plaza (scene 2) after a minute,
  else in scenes 3/5/6 after three minutes.

### Story level: input replay, asynchronous shaders (2026-10-25)

Measured with an input recording of the user's own run (title, plaza, Octo
Valley, mission 2 "Lair of the Octoballs") replayed by `RBREW_INPUT_REPLAY`.

- Input recording/replay: `RBREW_INPUT_RECORD=<file>` stores every frame's
  pad (buttons, sticks, touch, controller gyro, motion step) plus marks for
  scene changes and for the end of each loading screen (the frame's guest draw
  count rises from 8-36 to thousands). The replay waits at each mark until the
  game catches up, so load times do not shift inputs. `RBREW_SAVE_DIR` keeps
  the replay on a copy of the recorded save. In a level the run diverges after
  a while (enemy behaviour), which is fine for profiling.
- Asynchronous shaders (`RBREW_ASYNC_SHADERS=0` restores waiting): a draw whose
  program or pipeline is still compiling is skipped instead of stalling the
  render thread, and the game thread behind it, for the whole compile. Only
  draws into a render target already drawn before the last present qualify, so
  one-time renders (cubemaps, captures) still wait; stream-out draws and draws
  inside an occlusion query also wait. Level with this level's shaders removed
  from the cache (same replay, 48 s): hitch time 4.84 s -> 2.48 s, frames over
  50 ms 46 -> 27, longest 1287 -> 545 ms, average 49.8 -> 52.4 FPS. With a
  warm cache there is no difference (56.7 vs 56.5 FPS, 7 frames over 50 ms in
  27 s).
- Tried and dropped: counting occlusion-query begins inside display lists so
  listed reads need not drain the render queue. Gambit reads one listed query
  per frame in the frame that began it; answering "not ready" or the previous
  result removed the whole 2D layer (HUD, text box) for single frames
  (18 of 146 sampled frames), and even without any query wait the level ran at
  the same ~55-56 FPS: the wait only moved. The per-frame read acts as the
  game's fence against reusing buffers the renderer has not read yet.
- Remaining limit in the level: game thread ~7 ms and render thread ~8 ms of
  work per frame, serialized at that fence; average 56-57 FPS, median 16.7 ms,
  ~40% of frames between 17 and 26 ms. The FPS overlay no longer rebuilds its
  background every frame (it was 3.6% of the render thread).

### Plaza CPU decoding and query safety (2026-10-25)

The profiled executable (`11420A84...`) averaged 31.98 FPS over 1,532 loaded
plaza frames (scene 2, frame > 1400, draws >= 1000), with 0% <= 17.5 ms and
p99 47.18 ms. This is actual replayed plaza gameplay, not a menu measurement.
The CPU profile includes 11.3% of the main thread in paired-single helpers.

- Generated constant instruction words now use `rbrew_ps_constant<word>` for
  float loads/stores and common paired arithmetic. Quantized and other forms
  retain the interpreter; memory checks, write observers, ordered stores,
  register aliases and single-precision rounding are preserved.
  `RBREW_PAIRED_INLINE=0` restores the interpreter for the same executable.
  The authored 25-character animation benchmark improved from 4.7956 to
  3.3623 ms with identical palette and state checksums. This is a CPU kernel
  benchmark, **not a game FPS result**. The new differential test compares
  11,008 instruction cases bit-for-bit against the interpreter.
- The listed-query counting prototype really is off by default now
  (`RBREW_QUERY_LIST_COUNT=1` opts into the earlier experimental behaviour).
  Published listed queries cannot bypass queued rendering without that count.
  A non-ready query clears its output instead of leaving an old sample count.
  The existing CPU-clear and queued-query regression tests pass.
- Rejected: bypassing a null `GX2SetContextState` call. The matched replay
  went from 31.35 to 29.53 FPS (1,480/1,420 loaded frames; p99 40.64/45.44 ms).
  The implementation and its switch were removed; real queue fences remain.
- `tools/perf/prof.ps1` refuses reused run labels and keeps replay saves in
  the run directory. It unlinks only junction nodes, never their targets.
  `ab.ps1` delegates to this same implementation. `check_replay.py` checks
  sampled HUD brightness, writes contact sheets and optionally replaces BMPs
  with pixel-verified PNGs. Dense screenshot runs do not produce FPS results.

Final validation is limited to the plaza at the user's request. The canceled
level graphics capture ran alongside compilation and is not a performance
comparison. FPS measurements must run after the compiler has stopped and
with `RBREW_FRAME_DUMP_EVERY=100000`; graphics comparisons use every sixth
frame. The installed game remains unchanged until those checks complete.

### Plaza and level, matched replays (2026-10-25, evening)

Matched replays of the user's recording (`inputs-plaza.txt` stands in the
plaza; `inputs.txt` plays mission 2), all new switches off ("base") versus the
current defaults ("final"):

| | base | final |
|---|---|---|
| Plaza average | 30.2 FPS (p50 32.8 ms) | 35.0 FPS (p50 28.1 ms) |
| Level average | 55.4 FPS, 61% of frames <= 17.5 ms, p99 29.3 ms | 58.1 FPS, 95% <= 17.5 ms, p99 23.3 ms |

Level graphics check (every sixth frame, 350 shots): no single-frame HUD loss;
the only HUD-less shots are the iris wipe of a fall.

Kept (defaults, each with a switch back):
- Query-end submit batching, async shaders, conditional render without GPU
  wait, posted GX2Invalidate inside lists (see above).
- `RBREW_LIST_QUERY_POLL=0`: long display-list replays publish finished
  occlusion results every 128 commands instead of only between render items.
- `RBREW_VSYNC_WAIT_LATE=1`: GX2WaitForVsync waited for the next vblank even
  when the last flip was already a vblank or more ago. Gambit's pacer
  (02796C4C) reads the frame timestamps it stores at 0x101C91B8/C0 in many
  places (frame deltas), so a late frame now continues at once.
- Paired-single inlining by Codex (`RBREW_PAIRED_INLINE=0`).

Measured and dropped or left off:
- `RBREW_QUERY_LIST_COUNT=1..3` (off): the plaza main thread waits ~10-12 ms
  per frame for the scene list (`ListCall`, ~11.6 ms to replay ~4800 draws).
  Answering the occlusion reads behind it without that wait (queued begin,
  or counters cleared by a newer recording) gave 37.5 FPS in the plaza, but
  produced single-frame HUD losses in the level (3 isolated drops in 350
  shots). Each half alone gave no plaza gain. Waiting only until the read's
  own result is published (no full drain) was not faster either.
- Posting GX2SetContextState instead of waiting: the game hung after ~20 s.
- `RBREW_GAME_30FPS=0` (skip the pacer's 30 FPS-mode vblank wait): no gain;
  the time moved into game work.
- Larger write-watch windows (`RBREW_WRITE_WATCH_WINDOW`, 256/2048 pages):
  no gain.

Why the plaza is not at 60 yet: the game thread does ~14-15 ms of its own work
per frame (recompiled code, no single hot function above 2%; display-list
recording ~7%), then waits ~12 ms for the render thread to replay the frame's
scene list. The render thread's time in that replay: write-watch queries 13%,
texture checks 16%, shader-key resolution 13%, per-call decoding of
unrecorded lists 5%. On the console the plaza runs at 30 FPS. Reaching 60
needs the scene-list replay to become several times faster (e.g. caching the
Vulkan work of unchanged display lists) or a safe way to answer that fence
query without waiting.

### Plaza fast mode (2026-10-25, night, user's choice)

The plaza now runs at ~55 FPS (was 30) with these defaults:
- `RBREW_QUERY_ASSUME_VISIBLE` (on; `=0` waits): a busy listed occlusion
  read answers "visible" (the query's last nonzero count, else 4096) instead
  of draining the render queue for the ~11.6 ms scene list.
- GX2SetContextState is posted instead of waited for
  (`RBREW_CONTEXT_STATE_SYNC=1` waits). Its shadow snapshot list is built in
  a private builder slot of the render thread; built in the game core's slot,
  it captured the game's concurrent commands and hung the game.
- Long scene lists are submitted every 300 draws so the GPU works alongside
  (`RBREW_SUBMIT_EVERY_DRAWS=<n>`, `0` disables).
- The pacer's 30 FPS-mode vblank wait and the late-frame vblank wait are
  skipped (`RBREW_GAME_30FPS=1`, `RBREW_VSYNC_WAIT_LATE=1` restore them).
- Large display lists (> 512 commands) are no longer hashed for reuse
  (`RBREW_DL_HASH_ALL=1`).

Known consequence: the plaza's game logic runs too fast. Gambit's loop
(02818E7C) steps the logic once per iteration; in the plaza's 30 FPS mode
(bit 0 at +0x74 of the framework object) it draws every other iteration and
the undrawn iteration does the rest of the frame. Displayed FPS x 2 = logic
steps per second, so normal speed means exactly 30 displayed FPS. Tried:
the game's 60 FPS mode (`RBREW_GAME_60FPS_MODE=1`): correct speed, but no
NPCs appear; drawing every iteration: black plaza. Forcing write helpers
inline: no FPS gain, build 43 instead of 23 minutes, reverted.

### Plaza 60 FPS with normal game speed (2026-10-25, late)

Replaces the "plaza fast mode" above.

- `RBREW_PLAZA_60FPS` (default adaptive; `=1` always 60 FPS mode, `=0` the
  game's 30 FPS plaza). Same idea as Exzap's Cemu pack "60FPS In Plaza", in
  own code for this build: Gambit_fn_02A092A8 (called through a vtable once
  per loop iteration) copies an object's 30 FPS request (bit 0 of +0x304)
  into the framework's mode bit (+0x74). A dispatch-table wrapper clears the
  request, so the plaza runs in the game's 60 FPS mode with its NPCs (forcing
  the framework bit itself every frame lost the NPCs). In that mode the logic
  steps once per frame, so a slow frame would slow the game: a time account
  (real time minus 1/60 s per iteration) lets one 30 FPS frame through (two
  logic steps, its undrawn iteration included) whenever the logic is half a
  step behind; the pacer's two-vblank wait is skipped for those frames.
  Logic stays at ~60 steps/s (measured with RBREW_PLAZA_STATS).
- Plaza ~50 FPS in the replayed view (p50 19.4 ms), up to ~59 FPS when the
  camera looks at the ground; console: 30.
- `RBREW_QUERY_ASSUME_VISIBLE` now applies only in the plaza (scene kind 2,
  default; `=1` everywhere, `=0` never): Gambit also tests the ground under
  the player with occlusion queries, and answering "visible" everywhere made
  normal ground count as enemy ink in Octo Valley.
- `RBREW_GAME_30FPS` and `RBREW_VSYNC_WAIT_LATE` are back to console timing by
  default (outside the plaza a 30 FPS-mode scene must not run fast).
- GX2SetContextState is posted only after 120 presented frames: posting it
  during boot hung the game before its first frame in rare runs.
