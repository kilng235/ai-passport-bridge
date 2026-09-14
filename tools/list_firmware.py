"""List the available pre-built firmware artifacts with size and SHA-256."""
from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CANDIDATES = [
    "build/FoloToy-AI-Passport-full.bin",
    "build/FoloToy-AI-Passport.bin",
    "build/merged-binary.bin",
    "build/partition_table/partition-table.bin",
    "build/bootloader/bootloader.bin",
]


def main() -> int:
    print(f"{'File':<48s}{'Size':>12s}    SHA-256")
    print("-" * 120)
    found = 0
    for rel in CANDIDATES:
        p = ROOT / rel
        if not p.is_file():
            continue
        found += 1
        size = p.stat().st_size
        sha = hashlib.sha256(p.read_bytes()).hexdigest()
        human = f"{size/1024:.1f} KB" if size < 1024 * 1024 else f"{size/(1024*1024):.2f} MB"
        print(f"{rel:<48s}{human:>12s}    {sha}")
    print(f"\n{found} firmware artifact(s) found.")
    return 0 if found else 1


if __name__ == "__main__":
    raise SystemExit(main())
