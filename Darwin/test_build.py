#!/usr/bin/env python3
"""Check real built modules, provenance, PE rejection, and fresh-output enforcement."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

SPEC = importlib.util.spec_from_file_location("build_owned_fex_windows", Path(__file__).with_name("build.py"))
assert SPEC and SPEC.loader
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)
ROOT = Path(__file__).resolve().parents[1]


class FEXBuildTests(unittest.TestCase):
    def test_real_modules_and_provenance(self) -> None:
        result = json.loads((OUTPUT / "fex-result.json").read_text())
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["source_commit"], builder.COMMIT)
        self.assertEqual(result["host"]["proc_translated"], "0")
        self.assertEqual(result["host"]["machine"], "arm64")
        self.assertEqual({item["path"] for item in result["submodules"]}, set(builder.SUBMODULES))
        self.assertEqual({item["path"] for item in result["licenses"]}, set(builder.LICENSES))
        provenance = json.loads((ROOT / "Darwin/provenance.json").read_text())
        self.assertEqual(hashlib.sha256((ROOT / "Darwin/provenance.json").read_bytes()).hexdigest(), result["provenance_sha256"])
        self.assertEqual({item["path"] for item in result["source_files"]}, {item["path"] for item in provenance["sources"]})
        for item in result["source_files"]:
            self.assertEqual(hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest(), item["sha256"])
        self.assertEqual(result["source_patches"], [{"path": item["path"], "sha256": item["sha256"]} for item in provenance["patches"]])
        for item in provenance["patches"]:
            self.assertEqual(hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest(), item["sha256"])
        for item in result["licenses"]:
            self.assertEqual(hashlib.sha256((OUTPUT / "licenses" / item["path"].replace("/", "__")).read_bytes()).hexdigest(), item["sha256"])
        marker = (SOURCE / "Source/Windows/wine_builtin.bin").read_bytes()
        for variant, _, target, directory, wine_name in builder.VARIANTS:
            with self.subTest(module=wine_name):
                data = (OUTPUT / "dlls" / wine_name).read_bytes()
                report = (OUTPUT / "logs" / (variant + "-pe-audit.log")).read_text()
                definition = (SOURCE / "Source/Windows" / directory / ("lib" + target + ".def")).read_text()
                actual = builder.audit(data, report, definition, marker, variant == "arm64ec")
                for key, value in actual.items():
                    self.assertEqual(value, result["artifacts"][wine_name][key])
                self.assertEqual(data, (OUTPUT / variant / "Bin" / ("lib" + target + ".dll")).read_bytes())

    def test_rejects_wrong_architecture_marker_exports_imports_and_chpe(self) -> None:
        data = (OUTPUT / "dlls/xtajit64.dll").read_bytes()
        report = (OUTPUT / "logs/arm64ec-pe-audit.log").read_text()
        definition = (SOURCE / "Source/Windows/ARM64EC/libarm64ecfex.def").read_text()
        marker = (SOURCE / "Source/Windows/wine_builtin.bin").read_bytes()
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        arm64 = bytearray(data)
        struct.pack_into("<H", arm64, pe + 4, 0xaa64)
        executable = bytearray(data)
        struct.pack_into("<H", executable, pe + 22, 0x22)
        missing_marker = bytearray(data)
        missing_marker[64] ^= 1
        small_pages = bytearray(data)
        struct.pack_into("<I", small_pages, pe + 56, 4096)
        cases = ((bytes(arm64), report), (bytes(executable), report), (bytes(missing_marker), report),
                 (bytes(small_pages), report),
                 (data, report.replace("Format: COFF-ARM64EC", "Format: COFF-x86-64")),
                 (data, report.replace("Name: ProcessInit", "Name: MissingExport")),
                 (data, report.replace("Name: ntdll.dll", "Name: kernel32.dll")),
                 (data, report.replace("CHPEMetadataPointer:", "MissingCHPEPointer:")),
                 (data, report.replace("  ARM64EC", "  MissingCodeMap")))
        for index, (image, metadata) in enumerate(cases):
            with self.subTest(case=index), self.assertRaises(RuntimeError):
                builder.audit(image, metadata, definition, marker, True)

    def test_rejects_output_reuse_without_writes(self) -> None:
        before = (OUTPUT / "fex-result.json").read_bytes()
        process = subprocess.run([sys.executable, str(ROOT / "Darwin/build.py"),
                                  "--output", str(OUTPUT)], capture_output=True, text=True)
        self.assertEqual(process.returncode, 2)
        self.assertIn("fresh output", process.stderr)
        self.assertEqual((OUTPUT / "fex-result.json").read_bytes(), before)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-output", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    OUTPUT = args.build_output.resolve()
    SOURCE = ROOT
    unittest.main(argv=[sys.argv[0], *remaining])
