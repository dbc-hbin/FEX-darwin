#!/usr/bin/env python3
"""Run the built FEX DLLs in a cloned ARM64 Wine runtime and a fresh private prefix."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROBES = {"smc_probe": "smc_checks", "thread_termination": "thread_termination", "pcmp_probe": "pcmp_checks",
          "disk_cache_lookup": "cache_checks", "x87_probe": "x87_checks", "wine_unix_probe": "wine_unix_checks",
          "wine_user_probe": "wine_user_checks", "boundary_probe": "boundary_checks", "pair_probe": "pair_checks",
          "rep_probe": "rep_checks", "gather_probe": "gather_checks", "xstate_probe": "xstate_checks",
          "context_probe": "context_checks"}


def probe_result(text: str, key: str, bits: int) -> dict:
    rows = [json.loads(line) for line in text.splitlines() if line.startswith('{"')]
    entries = [row for row in rows if "guest_entry" in row]
    results = [row for row in rows if key in row]
    if (len(entries) != 1 or entries[0].get("guest_entry") is not True or
            entries[0].get("pointer_bits") != bits or len(results) != 1 or
            results[0][key] != "PASS" or type(results[0].get("cases")) is not int or results[0]["cases"] <= 0):
        raise RuntimeError(f"missing or invalid {key} guest evidence")
    return results[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True, help="plain Wine-darwin ARM64/WoW64 runtime, not a game prefix")
    parser.add_argument("--build-output", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="fresh directory below build/local")
    parser.add_argument("--llvm-mingw", type=Path, default=Path("/opt/llvm-mingw-20260616-ucrt-macos-universal"))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--probe", nargs="+", choices=(*PROBES, "shared_data_probe", "callret_guard"),
                        help="run selected probes; default runs the complete suite")
    args = parser.parse_args()
    source, build, output = args.runtime.resolve(), args.build_output.resolve(), args.output.resolve()
    if (output.exists() or not output.is_relative_to(ROOT / "build/local") or
            output.is_relative_to(source) or source.is_relative_to(output) or args.timeout <= 0):
        parser.error("a fresh, disjoint output below build/local and a positive timeout are required")
    if any((source / name).exists() for name in ("system.reg", "user.reg", "dosdevices", "owned-graphics-runtime.json")):
        parser.error("use a plain Wine runtime, not a prefix or staged graphics runtime")
    output.mkdir(parents=True)
    result = {"status": "failed", "runtime_source": str(source), "build_output": str(build), "runs": []}
    runtime, prefix, images = output / "runtime", output / "prefix", output / "images"
    images.mkdir()
    prefix.mkdir()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("WINE", "DYLD_", "FEX_", "DXMT_", "D3DMETAL_", "GPTK_"))}
    server = None
    server_log = (output / "server.log").open("w")

    def run(command: list[str], name: str, env: dict | None = None) -> str:
        with (output / (name + ".log")).open("w") as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=True)
        return (output / (name + ".log")).read_text(errors="replace")

    try:
        run([sys.executable, str(ROOT / "Darwin/test_build.py"), "--build-output", str(build)], "build-audit")
        build_result = json.loads((build / "fex-result.json").read_text())
        result["fex_result_sha256"] = hashlib.sha256((build / "fex-result.json").read_bytes()).hexdigest()
        # APFS clone keeps this isolated without copying gigabytes of unchanged Wine files.
        run(["/bin/cp", "-cR", str(source), str(runtime)], "clone-runtime")
        result["fex"] = {}
        for name in ("xtajit64.dll", "xtajit.dll"):
            destination = runtime / "lib/wine/aarch64-windows" / name
            destination.unlink(missing_ok=True)
            shutil.copyfile(build / "dlls" / name, destination)
            digest = hashlib.sha256(destination.read_bytes()).hexdigest()
            if digest != build_result["artifacts"][name]["sha256"]:
                raise RuntimeError(f"staged {name} does not match the audited build")
            result["fex"][name] = digest
        observer = output / "observer.dylib"
        run(["/usr/bin/clang", "-arch", "arm64", "-dynamiclib", "-Wall", "-Wextra", "-Werror",
             str(ROOT / "Darwin/tests/fex_host_observer.c"), "-o", str(observer)], "observer-build")
        fixtures = []
        for arch, bits in (("x86_64", 64), ("i686", 32)):
            for probe in args.probe or (*PROBES, "shared_data_probe", "callret_guard"):
                if bits == 64 and probe == "boundary_probe":
                    continue
                if bits == 32 and probe in ("shared_data_probe", "callret_guard", "pair_probe", "gather_probe", "xstate_probe", "context_probe"):
                    continue
                executable = output / f"{arch}-{probe}.exe"
                command = [str(args.llvm_mingw / "bin" / (arch + "-w64-mingw32-clang++")),
                           "-std=c++20", "-O2", "-static", "-msse4.2", "-fno-vectorize", "-fno-slp-vectorize",
                           "-I" + str(ROOT / "FEXCore/include"), "-I" + str(ROOT / "External/fmt/include"),
                           str(ROOT / "Darwin/tests" / ("fex_" + probe + ".cpp")), "-o", str(executable)]
                if probe == "wine_user_probe":
                    command += ["-luser32", "-lgdi32"]


                run(command, executable.stem + "-build")
                fixtures.append((executable, bits, probe))
        loader = runtime / "bin/wine"
        environment |= {"WINEPREFIX": str(prefix), "WINEARCH": "win64", "WINEDEBUG": "+loaddll,+seh",
                        "WINELOADER": str(loader), "WINESERVER": str(runtime / "bin/wineserver"),
                        "WINEDLLPATH": str(runtime / "lib/wine"), "WINEDLLOVERRIDES": "mscoree,mshtml=",
                        "DYLD_INSERT_LIBRARIES": str(observer), "FEX_HOST_EVIDENCE": str(images),
                        "FEX_DISKCACHE": "0", "FEX_X87REDUCEDPRECISION": "0"}
        server = subprocess.Popen([str(runtime / "bin/wineserver"), "-f", "-p"], env=environment,
                                  stdout=server_log, stderr=subprocess.STDOUT)
        result["server_pid"] = server.pid
        for executable, bits, probe in fixtures:
            modes = (("default", "full") if probe == "smc_probe" else ("default", "vector_tso") if probe == "pair_probe"
                     else ("default", "memcpy_tso") if probe == "rep_probe"
                     else ("full", "reduced") if probe in ("x87_probe", "boundary_probe", "xstate_probe")
                     else ("cold", "warm") if probe == "disk_cache_lookup" else ("default",))
            for mode in modes:
                label = executable.stem + "-" + mode
                env = environment | {"FEX_X87REDUCEDPRECISION": "1" if mode == "reduced" else "0"}

                if probe == "smc_probe" and mode == "full":
                    env["FEX_SMCCHECKS"] = "full"
                if mode == "vector_tso":
                    env |= {"FEX_TSOENABLED": "1", "FEX_VECTORTSOENABLED": "1"}
                if probe == "rep_probe":
                    env |= {"FEX_TSOENABLED": "1", "FEX_MEMCPYSETTSOENABLED": "1" if mode == "memcpy_tso" else "0"}
                if probe == "disk_cache_lookup":
                    env |= {"FEX_DISKCACHE": "1", "FEX_DISKCACHEPATH": "C:\\fex-cache-" + str(bits) + "\\",
                            "FEX_DISKCACHEMEMORYSIZE": "4096", "FEX_DISKCACHEMAXFILESIZE": "65536"}
                row = {"probe": probe, "pointer_bits": bits, "mode": mode, "log": label + ".log"}
                result["runs"].append(row)
                with (output / row["log"]).open("w") as log:
                    with subprocess.Popen([str(loader), str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT) as process:
                        row["pid"] = process.pid
                        try:
                            process.wait(timeout=args.timeout)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            raise
                row["exit_code"] = process.returncode
                text = (output / row["log"]).read_text(errors="replace")
                if process.returncode:
                    raise RuntimeError(f"{label}: exit {process.returncode}; inspect {row['log']}")
                if probe in PROBES:
                    row["result"] = probe_result(text, PROBES[probe], bits)
                elif probe == "shared_data_probe":
                    if text.splitlines().count("FEX_SHARED_DATA_ENTER") != 1 or not re.search(r"^PASS: fixed-address .*read-only AV; Windows \d+\.\d+ build \d+$", text, re.M):
                        raise RuntimeError("shared-data guest probe did not complete")
                elif text.splitlines().count("FEX_CALLRET_GUARD_PASS lower=reserved16k data=rw upper=reserved16k") != 1:
                    raise RuntimeError("call/return guard guest probe did not complete")
                name = "xtajit64.dll" if bits == 64 else "xtajit.dll"
                if not re.search(r'Loaded L"C:\\\\windows\\\\system32\\\\' + re.escape(name) + r'".*: builtin', text, re.I):
                    raise RuntimeError(f"{label}: no loaded {name} evidence")
                installed = prefix / "drive_c/windows/system32" / name
                row["loaded_fex_sha256"] = hashlib.sha256(installed.read_bytes()).hexdigest()
                if row["loaded_fex_sha256"] != result["fex"][name]:
                    raise RuntimeError(f"{label}: loaded FEX file differs from the audited DLL")
                evidence = (images / f"{process.pid}.images").read_text().splitlines()
                process_rows = [line.split("\t", 3) for line in evidence if line.startswith("PROCESS\t")]
                native_rows = [line.split("\t", 3) for line in evidence if line.startswith("IMAGE\t")]
                if (not process_rows or any(item[2] != "0" for item in process_rows) or not native_rows or
                        any(int(item[2]) != 0x100000c for item in native_rows) or
                        not {"wine", "ntdll.so"}.issubset({Path(item[3]).name for item in native_rows})):
                    raise RuntimeError(f"{label}: native ARM64 Wine execution not proven")
                row["native_arm64"] = True
                row["status"] = "pass"
                print(f"PASS {label}", flush=True)
        result["status"] = "pass"
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    finally:
        if server is not None:
            try:
                cleanup = environment.copy()
                cleanup.pop("DYLD_INSERT_LIBRARIES", None)
                run([str(runtime / "bin/wineserver"), "-k"], "server-stop", cleanup)
                server.wait(timeout=args.timeout)
                run([str(runtime / "bin/wineserver"), "-w"], "server-wait", cleanup)
                shutil.rmtree(prefix)
                result["prefix_cleaned"] = True
            except (OSError, subprocess.SubprocessError) as error:
                result["status"] = "failed"
                result["cleanup_error"] = str(error)
        server_log.close()
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "runs"}, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
