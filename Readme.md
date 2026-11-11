# FEX-darwin

Darwin / Apple Silicon patches for native ARM64 Wine's x64 and WoW64 translators.
This repository keeps our patch queue; FEX itself is fetched from
[FEX-Emu/FEX](https://github.com/FEX-Emu/FEX) at commit
`e3b30dda25342362588ed1096494bea019cde4cd`, after FEX-2611.

The patch queue retains Hadron's hardware TSO and W^X JIT support, architectural
and cache-lifetime fixes, and Arknights EFLAGS transport. REP and gather use the
original pre-PR bulk paths, including their original address/partial-fault
limitations. The later compromise implementation is removed; the independently
verified block-link arena leak fix remains.
Source revisions, patch hashes and licenses are recorded in
[`Darwin/provenance.json`](Darwin/provenance.json).
Local patches are organized by feature, with related frontend, backend and test
changes kept together. The [patch map](Darwin/patches/README.md) records their
purpose and application dependencies.

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

Build and stage both translators and native helpers together, using
native-host-page PE section alignment. The queue contains 55 FEX patches;
seven former local patches now come from the upstream pin. The patch map separates
current validation from historical receipts. A separate reproduced POPF
single-step correction accompanies the upgrade. Extended REP/gather probes have
known original address-size/segment limitations; see the patch map.
i386 execution, game performance and jitter remain unverified.
Darwin packaging and tooling are MIT-licensed under [`LICENSE`](LICENSE),
copyright 2026 dbc-hbin. The original [FEX MIT notice](Darwin/patches/FEX-LICENSE)
and imported Hadron (MIT) and Arknights (MPL-2.0) notices remain with the patches.
