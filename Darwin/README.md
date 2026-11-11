# Darwin build

This patch queue targets Wine on macOS: Windows ARM64EC/WoW64 translator DLLs
and native ARM64 Unix helpers. It does not maintain a Linux FEX runtime or its
POSIX test suite.

Run `uv run Darwin/prepare.py` from the repository root. It fetches the upstream
commit pinned in `provenance.json`, applies `patches/series`, and checks the exact
source tree. Applied sources and build outputs stay under ignored `build/local/`.
The upstream release tag is kept for CMake's version detection.

The pin is commit `e3b30dda25342362588ed1096494bea019cde4cd`, 39 commits after
FEX-2611. Preparation fetches that exact SHA and the release tag with ancestry;
the tag supplies CMake's version name, not the selected source revision.

See [patches/README.md](patches/README.md) for the 56-patch evidence inventory,
runtime reachability, retention reasons, removal checks and historical receipts.
The retained set is 55 FEX entries (49 local, five Hadron, one Arknights) plus
one Wine companion. IDs remain stable; the patch map records removed IDs and
their upstream replacements. Individual groups are not independent feature switches.

Set `LLVM_MINGW` to your llvm-mingw installation. Initialize the required upstream
submodules and build both translator DLLs and the native ARM64 helpers:

```sh
: "${LLVM_MINGW:?Set LLVM_MINGW to the llvm-mingw installation}"
export PATH="$LLVM_MINGW/bin:$PATH"
SOURCE="$PWD/build/local/source"
OUTPUT="$PWD/build/local/native"
HOST_PAGE_SIZE="$(getconf PAGESIZE)"
git -C "$SOURCE" submodule update --init \
    External/fmt External/range-v3 External/rpmalloc \
    External/unordered_dense External/xxhash Source/Common/cpp-optparse

for target in arm64ec wow64; do
    triple=arm64ec-w64-mingw32
    if [ "$target" = wow64 ]; then triple=aarch64-w64-mingw32; fi
    cmake -S "$SOURCE" -B "$OUTPUT/build-$target" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_TOOLCHAIN_FILE="$SOURCE/Data/CMake/toolchain_mingw.cmake" \
        -DMINGW_TRIPLE="$triple" -DENABLE_LTO=OFF -DENABLE_ASSERTIONS=OFF \
        -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DBUILD_TESTING=OFF \
        -DTUNE_ARCH=generic -DTUNE_CPU=none -DRANGES_NATIVE=OFF \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--section-alignment,$HOST_PAGE_SIZE" \
        -DPython_EXECUTABLE="$(command -v python3)" \
        -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$LLVM_MINGW/bin/clang-scan-deps" || exit
    cmake --build "$OUTPUT/build-$target" --target "${target}fex" --parallel 8 || exit
done
cmake -S "$SOURCE/Source/Windows/UnixLib" -B "$OUTPUT/build-unix" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$OUTPUT/build-unix" --parallel 8
```

Upstream disables `preserve_all` for MinGW Clang older than 22.1. The tested
Clang 22.1.8 build enables it and applies upstream's
`-fno-asynchronous-unwind-tables` workaround. Stage `libarm64ecfex.dll`
with its `xtajit64.dll` alias, and `libwow64fex.dll` with its `xtajit.dll` alias,
in the Wine runtime's `lib/wine/aarch64-windows/`. Both native helpers from
`build-unix/` belong in `lib/wine/aarch64-unix/`.
Patch `bug-fixes/0002` removes the unused native support-query operation;
`JITWriteProtect(2)` already queries support and the current thread mode.
The new upstream CPU-index query also occupies an operation slot. These changes
shift the subsequent JIT-write and cache-unmapping operation numbers.
Rebuild and stage both PE DLLs and Unix helpers together rather than mixing
them with older helpers.

The PE section alignment must match the native host page size. With the restored
code layout, 4 KiB sections placed writable data and read-only unwind data on
the same 16 KiB page and Wine failed before translator initialization. Relinking
the same source with the host-page alignment above passed the runtime checks.

Arknights flag transport requires the paired Wine patch and
`patches/wine/0001-ntdll-preserve-carried-eflags-on-continuation.patch`.
Use Wine-darwin milestone `6590d86`; set `LMISXH_AKMOCL_EFLAGS=0` for an unpaired
Wine. Hardware TSO also requires Wine's matching process policy, an entitled
loader and a host that permits the Darwin API; otherwise software ordering stays
active. `HADRON_HARDWARE_TSO=0` opts out.

The exact patched source tree and patch checksums are authoritative in
[provenance.json](provenance.json), rather than a copied tree hash in this guide.
The current queue uses **A633**, excluding A632 and older generated host bytes.
The latest upstream pin supplies spill-stack cleanup for nonreturning JIT exits,
replacing local bug-fixes/0036. Its changed host code must not reuse older cache
entries. A632 introduced the SMC, cache-publication and bit-string corrections.
Read the actual FormatVersion from DiskCache.h in the checked replay.
Effective hardware TSO now occupies the remaining bit in upstream's process
feature hash. The cache uses upstream constructor initialization after Windows
has selected TSO and installed the mapper/thread hooks.

