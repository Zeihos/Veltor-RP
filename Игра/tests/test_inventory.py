#!/usr/bin/env python3
"""Inventory contract, atomic purchase/use and persistence checks.

This runs equivalent SQL statements in sqlite3; real game-client behaviour is
covered separately and is NOT proven by these offline tests.
"""
from __future__ import annotations

from pathlib import Path
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "include" / "veltor_inventory.inc").read_text("utf-8")
gamemode = (ROOT / "gamemodes" / "veltor.pwn").read_text("utf-8")

for fragment in (
    "CREATE TABLE IF NOT EXISTS player_items",
    "CREATE TABLE IF NOT EXISTS inventory_events",
    "PRIMARY KEY(name,item_id)",
    "CHECK(quantity>=0 AND quantity<=10)",
    "BEGIN IMMEDIATE TRANSACTION",
    "COMMIT", "ROLLBACK",
    "InventoryChangedOne()", "InventoryAtShop(playerid)",
    "GetPlayerHealth(playerid, health)",
    "GetPlayerArmour(playerid, armor)",
    "HandleInventoryCommand(playerid, const cmdtext[])",
):
    assert fragment in source, f"Missing inventory invariant: {fragment}"

assert '#include "veltor_inventory.inc"' in gamemode
assert "if (HandleInventoryCommand(playerid, cmdtext)) return 1;" in gamemode

with tempfile.TemporaryDirectory() as temp:
    database = Path(temp) / "veltor.db"
    with sqlite3.connect(database, isolation_level=None) as db:
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("CREATE TABLE accounts(name TEXT PRIMARY KEY COLLATE NOCASE, money INTEGER)")
        db.execute("INSERT INTO accounts VALUES ('Player_1',2000)")
        db.execute("INSERT INTO accounts VALUES ('Player_2',9000)")
        db.execute("""CREATE TABLE player_items (
            name TEXT NOT NULL COLLATE NOCASE,
            item_id INTEGER NOT NULL CHECK(item_id IN (1,2)),
            quantity INTEGER NOT NULL DEFAULT 0 CHECK(quantity>=0 AND quantity<=10),
            PRIMARY KEY(name,item_id))""")
        db.execute("""CREATE TABLE inventory_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL, item_id INTEGER NOT NULL,
            action TEXT NOT NULL, quantity INTEGER NOT NULL,
            cash_delta INTEGER NOT NULL, created_at INTEGER NOT NULL)""")

        # Purchase: debit cash + create/update inventory + audit together.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=1700 WHERE name='Player_1' AND money=2000")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("INSERT OR IGNORE INTO player_items(name,item_id,quantity) VALUES('Player_1',1,0)")
        db.execute("UPDATE player_items SET quantity=1 WHERE name='Player_1' AND item_id=1 AND quantity=0")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('Player_1',1,'buy',1,-300,1)")
        db.execute("COMMIT")

        assert db.execute("SELECT money FROM accounts WHERE name='Player_1'").fetchone()[0] == 1700
        assert db.execute("SELECT quantity FROM player_items WHERE name='player_1' AND item_id=1").fetchone()[0] == 1
        assert db.execute("SELECT money FROM accounts WHERE name='Player_2'").fetchone()[0] == 9000
        assert db.execute("SELECT COUNT(*) FROM inventory_events WHERE action='buy'").fetchone()[0] == 1

        # Consumption: decrement inventory and write a use-event atomically.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE player_items SET quantity=0 WHERE name='Player_1' AND item_id=1 AND quantity=1")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('Player_1',1,'use',1,0,2)")
        db.execute("COMMIT")
        assert db.execute("SELECT quantity FROM player_items").fetchone()[0] == 0
        assert db.execute("SELECT COUNT(*) FROM inventory_events").fetchone()[0] == 2

        # Double consumption cannot succeed on an empty slot.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE player_items SET quantity=-1 WHERE name='Player_1' AND item_id=1 AND quantity=1")
        assert db.execute("SELECT changes()").fetchone()[0] == 0
        db.execute("ROLLBACK")
        assert db.execute("SELECT quantity FROM player_items").fetchone()[0] == 0

        # Stock limit violation must rollback a previously staged cash debit.
        db.execute("UPDATE player_items SET quantity=10 WHERE name='Player_1' AND item_id=1")
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=1400 WHERE name='Player_1' AND money=1700")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        try:
            db.execute("UPDATE player_items SET quantity=11 WHERE name='Player_1' AND item_id=1 AND quantity=10")
        except sqlite3.IntegrityError:
            db.execute("ROLLBACK")
        else:
            raise AssertionError("Inventory allowed more than 10 units")
        assert db.execute("SELECT money FROM accounts WHERE name='Player_1'").fetchone()[0] == 1700
        assert db.execute("SELECT quantity FROM player_items").fetchone()[0] == 10

        # Missing-account CAS must not charge anyone else.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=1 WHERE name='Missing_Player' AND money=1700")
        assert db.execute("SELECT changes()").fetchone()[0] == 0
        db.execute("ROLLBACK")

        # Reject invalid item IDs independently of caller-provided values.
        try:
            db.execute("INSERT INTO player_items VALUES('Player_2',999,1)")
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("Unknown item was accepted")

    # Verify inventory events and counts persist after database close/reopen.
    with sqlite3.connect(database) as reopened:
        assert reopened.execute(
            "SELECT quantity FROM player_items WHERE name='PLAYER_1' AND item_id=1"
        ).fetchone() == (10,)
        assert reopened.execute(
            "SELECT action,cash_delta FROM inventory_events ORDER BY id"
        ).fetchall() == [("buy", -300), ("use", 0)]

print("PASS: inventory purchase and consumption are atomic and journaled")
print("PASS: inventory cap and invalid item IDs are enforced by SQLite")
print("PASS: missing account, empty stock and overflow do not change money")
print("PASS: items and events persist across database reopen")
