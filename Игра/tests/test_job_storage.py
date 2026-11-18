#!/usr/bin/env python3
"""SQLite invariants for the route-job data layout, independent from game clients.

This checks the SQL contract and rollback semantics; it is NOT a Pawn gameplay test.
"""
from pathlib import Path
import sqlite3
import tempfile

PWN = Path(__file__).resolve().parents[1] / "include" / "veltor_jobs.inc"
MOD = Path(__file__).resolve().parents[1] / "gamemodes" / "veltor.pwn"
source = PWN.read_text(encoding="utf-8")
gamemode = MOD.read_text(encoding="utf-8")

for required in [
    "BEGIN IMMEDIATE TRANSACTION", "COMMIT", "ROLLBACK",
    "INSERT OR REPLACE INTO work_cooldown",
    "INSERT OR REPLACE INTO job_progress",
    "AtJobLocation(playerid, false)", "AtJobLocation(playerid, true)",
    "JOB_MIN_TRAVEL", "JobLevel(playerid)",
]:
    assert required in source, f"Missing job safety invariant: {required}"
assert '#include "veltor_jobs.inc"' in gamemode
assert 'if (HandleJobCommand(playerid, cmdtext)) return 1;' in gamemode
assert '"CREATE TABLE IF NOT EXISTS job_progress' in source

with tempfile.TemporaryDirectory() as tmp:
    db = Path(tmp) / "test.db"
    with sqlite3.connect(db) as conn:
        conn.execute("CREATE TABLE accounts (name TEXT PRIMARY KEY COLLATE NOCASE, money INTEGER, job INTEGER)")
        conn.execute("CREATE TABLE work_cooldown (name TEXT PRIMARY KEY COLLATE NOCASE, last_work INTEGER)")
        conn.execute("CREATE TABLE job_progress (name TEXT PRIMARY KEY COLLATE NOCASE, shifts INTEGER, xp INTEGER)")
        conn.execute("INSERT INTO accounts VALUES ('Player_1',2500,1)")
        # SQL equivalent of successful shift completion in veltor_jobs.inc
        conn.execute("BEGIN IMMEDIATE")
        conn.execute("UPDATE accounts SET money=?,job=? WHERE name=?", (2900, 1, "Player_1"))
        conn.execute("INSERT OR REPLACE INTO work_cooldown(name,last_work) VALUES(?,?)", ("Player_1", 1000))
        conn.execute("INSERT OR REPLACE INTO job_progress(name,shifts,xp) VALUES(?,?,?)", ("Player_1",1,20))
        conn.commit()
        assert conn.execute("SELECT money FROM accounts").fetchone()[0] == 2900
        assert conn.execute("SELECT last_work FROM work_cooldown").fetchone()[0] == 1000
        assert conn.execute("SELECT shifts,xp FROM job_progress").fetchone() == (1,20)

        # A failed transaction must not partially credit money or XP.
        try:
            conn.execute("BEGIN IMMEDIATE")
            conn.execute("UPDATE accounts SET money=99999 WHERE name='Player_1'")
            conn.execute("INSERT INTO job_progress(name,shifts,xp) VALUES('Player_1',1,20)")
            conn.commit()
            raise AssertionError("Duplicate progress write unexpectedly succeeded")
        except sqlite3.IntegrityError:
            conn.rollback()
        assert conn.execute("SELECT money FROM accounts").fetchone()[0] == 2900
        assert conn.execute("SELECT shifts,xp FROM job_progress").fetchone() == (1,20)

print("PASS: job payout and progress stored together in SQLite")
print("PASS: transaction rollback prevents partial reward updates")
print("PASS: game source checks location, shift timing and progression")
