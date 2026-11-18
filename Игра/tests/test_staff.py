#!/usr/bin/env python3
"""Offline command regression tests; no running game server is required."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sqlite3
import tempfile

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "staff_admin.py"
spec = importlib.util.spec_from_file_location("veltor_staff_admin", SCRIPT)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as temp:
    db = Path(temp) / "veltor.db"
    with sqlite3.connect(db) as conn:
        conn.execute("CREATE TABLE accounts(name TEXT PRIMARY KEY COLLATE NOCASE)")
        conn.execute("INSERT INTO accounts(name) VALUES('Test_Player')")
    assert module.set_staff(db, "test_player", True) == "Test_Player"
    assert module.set_staff(db, "Test_Player", True) == "Test_Player"
    with sqlite3.connect(db) as conn:
        assert conn.execute("SELECT COUNT(*) FROM staff").fetchone()[0] == 1
    assert module.set_staff(db, "TEST_PLAYER", False) == "Test_Player"
    with sqlite3.connect(db) as conn:
        assert conn.execute("SELECT COUNT(*) FROM staff").fetchone()[0] == 0
    for invalid in ["' OR 1=1--", "", "A" * 40, "wrong name"]:
        try:
            module.set_staff(db, invalid, True)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Invalid username accepted: {invalid!r}")
    try:
        module.set_staff(db, "NoSuchAccount", True)
    except ValueError:
        pass
    else:
        raise AssertionError("Nonexistent account gained staff access")
    print("PASS: staff provisioning requires an existing account")
    print("PASS: roles are case-insensitive and idempotent")
    print("PASS: unknown and unsafe usernames are rejected")
