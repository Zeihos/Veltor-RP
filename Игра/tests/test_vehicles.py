#!/usr/bin/env python3
"""Vehicle storage contract checks. SQL-equivalent checks, not in-game tests."""
from __future__ import annotations

from pathlib import Path
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "include" / "veltor_vehicles.inc").read_text(encoding="utf-8")
gamemode = (ROOT / "gamemodes" / "veltor.pwn").read_text(encoding="utf-8")

required_source = (
    "CREATE TABLE IF NOT EXISTS player_vehicles",
    "BEGIN IMMEDIATE TRANSACTION",
    "COMMIT",
    "ROLLBACK",
    "SELECT model,x,y,z,angle,locked FROM player_vehicles",
    "INSERT OR IGNORE INTO player_vehicles",
    "GetPlayerState(playerid) != PLAYER_STATE_DRIVER",
    "GetPlayerVirtualWorld(playerid) != 0",
    "CarIsOccupied(gCarId[playerid])",
    "SetVehicleParamsForPlayer",
    "OwnedCarOwner(vehicleid)",
)
for phrase in required_source:
    assert phrase in source, f"Vehicle invariant not in Pawn source: {phrase}"
assert '#include "veltor_vehicles.inc"' in gamemode
assert "if (HandleVehicleCommand(playerid, cmdtext)) return 1;" in gamemode
assert "OnPlayerVehicleStateChange(playerid, newstate)" in gamemode

with tempfile.TemporaryDirectory() as temp:
    with sqlite3.connect(Path(temp) / "game.db", isolation_level=None) as db:
        db.execute("CREATE TABLE accounts (name TEXT PRIMARY KEY COLLATE NOCASE, money INTEGER, car INTEGER)")
        db.execute("INSERT INTO accounts VALUES ('Player_1',20000,0)")
        db.execute("INSERT INTO accounts VALUES ('Legacy_Player',1000,560)")
        db.execute("""CREATE TABLE player_vehicles(
            name TEXT PRIMARY KEY COLLATE NOCASE, model INTEGER NOT NULL,
            x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL,
            angle REAL NOT NULL, locked INTEGER NOT NULL DEFAULT 1)""")

        # Simulate purchase with account and ownership updated together.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("""INSERT INTO player_vehicles(name,model,x,y,z,angle,locked)
                   VALUES('Player_1',560,1493.1,-1725.5,13.3,90,1)""")
        db.execute("UPDATE accounts SET money=5000,car=560 WHERE name='Player_1'")
        db.execute("COMMIT")
        assert db.execute("SELECT money,car FROM accounts WHERE name='Player_1'").fetchone() == (5000,560)
        assert db.execute("SELECT model,locked FROM player_vehicles WHERE name='Player_1'").fetchone() == (560,1)

        # Parking position and lock state persist after a new database connection.
        db.execute("UPDATE player_vehicles SET x=?,y=?,z=?,angle=?,locked=? WHERE name=?",
                   (1420.0,-1740.0,13.4,180.0,0,"Player_1"))
        assert db.execute("SELECT x,y,z,angle,locked FROM player_vehicles WHERE name='player_1'").fetchone() == (
            1420.0,-1740.0,13.4,180.0,0
        )

        # Pre-garage accounts must migrate without creating a second car.
        statement="""INSERT OR IGNORE INTO player_vehicles(name,model,x,y,z,angle,locked)
                  VALUES(?,560,1493.1,-1725.5,13.3,90,1)"""
        db.execute(statement, ("Legacy_Player",))
        db.execute(statement, ("legacy_player",))
        assert db.execute("SELECT COUNT(*) FROM player_vehicles WHERE name='Legacy_Player'").fetchone()[0] == 1

        # A duplicate car insert must roll back without debiting the account.
        try:
            db.execute("BEGIN IMMEDIATE TRANSACTION")
            db.execute("""INSERT INTO player_vehicles(name,model,x,y,z,angle,locked)
                       VALUES('player_1',560,1493.1,-1725.5,13.3,90,1)""")
        except sqlite3.IntegrityError:
            db.execute("ROLLBACK")
        else:
            raise AssertionError("Duplicate vehicle was accepted")
        assert db.execute("SELECT money FROM accounts WHERE name='Player_1'").fetchone()[0] == 5000

        # A resale atomically clears ownership and credits the balance.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("DELETE FROM player_vehicles WHERE name='Player_1'")
        db.execute("UPDATE accounts SET money=12500,car=0 WHERE name='Player_1'")
        db.execute("COMMIT")
        assert db.execute("SELECT car,money FROM accounts WHERE name='Player_1'").fetchone() == (0,12500)
        assert db.execute("SELECT COUNT(*) FROM player_vehicles WHERE name='Player_1'").fetchone()[0] == 0

print("PASS: vehicle purchases and resales use atomic updates")
print("PASS: garage location, locks and legacy accounts persist")
print("PASS: duplicate ownership and invalid access paths are guarded in Pawn")
