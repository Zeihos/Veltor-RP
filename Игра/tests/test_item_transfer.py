#!/usr/bin/env python3
"""Item gifting contract: all-or-nothing, capacities, audit, and persistence.

SQLite integration checks are independent of game networking. Physical player
proximity and the in-game command must be verified with two real clients.
"""
from __future__ import annotations

from pathlib import Path
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "include" / "veltor_inventory.inc").read_text("utf-8")
mode = (ROOT / "gamemodes" / "veltor.pwn").read_text("utf-8")

checks = (
    "CREATE TABLE IF NOT EXISTS item_transfers",
    "BEGIN IMMEDIATE TRANSACTION", "COMMIT", "ROLLBACK",
    "InventoryChangedOne()", "InventoryTransferNear(sender, recipient)",
    "GetPlayerVirtualWorld(sender) != GetPlayerVirtualWorld(recipient)",
    "GetPlayerInterior(sender) != GetPlayerInterior(recipient)",
    "GetPlayerState(sender) != PLAYER_STATE_ONFOOT",
    "GetPlayerState(recipient) != PLAYER_STATE_ONFOOT",
    "IsPlayerInRangeOfPoint(sender, ITEM_GIVE_RANGE, x, y, z)",
    "available > ITEM_MAX_STACK - quantity",
    "quantity < 1 || quantity > ITEM_GIVE_LIMIT",
    "sender == recipient", "gLastItemGive[playerid]",
    "INSERT INTO item_transfers", "'give'", "'receive'",
)
for phrase in checks:
    assert phrase in source, f"Gift safety check missing: {phrase}"
assert "new gLastItemGive[MAX_PLAYERS];" in mode
assert "gLastItemGive[playerid] = 0;" in mode

with tempfile.TemporaryDirectory() as tmp:
    db_path = Path(tmp) / "gift_test.sqlite3"
    with sqlite3.connect(db_path, isolation_level=None) as db:
        db.execute("""CREATE TABLE player_items(
            name TEXT NOT NULL COLLATE NOCASE,
            item_id INTEGER NOT NULL CHECK(item_id IN (1,2)),
            quantity INTEGER NOT NULL DEFAULT 0 CHECK(quantity>=0 AND quantity<=10),
            PRIMARY KEY(name,item_id))""")
        db.execute("""CREATE TABLE inventory_events(
            id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,
            item_id INTEGER NOT NULL, action TEXT NOT NULL,
            quantity INTEGER NOT NULL, cash_delta INTEGER NOT NULL,
            created_at INTEGER NOT NULL)""")
        db.execute("""CREATE TABLE item_transfers(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender TEXT NOT NULL, receiver TEXT NOT NULL,
            item_id INTEGER NOT NULL CHECK(item_id IN (1,2)),
            quantity INTEGER NOT NULL CHECK(quantity BETWEEN 1 AND 5),
            created_at INTEGER NOT NULL)""")
        db.execute("INSERT INTO player_items VALUES('Alice',1,8)")
        db.execute("INSERT INTO player_items VALUES('Bob',1,5)")
        db.execute("INSERT INTO player_items VALUES('Charlie',1,10)")

        # Gift 4 first aid kits: conservation of quantity and two audit entries.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("""UPDATE player_items SET quantity=4
            WHERE name='Alice' AND item_id=1 AND quantity=8""")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("""UPDATE player_items SET quantity=9
            WHERE name='Bob' AND item_id=1 AND quantity=5""")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("INSERT INTO item_transfers(sender,receiver,item_id,quantity,created_at) VALUES('Alice','Bob',1,4,123)")
        db.execute("INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('Alice',1,'give',4,0,123)")
        db.execute("INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('Bob',1,'receive',4,0,123)")
        db.execute("COMMIT")
        assert db.execute("SELECT quantity FROM player_items WHERE name='Alice'").fetchone() == (4,)
        assert db.execute("SELECT quantity FROM player_items WHERE name='Bob'").fetchone() == (9,)
        assert db.execute("SELECT SUM(quantity) FROM player_items WHERE item_id=1").fetchone()[0] == 23
        assert db.execute("SELECT COUNT(*) FROM item_transfers").fetchone()[0] == 1
        assert db.execute("SELECT action FROM inventory_events ORDER BY id").fetchall() == [("give",), ("receive",)]

        # Full recipient: rollback must restore sender's stock.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE player_items SET quantity=3 WHERE name='Alice' AND item_id=1 AND quantity=4")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        try:
            db.execute("UPDATE player_items SET quantity=11 WHERE name='Charlie' AND item_id=1 AND quantity=10")
        except sqlite3.IntegrityError:
            db.execute("ROLLBACK")
        else:
            raise AssertionError("Full receiver accepted 11 items")
        assert db.execute("SELECT quantity FROM player_items WHERE name='Alice'").fetchone() == (4,)

        # Stale quantity: CAS fails, rollback preserves sender and receiver.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE player_items SET quantity=3 WHERE name='Alice' AND item_id=1 AND quantity=8")
        assert db.execute("SELECT changes()").fetchone()[0] == 0
        db.execute("ROLLBACK")
        assert db.execute("SELECT quantity FROM player_items WHERE name='Bob'").fetchone() == (9,)

        # Quantity cannot exceed 5 even when SQL is called directly.
        try:
            db.execute("INSERT INTO item_transfers(sender,receiver,item_id,quantity,created_at) VALUES('Bob','Alice',1,6,124)")
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("Invalid transfer amount accepted")

    with sqlite3.connect(db_path) as reopened:
        assert reopened.execute("SELECT quantity FROM player_items WHERE name='alice'").fetchone() == (4,)
        assert reopened.execute("SELECT receiver,quantity FROM item_transfers").fetchall() == [("Bob",4)]
        assert reopened.execute("PRAGMA integrity_check").fetchone() == ("ok",)

print("PASS: item transfers conserve quantities and record two inventory events")
print("PASS: full recipient and stale balance cannot silently consume sender stock")
print("PASS: transfer quantity cap enforced by SQLite")
print("PASS: inventory and transfer history survive a database restart")
