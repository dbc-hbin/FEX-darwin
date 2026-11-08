#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pydantic>=2,<3"]
# ///
# --- How to run ---
# Install uv if needed: curl -LsSf https://astral.sh/uv/install.sh | sh
# Run: uv run Darwin/prepare.py [source-directory]

"""Fetch the pinned FEX source and apply the Darwin patch queue."""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

from pydantic import BaseModel


class Upstream(BaseModel, frozen=True):
    repository: str
    tag: str
    commit: str


class Patch(BaseModel, frozen=True):
    path: str
    sha256: str


class Provenance(BaseModel, frozen=True):
    upstream: Upstream
    series: str
    patchDirectory: str
    patches: list[Patch]
    appliedSourceTree: str


def main() -> None:
    """Prepare a new source directory without modifying an existing checkout."""
    root = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 2:
        sys.exit("Usage: uv run Darwin/prepare.py [source-directory]")
    source = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) == 2
        else root / "build/local/source"
    )
    if source.exists():
        sys.exit(f"Source directory already exists: {source}")
    provenance = Provenance.model_validate_json(
        (root / "Darwin/provenance.json").read_text(encoding="utf-8")
    )
    series = (root / provenance.series).read_text(encoding="utf-8").splitlines()
    checksums = {entry.path: entry.sha256 for entry in provenance.patches}
    for entry in series:
        patch = root / provenance.patchDirectory / entry
        if hashlib.sha256(patch.read_bytes()).hexdigest() != checksums[entry]:
            sys.exit(f"Patch checksum mismatch: {entry}")
    source.parent.mkdir(parents=True, exist_ok=True)
    _ = subprocess.run(
        [
            "git",
            "clone",
            "--depth=1",
            f"--branch={provenance.upstream.tag}",
            provenance.upstream.repository,
            str(source),
        ],
        check=True,
    )
    revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if revision != provenance.upstream.commit:
        sys.exit(f"Upstream revision mismatch: {revision}")
    for entry in series:
        _ = subprocess.run(
            [
                "patch",
                "-d",
                str(source),
                "-p1",
                "-F0",
                "--batch",
                "--no-backup-if-mismatch",
                "-i",
                str(root / provenance.patchDirectory / entry),
            ],
            check=True,
        )
    _ = subprocess.run(["git", "-C", str(source), "add", "--all"], check=True)
    tree = subprocess.run(
        ["git", "-C", str(source), "write-tree"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if tree != provenance.appliedSourceTree:
        sys.exit(f"Patched source tree mismatch: {tree}")
    print(f"Prepared {source}: {len(series)} patches, tree {tree}")


if __name__ == "__main__":
    main()
