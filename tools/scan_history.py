"""Scan the git history of this repo for leaked credentials."""
from __future__ import annotations
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PATTERNS = {
    "DeepSeek": r"sk-[a-f0-9]{30,}",
    "MiniMax":  r"sk-cp-[A-Za-z0-9_]{30,}",
    "GitHub":   r"(?:ghp_|github_pat_|gho_)[A-Za-z0-9_]{20,}",
    "AWS":      r"AKIA[0-9A-Z]{16}",
    "RSA/EC Key": r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
}
COMPILED = [(name, re.compile(pat)) for name, pat in PATTERNS.items()]

SKIP_SUFFIX = {".png", ".jpg", ".jpeg", ".gif", ".bin", ".elf", ".map",
               ".ttf", ".otf", ".zip", ".tar.gz"}


def iter_tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "log", "--all", "--pretty=format:", "--name-only"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    )
    return sorted({line for line in out.stdout.splitlines() if line.strip()})


def main() -> int:
    files = iter_tracked_files()
    found = 0
    for rel in files:
        p = ROOT / rel
        if not p.is_file() or p.suffix.lower() in SKIP_SUFFIX:
            continue
        if p.stat().st_size > 2 * 1024 * 1024:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for name, pat in COMPILED:
            for m in pat.finditer(text):
                snippet = text[max(0, m.start() - 20): m.end() + 20].replace("\n", " ")
                print(f"  LEAK: {rel}: type={name}: ...{snippet}...")
                found += 1

    print(f"\nScan complete: {found} potential leak(s) across {len(files)} tracked files.")
    return 0 if found == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
