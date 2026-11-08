# FEX-darwin

Darwin / Apple Silicon patches for native ARM64 Wine's x64 and WoW64 translators.
This repository keeps our patch queue; FEX itself is fetched from
[FEX-Emu/FEX](https://github.com/FEX-Emu/FEX) at the pinned FEX-2610 revision.

The 18 patches include Hadron's hardware TSO and W^X JIT support, translation and
cache optimizations, Arknights EFLAGS transport, and the REP/DIV exception fixes.
Source revisions, patch hashes and licenses are recorded in
[`Darwin/provenance.json`](Darwin/provenance.json).

## Prepare and build

Requires macOS ARM64, [uv](https://docs.astral.sh/uv/), Git, and `patch`.
Building also requires CMake, Ninja, llvm-mingw and the macOS SDK.

```sh
git clone --depth 1 https://github.com/dbc-hbin/FEX-darwin.git
cd FEX-darwin
uv run Darwin/prepare.py
# Source is downloaded and patched in build/local/source.
```

An optional destination can be supplied as the first argument; it must not exist.
The helper verifies the upstream commit, every patch hash and the resulting
source tree, retaining the release tag for FEX's build version detection.
See [Darwin build instructions](Darwin/README.md) for the two PE translators and
native Unix helpers.

Use the paired Wine 11.20 EFLAGS transport and the companion continuation patch
in `Darwin/patches/wine/`. With an unpaired Wine, set `LMISXH_AKMOCL_EFLAGS=0`.

The existing runtime passed 23 x64 CPU checks and eight GPU readbacks, and ZZZ
reached city loading. i386 startup and clean game shutdown remain unverified.
Darwin packaging and tooling are MIT-licensed under [`LICENSE`](LICENSE),
copyright 2026 dbc-hbin. The original [FEX MIT notice](Darwin/patches/FEX-LICENSE)
and imported Hadron (MIT) and Arknights (MPL-2.0) notices remain with the patches.
