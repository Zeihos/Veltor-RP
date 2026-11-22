#!/usr/bin/env python3
"""Offline database contract tests for VELTOR bank and property modules.

These prove SQL invariants/rollback in SQLite; they do not replace multiplayer tests.
"""
from pathlib import Path
import sqlite3
import tempfile

root=Path(__file__).resolve().parents[1]
bank=(root/"include/veltor_bank.inc").read_text("utf-8")
houses=(root/"include/veltor_housing.inc").read_text("utf-8")
mode=(root/"gamemodes/veltor.pwn").read_text("utf-8")
for fragment in (
    "BEGIN IMMEDIATE TRANSACTION", "COMMIT", "ROLLBACK",
    "bank_accounts", "bank_history", "UPDATE accounts SET money",
    "BankUpdatedOneRow()", "BankAtATM(playerid)", "BANK_ACTION_LIMIT",
    "CommitCashTransfer(sender, recipient, amount)", "cash_transfers",
):
    assert fragment in bank, f"Missing bank invariant: {fragment}"
for fragment in (
    "owner TEXT UNIQUE COLLATE NOCASE", "INSERT OR IGNORE INTO houses",
    "BEGIN IMMEDIATE TRANSACTION", "COMMIT", "ROLLBACK",
    "AND owner IS NULL", "HouseAffectedOne()", "HouseAtDoor(playerid",
    "SetPlayerVirtualWorld(playerid, 2000+id)", "HouseCanEnter(playerid",
):
    assert fragment in houses, f"Missing property invariant: {fragment}"
assert '#include "veltor_bank.inc"' in mode
assert '#include "veltor_housing.inc"' in mode
assert "HandleBankCommand(playerid, cmdtext)" in mode
assert "HandleHousingCommand(playerid, cmdtext)" in mode

with tempfile.TemporaryDirectory() as td:
    file=Path(td)/"veltor.db"
    with sqlite3.connect(file, isolation_level=None) as db:
        db.execute("CREATE TABLE accounts(name TEXT PRIMARY KEY COLLATE NOCASE,money INTEGER)")
        db.execute("INSERT INTO accounts VALUES ('User_1',30000)")
        db.execute("INSERT INTO accounts VALUES ('User_2',10000)")
        db.execute("""CREATE TABLE bank_accounts (
            name TEXT PRIMARY KEY COLLATE NOCASE,
            balance INTEGER NOT NULL DEFAULT 0 CHECK(balance>=0 AND balance<=100000000))""")
        db.execute("CREATE TABLE bank_history(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT,action TEXT,amount INTEGER,created_at INTEGER)")
        db.execute("CREATE TABLE cash_transfers(id INTEGER PRIMARY KEY AUTOINCREMENT,sender TEXT,receiver TEXT,amount INTEGER,created_at INTEGER)")
        db.execute("""CREATE TABLE houses(
            id INTEGER PRIMARY KEY, price INTEGER NOT NULL,
            owner TEXT UNIQUE COLLATE NOCASE, locked INTEGER NOT NULL DEFAULT 1,
            purchased_at INTEGER NOT NULL DEFAULT 0)""")
        db.execute("INSERT INTO bank_accounts VALUES('User_1',0)")
        for i, price in enumerate((5000,12000,25000),1):
            db.execute("INSERT INTO houses(id,price,owner) VALUES(?,?,NULL)",(i,price))

        # Deposit cash into account with one transaction and a journal entry.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=25000 WHERE name='User_1' AND money=30000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("UPDATE bank_accounts SET balance=5000 WHERE name='User_1' AND balance=0")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("INSERT INTO bank_history(name,action,amount,created_at) VALUES('User_1','deposit',5000,1)")
        db.execute("COMMIT")
        assert db.execute("SELECT money FROM accounts WHERE name='User_1'").fetchone()[0]==25000
        assert db.execute("SELECT balance FROM bank_accounts WHERE name='User_1'").fetchone()[0]==5000

        # Failed bank write must roll back cash withdrawal.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=30000 WHERE name='User_1'")
        try:
            db.execute("UPDATE bank_accounts SET balance=-5 WHERE name='User_1'")
        except sqlite3.IntegrityError:
            db.execute("ROLLBACK")
        else:
            raise AssertionError("Invalid negative bank balance was accepted")
        assert db.execute("SELECT money FROM accounts WHERE name='User_1'").fetchone()[0]==25000

        # Home ownership, uniqueness and anti-double-purchase guarded by DB constraints.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE houses SET owner='User_1' WHERE id=1 AND owner IS NULL AND price=5000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("UPDATE accounts SET money=20000 WHERE name='User_1' AND money=25000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("COMMIT")
        assert db.execute("SELECT owner FROM houses WHERE id=1").fetchone()[0]=="User_1"

        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE houses SET owner='User_2' WHERE id=1 AND owner IS NULL")
        assert db.execute("SELECT changes()").fetchone()[0]==0
        db.execute("ROLLBACK")
        assert db.execute("SELECT owner FROM houses WHERE id=1").fetchone()[0]=="User_1"

        # One property maximum per account (case-insensitive).
        try:
            db.execute("UPDATE houses SET owner='user_1' WHERE id=2")
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("Duplicate home ownership was accepted")

        # Sale can clear ownership and credit money atomically.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE houses SET owner=NULL,locked=1 WHERE id=1 AND owner='User_1'")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("UPDATE accounts SET money=22500 WHERE name='User_1' AND money=20000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("COMMIT")
        assert db.execute("SELECT owner FROM houses WHERE id=1").fetchone()[0] is None
        assert db.execute("SELECT money FROM accounts WHERE name='User_1'").fetchone()[0]==22500
        # Two-player transfers are all-or-nothing and logged.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=21000 WHERE name='User_1' AND money=22500")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("UPDATE accounts SET money=11500 WHERE name='User_2' AND money=10000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("INSERT INTO cash_transfers(sender,receiver,amount,created_at) VALUES('User_1','User_2',1500,2)")
        db.execute("COMMIT")
        assert db.execute("SELECT money FROM accounts WHERE name='User_1'").fetchone()[0]==21000
        assert db.execute("SELECT money FROM accounts WHERE name='User_2'").fetchone()[0]==11500
        assert db.execute("SELECT sender,receiver,amount FROM cash_transfers").fetchone()==("User_1","User_2",1500)

        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=19000 WHERE name='User_1' AND money=21000")
        assert db.execute("SELECT changes()").fetchone()[0]==1
        db.execute("UPDATE accounts SET money=13500 WHERE name='User_2' AND money=99999")
        assert db.execute("SELECT changes()").fetchone()[0]==0
        db.execute("ROLLBACK")
        assert db.execute("SELECT money FROM accounts WHERE name='User_1'").fetchone()[0]==21000
        assert db.execute("SELECT money FROM accounts WHERE name='User_2'").fetchone()[0]==11500

print("PASS: bank deposit and ledger committed atomically")
print("PASS: negative balance rollback preserves cash")
print("PASS: properties cannot be sold to two owners or duplicated")
print("PASS: property purchase and sale update cash and title together")
print("PASS: cash transfers update both players and ledger together")
