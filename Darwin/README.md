# Darwin build

Run `uv run Darwin/prepare.py` from the repository root. It fetches the upstream
commit pinned in `provenance.json`, applies `patches/series`, and checks the exact
source tree. Applied sources and build outputs stay under ignored `build/local/`.
The upstream release tag is kept for CMake's version detection.

Set `LLVM_MINGW` to your llvm-mingw installation. Initialize the required upstream
submodules and build both translator DLLs and the native ARM64 helpers:

```sh
: "${LLVM_MINGW:?Set LLVM_MINGW to the llvm-mingw installation}"
export PATH="$LLVM_MINGW/bin:$PATH"
SOURCE="$PWD/build/local/source"
OUTPUT="$PWD/build/local/native"
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

Keep `FEX_MINGW_PRESERVE_ALL=OFF` with stock llvm-mingw. Stage `libarm64ecfex.dll`
with its `xtajit64.dll` alias, and `libwow64fex.dll` with its `xtajit.dll` alias,
in the Wine runtime's `lib/wine/aarch64-windows/`. Both native helpers from
`build-unix/` belong in `lib/wine/aarch64-unix/`.

Arknights flag transport requires the paired Wine patch and
`patches/wine/0001-ntdll-preserve-carried-eflags-on-continuation.patch`.
Use Wine-darwin milestone `6590d86`; set `LMISXH_AKMOCL_EFLAGS=0` for an unpaired
Wine. Hardware TSO also requires Wine's matching process policy, an entitled
loader and a host that permits the Darwin API; otherwise software ordering stays
active. `HADRON_HARDWARE_TSO=0` opts out.

The patched source tree is recorded in `provenance.json`; cache namespace is
`0xA624`. Existing build and regression receipts remain in
`../FEX-darwin-wt/fex-2610-debloated-patches/build/local/` relative to this checkout.
The runtime passed 23 reachable x64 CPU cases and eight GPU readbacks. ZZZ reached
city loading; shutdown retained two `winemetal` assertions. i386 startup retains
the Wine low-address limitation. These results do not establish full game
compatibility or clean shutdown.
