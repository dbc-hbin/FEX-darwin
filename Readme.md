# FEX-darwin

Darwin/Wine integration of [FEX](https://github.com/FEX-Emu/FEX), based on
`FEX-2611-9-gc985199ed` (`c985199ed5032a871366572c3d6a4f80f12ae6e7`).
Builds the x64 ARM64EC emulator (`xtajit64.dll`) and x86 WoW64 emulator (`xtajit.dll`)
for ARM64-native Wine on macOS. Darwin modifications are licensed under [MIT](LICENSE);
upstream FEX notices are retained in [LICENSE.upstream](LICENSE.upstream).

## Darwin patches

- Shared-data reads: emulate relocated `KUSER_SHARED_DATA` loads, including REP-copy writeback.
- JIT write scopes: restore nested Darwin JIT write-protection state, toggled inline from the commpage after a startup self-test against Wine's unix call.
- Native JIT allocation: use Wine's Darwin executable-memory allocator and 16KB PE alignment.
- WoW64 integration: use identity-mapped guest addresses, preserve fault-safe PUSH state and whole-process code invalidation. Completed effective addresses wrap to their address size; additional accesses within an operand remain linear.
- Call/return guards: use 16KB guard pages on Darwin.
- Fault/context correctness: retain REP/gather progress, honor XRSTOR requests and preserve extended state through paired Wine APC/thread-context paths.
- String instructions: honor address-sized REP counts and LODS source-segment overrides, and distinguish ES access violations from SS stack faults.

The upstream rebase includes thread-termination fixes, PCMPXSTRX lowering and disk-cache optimizations.
Local cache fixes cover locking, size limits, retry after failure/eviction and Unicode cleanup.
Indexed cache entries share immutable guest extents while snapshots retain file-mapping ownership.
The retired nonzero guest window no longer reserves host registers or adds address-conversion IR.
Disk caching is opt-in; the memory LRU defaults to 0 bytes and the main-file cap to 1GiB, excluding the index.
Original patches, upstream commit IDs and extraction hashes are in [Darwin/provenance.json](Darwin/provenance.json).
The patches are historical provenance, not an apply queue for the current upstream.
The integration already includes the retained changes; retired guest-window patches must not be reapplied.

## Current validation

The November 6 rebase and debloat integration is checked under
`build/local/main-upstream-20261106T145146Z/`, with the final DLLs in
`native-build-final/`. Both native-built emulator DLLs pass
the PE architecture, exports, imports, CHPE, builtin-marker and 16KB-alignment audits.
Native CPU tests cover 146 scalar/vector/shared-page cases, nested JIT write scopes
and the hardware-TSO policy/cache identity. Cache ownership, publication and pruning
checks pass with ASan/UBSan.

The retained Wine 11.18 runtime lacks the main branch's native JIT-protection hooks.
Its automatic writable-code SMC path is therefore unavailable. Explicit
`FEX_SMCCHECKS=full` passes 25 SMC cases and cross-process memory-notification
checks in each guest ABI. In that mode, Windows
Chromium 157.0.8103.38 passes TurboFan optimization, deoptimization, reoptimization,
integer checksum and WebAssembly execution checks. Observed host images are native
ARM64 and the physical emulator file hash matches the audited build. Wine maps
the logical names `xtajit64.dll` and `xtajit.dll` to `libarm64ecfex.dll` and
`libwow64fex.dll` respectively. Only the `verified-*` runtime receipts use the
corrected physical-file staging; earlier runtime receipts under this directory
do not validate the new DLLs. Guest runs also compare the mapped PE text-section
layout with that physical file.

The guest matrix also passes x64/x86 JIT growth, cold/warm disk caches and mixed
cache extents, 4,096 shuffle cases, default/single-instruction DIV checks,
ARM64EC target remapping, shared-data and call/return checks. The guard/commit
fixture was rebuilt against the current private thread layout and confirms
16KB guards, lazy commitment, growth and invalidation. The 32-bit REP fixture
passes 26,065 cases in each of default, memcpy-TSO and vector-TSO modes. The
bit-index regression passes with software TSO, TSO disabled and full SMC,
including signed offsets and locked updates. Native-backed cache
mapping checks pass 14 cases per guest ABI, including ownership, append fallback
and failed section/view creation. Existing Wine 11.18
failures remain in REP fault flags, YMM context transport, x64 full-SMC fault-time
RDX recovery and both ABIs' O0 startup; the prior accepted build has the same
failure signatures. The cross-4GiB operand fixture cannot run because Wine rejects
its required native mapping with `STATUS_CONFLICTING_ADDRESSES`; those fault
semantics remain unverified. This validation does not establish game FPS, hardware-TSO
activation or compatibility with mismatched Wine UnixLib helpers.

### Historical validation before the rebase

The measurements below describe earlier builds, not fresh performance measurements
of the rebased code.

The M5 Pro build in `build/local/darwin-extra-20261104/` passed both DLL audits.
Wine 11.17 passed 57 x64 and WoW64 runs. Wine 11.20 passed three x64 runs.
All 60 runs verified the native ARM64 host and the loaded emulator DLL hash.
The receipts are `build/local/darwin-extra-{1117-core,1117-rep,1120}/result.json`.

With dynamic L1 enabled and L2 disabled, the Darwin Wine path commits the active
lookup window when a thread starts, when the mask grows and after a cache clear.
Eight additional live x64 threads reserved 16 MiB each on both builds. Their
combined committed L1 memory fell from 128 MiB to 1 MiB. A separate indirect
branch collision test grew one committed window from 128 KiB to 512 KiB and
checked the target of each call. With growth and shrink thresholds set to
8,000 and 7,000 hits per second for the test, the window subsequently shrank
to 256 KiB. The static L1 control committed 16 MiB per thread. These values
come from Wine VirtualQuery.

The x64 bit-scan and x87 probes checked 1,837 cases with CSSC enabled and again
with CSSC disabled. The published code uses CTZ for BSF and TZCNT on CSSC hosts,
including a bit-16 sentinel for 16-bit TZCNT. BSR computes the index with CLZ
and an immediate XOR. Full-precision FILD uses CLZ directly to normalize the
integer and reuses the original zero comparison, saving six host instructions
in each tested width.

Same-register VPTEST now reduces the combined NEON halves once. VTESTPS and
VTESTPD reduce their sign bits. The emitted 256-bit blocks lost ten instructions
for VPTEST and twelve for VTESTPS and VTESTPD. All 1,799 flag cases and seven
guard-page reads passed. Six distinct-register control blocks had identical
disassembly before and after the change.

Wine 11.17 x64 microbenchmarks on the same M5 Pro used the previous validated
DLL and this build in A/B/B/A order. Times are median nanoseconds per iteration
over 14 samples per build after warm-up.

| Guest loop body | Previous DLL | This build |
| --- | ---: | ---: |
| OR + BSF, dependent 64-bit register | 0.700 | 0.560 |
| OR + BSR, dependent 64-bit register | 0.784 | 0.712 |
| OR + TZCNT, dependent 64-bit register | 0.661 | 0.439 |
| FILD qword + FSTP 80-bit, hot memory | 0.645 | 0.527 |
| VTESTPS on one YMM register + SETZ + ADC | 0.823 | 0.357 |

The two BSF process medians for this build were 0.507 and 0.603 ns. Raw samples
are in `build/local/bit-x87-abba-01/` and `build/local/self-ptest-accum-abba.log`.

### Previous Apple Silicon build

The Apple Silicon build in `build/local/apple-silicon-verified-20261104/` passed both
PE audits on the M5 Pro. Wine 11.17 passed 57 x64 and WoW64 runs. Wine 11.20 passed
three additional x64 runs covering shared-page reads, instruction-level ordering and
register recovery, SIMD and ARM64EC target remapping. Each run verified the loaded
emulator DLL against the build. The receipts in `build/local/` are
`apple-verified-compat-1117/result.json`, `verified-pair-rep-1117-20261104/result.json`
and `apple-verified-block-1120/result.json`.

For x64, the emitted AArch64 code for a 256-bit movemask shrank from 16 to 10
instructions. A YMM self-move went from two to zero, and two consecutive VZEROUPPERs
went from 18 to nine. The scalar VADDSS merge used one instruction in both the
distinct-register and second-source-alias cases, previously two and three.

On Wine 11.20, a loop reading the fixed 32-bit Windows version field at
`0x7ffe026c` took 4,718 ns per iteration with the previous DLL and 0.35 ns with
the frame-relative backing pointer. Each sample repeats the same hot read 20,000
times. The numbers are medians of seven samples after warm-up.

The x64 REP benchmark used Wine 11.17 on the same M5 Pro with prefaulted buffers
and byte-for-byte output checks. The table gives median nanoseconds per operation
over 14 samples per build, with baseline and candidate launched in A/B/B/A order.

| Operation | Bytes | Previous DLL | This build |
| --- | ---: | ---: | ---: |
| REP MOVSB | 64 | 2.269 | 1.865 |
| REP MOVSB | 4,096 | 46.008 | 32.965 |
| REP MOVSB | 65,536 | 777.948 | 684.573 |
| REP STOSB | 64 | 1.741 | 1.515 |
| REP STOSB | 4,096 | 32.993 | 31.836 |
| REP STOSB | 65,536 | 566.077 | 550.739 |

The 64 KiB MOVSB median varied between candidate launches (860 and 672 ns).
The logs are in `build/local/rep-check-pre1117/` and `build/local/rep-check-vfy1117/`.

The pre-TSO integrated tree previously passed 56 native ARM64 Wine runs, with loaded emulator DLL hashes
checked against the build. REP coverage includes 31,247 x64 and 26,053 x86 cases in each of
the default, memcpy-TSO and vector-TSO modes. The guest-window boundary probe passes 37 cases
in both x87 precision modes. Regression fixtures and detailed run artifacts remain local.

The string fixes were checked against the previous DLL: x64 address-size-overridden
SCAS, CMPS and LODS incorrectly accessed memory with ECX zero and RCX's upper half nonzero;
the corrected DLL performs no access. Positive-count x64 addr32 SCAS/CMPS is not directly
covered because this Wine environment cannot allocate below 4GiB; GS LODS covers
positive-count addr32 execution. The new 4GiB boundary cases check exception metadata,
registers and memory, but do not establish fault-time flags or restart behavior.
These probes are not a new game-compatibility or performance acceptance run.

The hardware-TSO interface build passes both DLL audits and the native ordering/cache
regression under ASan/UBSan. Against the local Wine 11.20 runtime, six x64 runs pass:
thread termination, cold/warm disk cache, context/APC, shared data and Unix calls.
REP fault-return flags (`ac7` versus `8d7`) and i686 startup (`c0000017`, low-address
mapping before FEX loads) fail identically with the pre-change DLLs in that runtime.
These are not passing WoW64 or hardware-TSO activation results. Evidence:
`build/local/hardware-tso-final-20261104/` and
`build/local/hardware-tso-wine-core-20261104/`.

That build's translated-code cache namespace `0xA61B` invalidated earlier translations of
forwarded partial-register constants. Hardware-TSO and software-TSO contexts have distinct cache identities.
Independent review found no actionable defect in the bounded string
fixes; it does not constitute a fresh audit of every earlier optimization.

## Apple Silicon translation paths

- Cache namespace `0xA620` separates the rebased translator and identity-address layout from earlier generated code. Hardware TSO is part of the process-feature cache identity.
- For 32-bit synthetic subaccesses, scalar TSO loads/stores materialize the complete linear address before accessing memory; their ARM instructions cannot encode a register offset. Locked bit updates also retain the signed offset in 64-bit address arithmetic.
- Same-input shuffle lowering uses broadcasts and the selected EXTR/TRN2 and lane-permutation paths. DIV omits remainder calculation only when the following instructions provably overwrite RDX before it can be observed; native long division retains its required writable scratch register.
- The compiler chooses software TSO per instruction. A PE metadata range covers only whole instructions. Marked PCs and Unity's explicit TSO flag take precedence, including vector and stack-relative accesses. The default policy applies outside certified ranges.
- The split-AVX translator folds x64 displacements into Q-register pairs or their two ordered accesses. VEX.256 self-moves disappear, while VEX.128 self-moves retain the upper-half clear. Repeated upper clears reuse register-cache state. The 32-byte movemask combines both NEON halves before extracting the scalar result. AFP scalar arithmetic takes its preserved lanes from its first input.
- On Windows, adjacent non-faulting register moves and integer operations can forward their GPR and XMM operands. The translator synchronizes fixed registers before faulting and control-flow instructions, single-step and full SMC checks.
- Before promoting an 8/16-bit logical operation to a full-register operation, the compiler masks a forwarded constant to the operand width. This preserves the destination's upper bits while retaining register forwarding and widened logical operations. The local `apple_block_probe` covers byte/word XOR, OR and AND with full-width source constants; its byte XOR case reproduced the ZZZ startup corruption before the fix.
- Fixed ARM64EC reads wholly inside the Windows shared data page use the current thread frame's Wine backing address. Dynamic addresses retain the exception handler. Stores still target the read-only guest address.
- Forward REP MOVS and STOS select overlap handling before the copy loop. The wide loop processes 64-byte chunks up to the count and both guest page boundaries. Each completed chunk commits guest indices and count, so a fault or cooperative suspend sees the restart position.
- JIT compilation looks up image data only for active consumers and records subblock names when requested. SMC calculates the expected CRC once per validation segment. On Darwin, JIT growth retains the generation's existing translations in append-only regions up to 128 MiB. Each generation owns its code, links and block map together.
- A stable indirect ARM64EC call or jump can hit a native-target inline-cache slot. The entry checks the current EC bitmap before transferring through Wine's native exit path, so unloading a DLL and reusing its address for x64 code is handled correctly.

## Wine hardware TSO interface

Both translators optionally resolve this native PE ntdll export before creating
their FEX context:

```c
BOOL WINAPI __wine_process_hardware_tso_enabled(void);
```

This is a process-lifetime guarantee, not a CPU capability or current-thread query.
Wine must return `TRUE` only after guaranteeing hardware TSO for every current,
future or adopted thread that can execute guest code, including callbacks, APCs,
exception continuations and nested execution. A later thread-enable failure must
prevent guest entry; Wine must not silently revoke the guarantee. The query is
side-effect-free, and its result is fixed before FEX context initialization.
Wine must expose native ARM64 and ARM64EC-callable implementations, following its
existing private ntdll export conventions.

FEX accepts only `TRUE`, selects the ordering policy before disk-cache initialization,
and includes the guarantee in the process-feature cache hash. A present hook returning
anything else retains software TSO without falling through to the UnixLib setter.
An absent hook retains the existing UnixLib path; older Darwin Wine without that
library continues using software TSO. `FEX_TSOENABLED=0` retains its existing explicit
override semantics and does not query or enable hardware TSO.

Wine's implementation, per-thread macOS calls and entitlement/provisioning setup
belong to Wine-darwin and are not supplied here. This FEX change alone does not
activate hardware TSO. The local CPU regression is
`python3 Darwin/tests/fex_hardware_tso.py`; it exercises production ordering policy
and cache-key construction with substituted transport, not actual macOS TSO.

## Build and verify

Requires native macOS ARM64, Xcode command-line tools, Python 3, CMake, Make and llvm-mingw 22.1.8.

```sh
git submodule update --init --depth 1 External/fmt External/range-v3 External/rpmalloc External/unordered_dense External/xxhash Source/Common/cpp-optparse
python3 Darwin/build.py --output build/local/darwin01 --jobs 8
```

Use a fresh build directory. The builder audits both DLLs; outputs and provenance are under
`build/local/darwin01/`.

Wine must provide the shared-data, JIT write-protection and native JIT allocation hooks.
WoW64 no longer requires a guest-window hook. Optional UnixLib helpers must match
the current upstream ABI: older Hadron helpers use slot 8 for a JIT query, whereas
current FEX uses it for `CheckSupportsTPIDRRO_CPU`. They are not interchangeable.
Wine changes and graphics backends are separate from this repository. This is not a standalone macOS or Linux loader.

Upstream architecture and development documentation: [FEX wiki](https://wiki.fex-emu.com/) and [source outline](docs/SourceOutline.md).
