#!/usr/bin/env python3
"""Consistent local SQLite backups for VELTOR ROLEPLAY.

Use SQLite's online backup API: copying veltor.db directly can omit WAL records.
Backup files contain private player data; never expose this directory on the web.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
from datetime import datetime, timezone
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "server" / "scriptfiles" / "veltor.db"
DEFAULT_OUTPUT = ROOT / "server" / "backups"


def backup_database(source: Path, output: Path, retain: int = 7) -> Path:
    source = source.expanduser()
    if source.is_symlink():
        raise ValueError("Символические ссылки на базу данных не поддерживаются.")
    source = source.resolve(strict=True)
    if not source.is_file():
        raise ValueError("Источник должен быть существующей базой SQLite.")
    if not 1 <= retain <= 365:
        raise ValueError("Параметр --retain должен быть в пределах от 1 до 365.")
    output = output.expanduser()
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    output = output.resolve(strict=True)
    if output == source.parent:
        raise ValueError("Резервные копии должны находиться в отдельном каталоге.")
    try:
        os.chmod(output, 0o700)
    except PermissionError as exc:
        raise ValueError("Невозможно защитить папку резервных копий правами 0700.") from exc

    timestamp = datetime.now(timezone.utc)
    filename = timestamp.strftime("veltor-%Y%m%dT%H%M%S%fZ.sqlite3")
    final = output / filename
    fd, temporary_name = tempfile.mkstemp(prefix=".backup-", suffix=".tmp", dir=output)
    os.close(fd)
    temporary = Path(temporary_name)
    manifest = final.with_suffix(final.suffix + ".json")
    source_conn = destination_conn = None
    try:
        uri = "file:" + quote(str(source), safe="/") + "?mode=ro"
        source_conn = sqlite3.connect(uri, uri=True, timeout=10)
        destination_conn = sqlite3.connect(temporary, timeout=10)
        source_conn.backup(destination_conn, pages=100, sleep=0.1)
        destination_conn.close()
        destination_conn = None

        destination_conn = sqlite3.connect(
            "file:" + quote(str(temporary), safe="/") + "?mode=ro",
            uri=True,
            timeout=10,
        )
        check = destination_conn.execute("PRAGMA integrity_check").fetchone()
        if check is None or check[0] != "ok":
            raise ValueError("Проверка целостности резервной копии не пройдена.")
        accounts = destination_conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='accounts'"
        ).fetchone()
        if not accounts:
            raise ValueError("В резервной копии отсутствует таблица accounts.")
        destination_conn.close()
        destination_conn = None

        checksum = hashlib.sha256(temporary.read_bytes()).hexdigest()
        os.chmod(temporary, 0o600)
        temporary.replace(final)

        metadata = {
            "project": "VELTOR ROLEPLAY",
            "created_at_utc": timestamp.isoformat(),
            "filename": filename,
            "size_bytes": final.stat().st_size,
            "sha256": checksum,
            "sqlite_integrity": "ok",
        }
        # Sidecar contains no user records; keep it private anyway.
        manifest.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.chmod(manifest, 0o600)

        # Only prune backups produced by this tool; never touch foreign files.
        prior = sorted(output.glob("veltor-*.sqlite3"), reverse=True)
        for old in prior[retain:]:
            old.unlink()
            old.with_suffix(old.suffix + ".json").unlink(missing_ok=True)
        return final
    finally:
        if source_conn is not None:
            source_conn.close()
        if destination_conn is not None:
            destination_conn.close()
        temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Безопасная резервная копия SQLite VELTOR ROLEPLAY")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help="Путь к veltor.db")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Приватная папка для копий")
    parser.add_argument("--retain", type=int, default=7, help="Сколько последних копий хранить (1–365)")
    args = parser.parse_args(argv)
    try:
        saved = backup_database(args.db, args.output, args.retain)
    except (OSError, ValueError, sqlite3.Error) as exc:
        print(f"Ошибка резервного копирования: {exc}", file=sys.stderr)
        return 1
    print(f"Резервная копия создана и проверена: {saved}")
    print(f"Контрольная сумма: {saved.name}.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
