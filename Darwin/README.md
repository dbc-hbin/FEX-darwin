# Debloated Darwin patch series

This worktree keeps upstream FEX-2610 (`14c92681f4d62cf84d901460e0358de09c8847a7`) unchanged. `patches/series` is the application order; `provenance.json` records the upstream and imported revisions, patch SHA-256 values, and expected applied source tree.

The patches are copied byte-for-byte from wine-build-new commit `396ecb609d07bf8f90905070d53b92f0a65d49bf` (`cleanup/fex-patch-source-debloat`, FEX package revision 10). Hadron and Arknights patches are regular files here, so this queue does not depend on the Wine checkout or its symlinks. Hadron's original submodule path is spelled `third-party/hardon`.

The imported license texts are retained at `patches/hadron/LICENSE` (David Andrews, MIT) and `patches/arknights-macos-runtime/LICENSE` (MPL 2.0). Preserve them when redistributing this queue.

The order is five Hadron Darwin/64 KiB guard/hardware-TSO/W^X patches, eleven local correctness and optimization patches (including Hadron JIT memory/invalidation), then Arknights carried-EFLAGS handling. The final debloat already folds former local patches 0012-0014 into their feature owners. Do not append those patches or the superseded standalone cleanup patch. Translation cache namespace remains `0xA623`.

## Apply to a fresh upstream source

Run from this worktree's root. `build/local/source` must not already exist.

```sh
git clone --shared --no-checkout . build/local/source
git -C build/local/source switch --detach 14c92681f4d62cf84d901460e0358de09c8847a7
while IFS= read -r entry; do
    patch -d build/local/source -p1 -F0 --batch --no-backup-if-mismatch \
        -i "$PWD/Darwin/patches/$entry" || exit
done < Darwin/patches/series
git -C build/local/source add --all
git -C build/local/source write-tree
```

The resulting source tree must be `7f0dbe825787d8c43e6442af8581164de3b9e8cb`. This is a tree object, not a source commit. Application must require no offsets or fuzz. Keep applied sources and outputs below `build/local/`; edit the patch queue to maintain this distribution.

Initialize only the build dependencies at their upstream gitlink revisions:

```sh
git -C build/local/source submodule update --init \
    External/fmt External/range-v3 External/rpmalloc \
    External/unordered_dense External/xxhash Source/Common/cpp-optparse
```

## Native macOS ARM64 build

Use CMake, Ninja, Python 3, the macOS SDK and llvm-mingw. The commands follow the source Portfile's Release recipe. Stock llvm-mingw must keep `FEX_MINGW_PRESERVE_ALL=OFF` because its Windows AArch64 `preserve_all` code generation is unsuitable for these fallbacks.

```sh
export LLVM_MINGW=/opt/llvm-mingw-20260616-ucrt-macos-universal
export PATH="$LLVM_MINGW/bin:$PATH"
SOURCE="$PWD/build/local/source"
OUTPUT="$PWD/build/local/native"
for target in arm64ec wow64; do
    triple=arm64ec-w64-mingw32
    if [ "$target" = wow64 ]; then triple=aarch64-w64-mingw32; fi
    cmake -S "$SOURCE" -B "$OUTPUT/build-$target" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_TOOLCHAIN_FILE="$SOURCE/Data/CMake/toolchain_mingw.cmake" \
        -DMINGW_TRIPLE="$triple" -DENABLE_LTO=OFF -DENABLE_ASSERTIONS=OFF \
        -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DBUILD_TESTING=OFF \
        -DTUNE_ARCH=generic -DTUNE_CPU=none -DRANGES_NATIVE=OFF \
        -DFEX_MINGW_PRESERVE_ALL=OFF \
        -DPython_EXECUTABLE="$(command -v python3)" \
        -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$LLVM_MINGW/bin/clang-scan-deps" || exit
    cmake --build "$OUTPUT/build-$target" --target "${target}fex" --parallel 8 || exit
done
cmake -S "$SOURCE/Source/Windows/UnixLib" -B "$OUTPUT/build-unix" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$OUTPUT/build-unix" --parallel 8
```

The final Arknights patch requires the [paired Wine EFLAGS transport patch](https://github.com/riverfog7/wine-build-new/blob/396ecb609d07bf8f90905070d53b92f0a65d49bf/patches/wine/arknights-macos-runtime/0001-ntdll-carry-eflags-af-df.patch) when extra flag transport is enabled. Both sides use `LMISXH_AKMOCL_EFLAGS`; an unset value or exactly `1` enables it. Without the paired Wine patch, disable it before starting any Wine process:

```sh
export LMISXH_AKMOCL_EFLAGS=0
```

An unpaired Wine runtime does not transport PF/AF/DF, so enabling this feature there can clear those flags during context synchronization. All 43 fresh regression runs used the opt-out.

Stage `build-arm64ec/Bin/libarm64ecfex.dll` as both `libarm64ecfex.dll` and `xtajit64.dll`, and `build-wow64/Bin/libwow64fex.dll` as both `libwow64fex.dll` and `xtajit.dll`, in a private Wine runtime's `lib/wine/aarch64-windows/`. Both native helpers in `build-unix/` belong in its `lib/wine/aarch64-unix/`. Use a fresh private Wine prefix and verify the loaded DLL and helper hashes; replacing only the xtajit aliases can leave Wine loading an older builtin implementation.

## Validation limits

The original final debloat passed 17-patch replay, both PE builds, native helper builds, PE audits and cache ASan/UBSan checks. Its native Wine run passed 32/43 checks with eleven existing failures: six legacy REP cases, `64-context_probe-default`, `x86_64-div-full`, `x86_64-div-o0`, `i686-div-o0`, and `rep-fault-flags-64`. The x86 REP fault-flags fixture passed all 3,936 cases; the x64 fixture had 6,468 failures out of 10,944, identical to its baseline. These historical results are not a green suite or a game/performance claim. Fresh validation results for this worktree are stored under `build/local/`.

Fresh validation on 2026-11-08 reproduced the expected source tree without offsets or fuzz, built both DLLs and both native helpers, and checked PE architecture, exports, imports and Wine builtin markers. All 43 native Wine runs verified the newly built DLL and helper identities; 32 passed, and the same eleven failures retained their previous status and exit code. The private Wine server exited. See `build/local/verification.json`, `build/local/pe-audit.log` and `build/local/wine-results/result.json`. Existing CMake/range-v3, unused-field and helper C++17-extension warnings remain; cache sanitizer checks were not rerun for this byte-identical patch transfer.
