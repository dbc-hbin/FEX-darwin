#!/usr/bin/env python3
"""Build the in-tree FEX-darwin Windows modules without installation.

Recipe: https://wiki.fex-emu.com/index.php/Development:ARM64EC
Uses the host's Unix Makefiles generator (no Ninja dependency); builds only the two DLL targets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys

COMMIT = "9fbdc00bd6401aff3b32d79e78ff98b8a13e4dcf"
TAG = "FEX-2609.1"
REPOSITORY = "https://github.com/FEX-Emu/FEX.git"
SUBMODULES = ("External/fmt", "External/range-v3", "External/rpmalloc",
              "External/unordered_dense", "External/xxhash", "Source/Common/cpp-optparse")
VARIANTS = (("arm64ec", "arm64ec-w64-mingw32", "arm64ecfex", "ARM64EC", "xtajit64.dll"),
            ("wow64", "aarch64-w64-mingw32", "wow64fex", "WOW64", "xtajit.dll"))
LICENSES = {"LICENSE": "MIT", "LICENSE.upstream": "MIT (upstream FEX)", "External/fmt/LICENSE": "MIT",
            "External/range-v3/LICENSE.txt": "BSL-1.0 and bundled notices",
            "External/rpmalloc/LICENSE": "permissive no-attribution grant",
            "External/unordered_dense/LICENSE": "MIT", "External/xxhash/LICENSE": "BSD-2-Clause",
            "Source/Common/cpp-optparse/LICENSE": "MIT", "External/cephes/LICENSE": "BSD permission",
            "External/tiny-json/LICENSE": "MIT",
            "External/SoftFloat-3e/include/SoftFloat-3e/softfloat.h": "BSD-3-Clause (header notice)"}


def audit(data: bytes, report: str, definition: str, marker: bytes, arm64ec: bool) -> dict:
    if data[:2] != b"MZ" or len(data) < 0x100:
        raise RuntimeError("not a PE image")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise RuntimeError("missing PE signature")
    if struct.unpack_from("<I", data, pe + 56)[0] < 16384:
        raise RuntimeError("FEX PE sections must not share Darwin 16KB executable/data host pages")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    # ARM64EC DLLs use the AMD64 on-disk machine for loader compatibility; LLVM
    # reports their effective machine as ARM64EC after recognizing CHPE metadata.
    if machine != (0x8664 if arm64ec else 0xaa64):
        raise RuntimeError(f"unexpected PE machine: {machine:#x}")
    if not struct.unpack_from("<H", data, pe + 22)[0] & 0x2000 or struct.unpack_from("<H", data, pe + 24)[0] != 0x20b:
        raise RuntimeError("not a PE32+ DLL")
    if data[64:64 + len(marker)] != marker or b"Wine builtin DLL\0" not in marker:
        raise RuntimeError("missing upstream Wine builtin marker at DOS offset 64")
    exports = re.findall(r"Export \{.*?\n\s*Name: ([^\n]+)", report, re.S)
    expected = [line.split()[0] for line in definition.split("EXPORTS", 1)[1].splitlines() if line.strip()]
    if set(exports) != set(expected):
        raise RuntimeError(f"export mismatch: expected {expected}, got {exports}")
    imports = re.findall(r"Import \{\s*Name: ([^\n]+)", report)
    allowed = {"ntdll.dll"} if arm64ec else {"ntdll.dll", "wow64.dll"}
    if not imports or set(name.lower() for name in imports) != allowed:
        raise RuntimeError(f"unexpected DLL dependencies: {imports}")
    chpe = re.search(r"CHPEMetadataPointer: (0x[0-9A-Fa-f]+)", report)
    chpe_pointer = int(chpe[1], 16) if chpe else 0
    code_map = re.findall(r"(0x[0-9A-Fa-f]+ - 0x[0-9A-Fa-f]+)  (ARM64EC|X64)", report)
    if arm64ec and ("Format: COFF-ARM64EC" not in report or not chpe_pointer
                   or {kind for _, kind in code_map} != {"ARM64EC", "X64"}):
        raise RuntimeError("ARM64EC image has no valid CHPE metadata/code map")
    return {"machine": hex(machine), "chpe_metadata_pointer": hex(chpe_pointer),
            "chpe_code_map": [list(entry) for entry in code_map],
            "exports": exports, "imports": imports, "wine_builtin_marker": True,
            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--llvm-mingw", type=Path, default=Path("/opt/llvm-mingw-20260616-ucrt-macos-universal"))
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output, source, compiler = args.output.resolve(), root, args.llvm_mingw.resolve()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("native macOS ARM64 host required; no Linux or Rosetta fallback")
    if args.jobs < 1 or output.exists() or not output.is_relative_to(root / "build/local"):
        parser.error("positive jobs and fresh output below build/local required")
    output.mkdir(parents=True)
    logs = output / "logs"
    logs.mkdir()
    environment = os.environ | {"PATH": str(compiler / "bin") + os.pathsep + os.environ["PATH"]}
    result = {"schema": 1, "status": "building", "commands": [], "artifacts": {},
              "source_commit": COMMIT, "source_tag": TAG, "repository": REPOSITORY,
              "recipe": "https://wiki.fex-emu.com/index.php/Development:ARM64EC",
              "installed": False, "guest_execution": "not_run", "source_patches": [],
              "host": {"system": platform.system(), "machine": platform.machine(),
                       "platform": platform.platform(), "python": sys.version}}

    def run(command: list[str], name: str, cwd: Path = root) -> str:
        log = logs / (name + ".log")
        record = {"argv": command, "cwd": str(cwd), "log": str(log)}
        result["commands"].append(record)
        with log.open("wb") as stream:
            process = subprocess.run(command, cwd=cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT)
        record["returncode"] = process.returncode
        if process.returncode:
            raise RuntimeError(f"command failed ({process.returncode}); see {log}")
        return log.read_text(errors="replace")

    try:
        translated = run(["/usr/sbin/sysctl", "-n", "sysctl.proc_translated"], "host-translated").strip()
        result["host"]["proc_translated"] = translated
        if translated != "0":
            raise RuntimeError("Rosetta host rejected")
        run(["/usr/bin/sw_vers"], "host-version")
        for name in ("clang", "llvm-readobj", "llvm-ar", "ld.lld"):
            executable = compiler / "bin" / name
            result["host"][name] = run(["/usr/bin/file", str(executable.resolve())], "host-" + name).strip()
            run([str(executable), "--version"], "version-" + name)
        run(["cmake", "--version"], "version-cmake")
        result["source_head"] = run(["git", "rev-parse", "HEAD"], "source-head", source).strip()
        result["source_status"] = run(["git", "status", "--porcelain", "--untracked-files=all"], "source-status", source)
        result["submodules"] = []
        for index, relative in enumerate(SUBMODULES):
            path = source / relative
            pin = run(["git", "ls-tree", "HEAD", relative], f"submodule-pin-{index}", source).split()[2]
            actual = run(["git", "rev-parse", "HEAD"], f"submodule-head-{index}", path).strip()
            if actual != pin or run(["git", "status", "--porcelain"], f"submodule-clean-{index}", path).strip():
                raise RuntimeError(f"submodule is not pristine/pinned: {relative}")
            result["submodules"].append({"path": relative, "commit": pin})
        licenses = output / "licenses"
        licenses.mkdir()
        result["licenses"] = []
        for name, license_name in LICENSES.items():
            data = (source / name).read_bytes()
            (licenses / name.replace("/", "__")).write_bytes(data)
            result["licenses"].append({"path": name, "license": license_name,
                                       "sha256": hashlib.sha256(data).hexdigest()})
        result["source_checkout"] = str(source)
        provenance_path = root / "Darwin/provenance.json"
        provenance = json.loads(provenance_path.read_text())
        if provenance["baseline"]["commit"] != COMMIT or provenance["baseline"]["tag"] != TAG:
            raise RuntimeError("unexpected baseline provenance")
        result["provenance_sha256"] = hashlib.sha256(provenance_path.read_bytes()).hexdigest()
        for item in provenance["patches"]:
            patch = root / item["path"]
            result["source_patches"].append({"path": item["path"], "sha256": hashlib.sha256(patch.read_bytes()).hexdigest()})
        result["source_files"] = [{"path": item["path"], "sha256": hashlib.sha256((root / item["path"]).read_bytes()).hexdigest()}
                                  for item in provenance["sources"]]
        result["source_snapshot"] = str(source)
        cpu_check = output / "shared-data-cpu-check"
        run(["/usr/bin/clang++", "-arch", "arm64", "-std=c++20", "-Wall", "-Wextra", "-Werror",
             "-I" + str(source / "Source/Windows/ARM64EC"),
             str(root / "Darwin/tests/fex_shared_data_cpu.cpp"), "-o", str(cpu_check)], "shared-data-cpu-build")
        result["shared_data_cpu_check"] = run([str(cpu_check)], "shared-data-cpu-check").strip()
        jit_check = output / "jit-write-scope-check"
        run(["/usr/bin/clang++", "-arch", "arm64", "-std=c++20", "-D_WIN32", "-Wall", "-Wextra", "-Werror",
             "-I" + str(source / "FEXCore/include"),
             str(root / "Darwin/tests/fex_jit_write_scope.cpp"), "-o", str(jit_check)], "jit-write-scope-build")
        result["jit_write_scope_check"] = run([str(jit_check)], "jit-write-scope-check").strip()
        marker = (source / "Source/Windows/wine_builtin.bin").read_bytes()
        result["builtin_marker_sha256"] = hashlib.sha256(marker).hexdigest()
        artifacts = output / "dlls"
        artifacts.mkdir()
        for variant, triple, target, directory, wine_name in VARIANTS:
            build = output / variant
            run([str(compiler / "bin" / (triple + "-clang++")), "--version"], variant + "-compiler")
            run(["cmake", "-S", str(source), "-B", str(build), "-G", "Unix Makefiles",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                 "-DCMAKE_TOOLCHAIN_FILE=" + str(source / "Data/CMake/toolchain_mingw.cmake"),
                 "-DMINGW_TRIPLE=" + triple, "-DENABLE_LTO=False", "-DBUILD_TESTING=False",
                 "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--section-alignment=16384",
                 "-DENABLE_JEMALLOC_GLIBC_ALLOC=False", "-DTUNE_CPU=none", "-DENABLE_CCACHE=False",
                 "-DENABLE_GUEST_WINDOW=" + ("True" if variant == "wow64" else "False"),
                 "-DPython_EXECUTABLE=" + sys.executable], variant + "-configure")
            run(["cmake", "--build", str(build), "--target", target, "--parallel", str(args.jobs)], variant + "-build")
            original = build / "Bin" / ("lib" + target + ".dll")
            destination = artifacts / wine_name
            shutil.copyfile(original, destination)
            report = run([str(compiler / "bin/llvm-readobj"), "--file-headers", "--coff-load-config",
                          "--coff-exports", "--coff-imports", str(destination)], variant + "-pe-audit")
            definition = (source / "Source/Windows" / directory / ("lib" + target + ".def")).read_text()
            result["artifacts"][wine_name] = audit(destination.read_bytes(), report, definition, marker, variant == "arm64ec") | {"path": str(destination), "upstream_path": str(original)}
        result["status"] = "pass"
    except (OSError, RuntimeError, ValueError, struct.error) as error:
        result["status"] = "blocked"
        result["blocker"] = str(error)
    (output / "fex-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
