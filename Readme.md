# FEX-darwin

Darwin/Wine integration of [FEX](https://github.com/FEX-Emu/FEX), based on `FEX-2609.1`.
Builds the x64 ARM64EC emulator (`xtajit64.dll`) and x86 WoW64 emulator (`xtajit.dll`)
for ARM64-native Wine on macOS. Darwin modifications are licensed under [MIT](LICENSE);
upstream FEX notices are retained in [LICENSE.upstream](LICENSE.upstream).

## Darwin patches

- Shared-data reads: emulate relocated `KUSER_SHARED_DATA` loads, including REP-copy writeback.
- JIT write scopes: restore nested Darwin JIT write-protection state, toggled inline from the commpage after a startup self-test against Wine's unix call.
- Native JIT allocation: use Wine's Darwin executable-memory allocator and 16KB PE alignment.
- WoW64 guest window: translate 32-bit guest addresses; core adapted from [willfaust/FEX PR #2](https://github.com/willfaust/FEX/pull/2) (MIT).
- WoW64 integration: preserve guest pointer identity, fault-safe PUSH state and whole-process code invalidation.
- Call/return guards: use 16KB guard pages on Darwin.
- Fault/context correctness: retain REP/gather progress, honor XRSTOR requests and preserve extended state through paired Wine APC/thread-context paths.
- String instructions: honor address-sized REP counts and LODS source-segment overrides, reject multi-byte WoW64 operands crossing the 4GiB guest window, and distinguish ES access violations from SS stack faults.

Selected upstream backports add thread-termination fixes, PCMPXSTRX lowering and disk-cache optimizations.
Local cache fixes cover address translation, locking, size limits, retry after failure/eviction and Unicode cleanup.
Disk caching is opt-in; the memory LRU defaults to 0 bytes and the main-file cap to 1GiB, excluding the index.
Original patches, upstream commit IDs and extraction hashes are in [Darwin/provenance.json](Darwin/provenance.json).
The patches are already integrated; do not apply them again.

## Current validation

The current integrated tree passes 56 native ARM64 Wine runs, with loaded emulator DLL hashes
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

Translated-code cache namespace `0xA617` prevents reuse of code generated before these
correctness fixes. Independent review found no actionable defect in the bounded string
fixes; it does not constitute a fresh audit of every earlier optimization.

## Build and verify

Requires native macOS ARM64, Xcode command-line tools, Python 3, CMake, Make and llvm-mingw 22.1.8.

```sh
git submodule update --init --depth 1 External/fmt External/range-v3 External/rpmalloc External/unordered_dense External/xxhash Source/Common/cpp-optparse
python3 Darwin/build.py --output build/local/darwin01 --jobs 8
```

Use a fresh build directory. The builder audits both DLLs; outputs and provenance are under
`build/local/darwin01/`.

Wine must provide the shared-data, JIT write-protection, native JIT allocation and WoW64 guest-window hooks.
Wine changes and graphics backends are separate from this repository. This is not a standalone macOS or Linux loader.

Upstream architecture and development documentation: [FEX wiki](https://wiki.fex-emu.com/) and [source outline](docs/SourceOutline.md).