The [historical upgrade receipt](../build/local/upstream-pin-20261110/verification.json)
records the 56-entry exact replay, seven preparation tests, both DLLs and both
native helpers for the earlier A631 tree. Its final Wine run has 42 passes and
four unchanged x87 failures; it does not verify the current corrections.
The reproduced POPF single-step defect and a CPUID terminal-exit spill-stack
omission found in review are corrected.
That focused review covered the pin upgrade. A dedicated fixture proving
nonzero spill slots at the CPUID exit was blocked by the tool provider;
the ordinary runtime batch does not establish that pressure case.
The native mapping fixture releases all 64 RO and 64 RW mappings. The new
SERIALIZE, long-DIV and partial-fetch cases pass. CPUID single-step fails before
the POPF correction and when the separate upstream TF guard is removed.
Actual WoW64 startup still fails with `c0000017` before guest entry. Default
hardware TSO reports result 5/process policy 0, so active hardware TSO is not
established. These checks used an isolated lane; no installed game runtime
was replaced.

The current corrections protect the private XSTATE hook from asynchronous
suspension, serialize section queries with unmap, and include register bit
indices before architectural address wrapping. SMC checks use the bytes actually
consumed by the decoder. Validated subblocks retain fixed operands and exact
cache extents; cache insertion checks one owned guest-byte snapshot before
publishing it. Disk caching remains enabled, but masked-operand reuse is no
longer used for these subblocks. Its performance cost has not been measured.
The local mapping fixture follows the paired mapper/unmapper ownership API.
The [latest pin receipt](../build/local/upstream-pin-20261111/verification.json)
records the clean 55-patch replay, both translator and native helper builds,
seven preparation tests, 146 shared-data checks, nested JIT-write scopes and the
host cache ASan/UBSan fixture. Wine execution of this pin remains unverified:
the existing loader is rejected by the host before guest entry because its
restricted-entitlement signature fails validation. Earlier Wine passes must not
be relabeled as current runtime evidence.

The [support-query cleanup receipt](../build/local/jev-cleanup-20261110/verification.json)
records the 61-entry replay, both DLL/helper builds, nested-scope fixture, six
native ARM64 Wine JIT/cache scenarios and 128 actual IndexedDB mapping-release
cycles through the renumbered native operation. This used an isolated runtime;
the mapping fixture is native ARM64 and does not establish WoW64 guest startup.

The [historical receipt index](patches/README.md#historical-receipt-index) preserves
all earlier replay/build/runtime evidence. In particular,
[slop-reduction verification](../build/local/slop-reduction-20261110/verification.json)
records both translator builds and 39 native ARM64 Wine scenarios with matching
loaded hashes and cleanup for the pre-removal baseline. Native helpers were
unchanged/reused there. The later 66-entry feature split replay was source
identical and did not rerun those scenarios.

The earlier FEX-2610 reduction's receipts are under
[verified-removals-20261110](../build/local/verified-removals-20261110/).
Official replay and both translator builds pass. The 43 isolated Wine scenarios
contain 39 passes and four extended x87 runs with eight existing failures each.
Those x87 outputs are identical before/after removal under four configurations:
the inherited full-tag reconstruction loses the zero-versus-valid distinction.
The failing assertions remain intact. Other environment state/value/extent checks
and m80 boundary faults pass. That historical build left SERIALIZE unadvertised;
the current upstream pin advertises and implements it, including the reserved
encoding correction after FEX-2611.
See the [removal results](patches/README.md#removal-decisions-and-required-checks)
for evidence and limits. Unchanged native helpers were reused, not rebuilt.

Pre-PR REP/string and gather retain the original bulk paths. Restoration receipts
under [prepr-memory-restore-20261110](../build/local/prepr-memory-restore-20261110/)
include source equality checks, 33 ordinary scenario passes and the unchanged
extended test failures: REP at `addr32-zero-count-opae-df0`, gather at
`vpgatherdd-xmm-gs-addr64-disp64`, under default and software TSO. Later
partial-fault/REP suspension cases were not reached, and the original bulk
contracts do not provide precise partial-fault progress. Earlier
compromise-versus-scalar performance results do not describe these restored paths.
The old native `ntdll.so` caused earlier context-flag failures; the paired
continuation fix is required, not just a Wine version label.

WoW64 historically built, but actual guest startup failed low-address allocation
with `c0000017`. Its instruction execution remains unverified; passing x64
fixtures that call DWORD helpers or production tracking code do not close that
gap. SVE256 execution is unavailable on this host. Resource fixtures distinguish
UAF, block-link arena release, target-heap retirement and native virtual mapping
release; an 8 GiB cache reservation is not 8 GiB of resident memory. JIT spill
frame repair is stack correctness. No result establishes a game's memory plateau,
character-jitter fix, FPS improvement or map-stutter cause.

The JIT lookup L2 cache defaults to disabled. Set `FEX_DISABLEL2CACHE=0` in the
environment launching Wine to enable it; this trades additional per-thread
memory for another lookup level. It is independent of the disk cache.

The disk cache remains enabled by its pre-PR default, while DiskCacheMemorySize
defaults to zero. The retained memory-cache header fix applies when a positive
LRU budget is selected; queued disk writes still need owned blobs at budget zero.
Default Apple execution uses LSE atomics; retained LL/SC fixes cover the nondefault
fallback/override. Volatile metadata support is optional, but its bounds and
forced-ordering contract remain while that feature is supported. The three small
allocation/translation optimizations have concrete avoided work, without a
measured game FPS claim; the FXRSTOR TOP patch is a test asset only.
