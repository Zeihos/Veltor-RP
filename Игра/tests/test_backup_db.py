#!/usr/bin/env python3
"""End-to-end SQLite online backup test, including uncheckpointed WAL writes."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import hashlib
import sqlite3
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / "tools" / "backup_db.py"
spec = importlib.util.spec_from_file_location("veltor_backup_db", SOURCE)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    source = root / "veltor.db"
    archive = root / "private-backups"
    with sqlite3.connect(source) as db:
        db.execute("PRAGMA journal_mode=WAL")
        db.execute("CREATE TABLE accounts (name TEXT PRIMARY KEY COLLATE NOCASE, money INTEGER)")
        db.execute("CREATE TABLE bank_accounts (name TEXT PRIMARY KEY COLLATE NOCASE, balance INTEGER)")
        db.execute("INSERT INTO accounts VALUES('Test_Player',13579)")
        db.execute("INSERT INTO bank_accounts VALUES('Test_Player',98765)")
        db.commit()

        # Keep the source open to ensure a raw-file copy would be unreliable in WAL mode.
        first = module.backup_database(source, archive, retain=2)
        assert first.exists()
        meta = json.loads(first.with_suffix(first.suffix + ".json").read_text("utf-8"))
        assert meta["sha256"] == hashlib.sha256(first.read_bytes()).hexdigest()
        assert meta["sqlite_integrity"] == "ok"
        assert meta["size_bytes"] == first.stat().st_size
        assert not any("Test_Player" in str(val) for val in meta.values())
        with sqlite3.connect(first) as restored:
            assert restored.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            assert restored.execute("SELECT money FROM accounts").fetchone()[0] == 13579
            assert restored.execute("SELECT balance FROM bank_accounts").fetchone()[0] == 98765

        db.execute("UPDATE accounts SET money=54321")
        db.commit()
        second = module.backup_database(source, archive, retain=2)
        third = module.backup_database(source, archive, retain=2)
        assert len(list(archive.glob("veltor-*.sqlite3"))) == 2
        assert not first.exists()
        assert not first.with_suffix(first.suffix + ".json").exists()
        for item in (second, third):
            assert item.exists()
        with sqlite3.connect(third) as restored:
            assert restored.execute("SELECT money FROM accounts").fetchone()[0] == 54321

    try:
        module.backup_database(root / "missing.db", archive)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("Missing source database was accepted")

    try:
        module.backup_database(source, archive, retain=0)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid retain value was accepted")

print("PASS: live SQLite WAL backup preserves cash and bank records")
print("PASS: integrity and SHA-256 manifest verified")
print("PASS: retention deletes only old VELTOR copies")
print("PASS: missing source and invalid retain arguments rejected")
