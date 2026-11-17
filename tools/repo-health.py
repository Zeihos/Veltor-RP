#!/usr/bin/env python3
"""Check repository safety invariants and local README links."""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = pathlib.Path(__file__).resolve().parents[1]
REQUIRED = [
    "README.md", "Сайт/index.php", "Сайт/install.php",
    "Форум/index.php", "Форум/install.php",
    "Оформление/ava.jpg", "Оформление/cover.jpg",
    "Сайт/app/.htaccess", "Сайт/storage/.htaccess",
    "Форум/app/.htaccess", "Форум/storage/.htaccess",
]
BAD_SUFFIXES = {".sqlite", ".sqlite3", ".db", ".sql", ".rar", ".zip", ".7z", ".amx", ".exe", ".dll", ".so", ".log"}
errors: list[str] = []

for item in REQUIRED:
    if not (ROOT / item).is_file():
        errors.append(f"Required file missing: {item}")

result = subprocess.run(
    ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
)
for raw in result.stdout.split(b"\0"):
    if not raw:
        continue
    name = raw.decode("utf-8", "replace")
    p = pathlib.PurePosixPath(name)
    lower = name.casefold()
    if p.suffix.casefold() in BAD_SUFFIXES:
        errors.append(f"Binary, database or archive committed: {name}")
    if any(x == ".env" or x.startswith(".env.") for x in p.parts):
        errors.append(f"Environment file committed: {name}")
    if "/storage/" in f"/{lower}" and p.name not in {".htaccess", "index.php"}:
        errors.append(f"Runtime storage file committed: {name}")

readme = (ROOT / "README.md").read_text(encoding="utf-8")
targets = re.findall(r"\]\((\.[^) ]+)\)", readme) + re.findall(
    r'(?:href|src)="(\.[^"]+)"', readme
)
for target in targets:
    path = unquote(target.split("#")[0].split("?")[0])
    if not path:
        continue
    full = (ROOT / path).resolve()
    if not full.is_relative_to(ROOT) or not full.exists():
        errors.append(f"Broken README link: {target}")

if errors:
    for problem in errors:
        print("FAIL:", problem, file=sys.stderr)
    sys.exit(1)
print(f"PASS: repository files and {len(targets)} README references checked.")
