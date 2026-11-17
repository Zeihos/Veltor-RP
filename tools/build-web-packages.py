#!/usr/bin/env python3
"""Build minimal website and forum installation ZIPs from tracked source files."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
APPS = {
    "Сайт": "VELTOR-SITE.zip",
    "Форум": "VELTOR-FORUM.zip",
}
REQUIRED = {
    ".htaccess",
    "index.php",
    "install.php",
    "app/.htaccess",
    "app/core.php",
    "storage/.htaccess",
    "storage/index.php",
}


def permitted(relative: pathlib.PurePosixPath) -> bool:
    p = relative.as_posix()
    parts = relative.parts
    if p in (".htaccess", "index.php", "install.php"):
        return True
    if parts[0] == "app" and len(parts) == 2:
        return relative.suffix == ".php" or p == "app/.htaccess"
    if parts[0] == "storage":
        return p in ("storage/.htaccess", "storage/index.php")
    if len(parts) >= 3 and parts[0] == "assets":
        if parts[1] == "css":
            return relative.suffix == ".css"
        if parts[1] == "js":
            return relative.suffix == ".js"
        if parts[1] == "images":
            return relative.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp", ".svg", ".ico"}
    return False


def collect(folder: pathlib.Path) -> dict[str, pathlib.Path]:
    result: dict[str, pathlib.Path] = {}
    for src in sorted(folder.rglob("*")):
        if not src.is_file():
            continue
        if src.is_symlink():
            raise ValueError(f"Symbolic link not allowed: {src}")
        rel = pathlib.PurePosixPath(src.relative_to(folder).as_posix())
        if permitted(rel):
            result[rel.as_posix()] = src
    missing = REQUIRED.difference(result)
    if missing:
        raise ValueError(f"{folder.name}: missing deployment files: {sorted(missing)}")
    if len(result) < 10:
        raise ValueError(f"{folder.name}: unexpectedly few files ({len(result)})")
    return result


def create_zip(zip_path: pathlib.Path, members: dict[str, pathlib.Path]) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(members.items()):
            # Stable timestamps and ordering: reproducible archives for a given revision.
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def verify_zip(zip_path: pathlib.Path, members: dict[str, pathlib.Path]) -> None:
    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError(f"{zip_path.name}: duplicate names")
        if set(names) != set(members):
            raise ValueError(f"{zip_path.name}: unexpected or missing files")
        if archive.testzip() is not None:
            raise ValueError(f"{zip_path.name}: corrupt archive")
        for name, source in members.items():
            if archive.read(name) != source.read_bytes():
                raise ValueError(f"{zip_path.name}: differs from {name}")
    print(f"PASS {zip_path.name}: {len(members)} tracked deployment files; sha256 "
          f"{hashlib.sha256(zip_path.read_bytes()).hexdigest()}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=".dist", help="Output directory")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    output = ROOT / args.output
    if not args.verify_only:
        output.mkdir(parents=True, exist_ok=True)
    for folder, archive_name in APPS.items():
        members = collect(ROOT / folder)
        target = output / archive_name
        if not args.verify_only:
            create_zip(target, members)
        verify_zip(target, members)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
