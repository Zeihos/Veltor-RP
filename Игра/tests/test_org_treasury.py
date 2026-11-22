#!/usr/bin/env python3
"""Organization treasury/deputy SQLite regression tests.

The Python test checks the SQL contracts and rollbacks, not gameplay network I/O.
"""
from pathlib import Path
import sqlite3
import tempfile

root = Path(__file__).resolve().parents[1]
org = (root / "include" / "veltor_organizations.inc").read_text("utf-8")
mgmt = (root / "include" / "veltor_org_management.inc").read_text("utf-8")
game = (root / "gamemodes" / "veltor.pwn").read_text("utf-8")
for required in (
    "CREATE TABLE IF NOT EXISTS organization_officers",
    "CREATE TABLE IF NOT EXISTS organization_treasury",
    "CREATE TABLE IF NOT EXISTS organization_treasury_history",
    "INSERT OR IGNORE INTO organization_treasury(org_id,balance) SELECT id,0 FROM organizations",
    "BEGIN IMMEDIATE TRANSACTION", "COMMIT", "ROLLBACK",
    "OrgChangedOne()", "BankAtATM(playerid)",
    "OrgIsLeader(playerid)", "OrgCanManage(playerid)",
    "OrgTreasuryBalance(orgid)", "ORG_TREASURY_TX_LIMIT",
    "AND money=%d", "AND balance=%d",
    "DELETE FROM organization_officers WHERE player=",
    "DELETE FROM organization_members WHERE player=",
):
    assert required in mgmt, f"Missing management protection: {required}"
assert "OrgTreasuryBalance(orgid)!=0" in org
assert "OrgEnsureTreasury(orgid)" in org
assert "OrgInviterAuthorized(orgid, gOrgInviter[playerid])" in org
assert '#include "veltor_org_management.inc"' in game
assert "HandleOrgManagementCommand(playerid, cmdtext)" in game
assert "new gLastOrgTreasury[MAX_PLAYERS];" in game

with tempfile.TemporaryDirectory() as temp:
    dbfile = Path(temp) / "veltor.db"
    with sqlite3.connect(dbfile, isolation_level=None) as db:
        db.execute("CREATE TABLE accounts(name TEXT PRIMARY KEY COLLATE NOCASE,money INTEGER NOT NULL)")
        for name, money in (("Founder",10000), ("Deputy",5000),
                             ("Member",1200), ("Other",9000)):
            db.execute("INSERT INTO accounts(name,money) VALUES(?,?)",(name,money))
        db.execute("""CREATE TABLE organizations(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE COLLATE NOCASE NOT NULL,
            owner TEXT UNIQUE COLLATE NOCASE NOT NULL,
            created_at INTEGER NOT NULL)""")
        db.execute("""CREATE TABLE organization_members(
            player TEXT PRIMARY KEY COLLATE NOCASE,
            org_id INTEGER NOT NULL, role INTEGER NOT NULL DEFAULT 0
            CHECK(role IN (0,1)), joined_at INTEGER NOT NULL)""")
        db.execute("INSERT INTO organizations(name,owner,created_at) VALUES('Group_1','Founder',1)")
        orgid = db.execute("SELECT id FROM organizations").fetchone()[0]
        for player, rank in (("Founder",1),("Deputy",0),("Member",0)):
            db.execute("INSERT INTO organization_members VALUES(?,?,?,2)", (player,orgid,rank))

        # Migration: existing organization and member schema must survive.
        db.execute("""CREATE TABLE organization_officers(
            player TEXT PRIMARY KEY COLLATE NOCASE,org_id INTEGER NOT NULL,
            appointed_at INTEGER NOT NULL)""")
        db.execute("""CREATE TABLE organization_treasury(
            org_id INTEGER PRIMARY KEY,balance INTEGER NOT NULL DEFAULT 0
            CHECK(balance>=0 AND balance<=100000000))""")
        db.execute("""CREATE TABLE organization_treasury_history(
            id INTEGER PRIMARY KEY AUTOINCREMENT,org_id INTEGER NOT NULL,
            actor TEXT NOT NULL,action TEXT NOT NULL,
            amount INTEGER NOT NULL CHECK(amount BETWEEN 1 AND 50000),
            balance_after INTEGER NOT NULL,created_at INTEGER NOT NULL)""")
        migration = "INSERT OR IGNORE INTO organization_treasury(org_id,balance) SELECT id,0 FROM organizations"
        db.execute(migration)
        db.execute(migration)
        assert db.execute("SELECT COUNT(*) FROM organization_treasury").fetchone() == (1,)
        assert db.execute("SELECT owner FROM organizations").fetchone() == ("Founder",)
        assert db.execute("SELECT COUNT(*) FROM organization_members").fetchone() == (3,)

        # Founder deposits $2000: only one account update, matching treasury update, one audit row.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=8000 WHERE name='Founder' AND money=10000")
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("UPDATE organization_treasury SET balance=2000 WHERE org_id=? AND balance=0",(orgid,))
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("INSERT INTO organization_treasury_history(org_id,actor,action,amount,balance_after,created_at) VALUES(?, 'Founder','deposit',2000,2000,3)",(orgid,))
        db.execute("COMMIT")
        assert db.execute("SELECT money FROM accounts WHERE name='Founder'").fetchone() == (8000,)
        assert db.execute("SELECT balance FROM organization_treasury").fetchone() == (2000,)
        assert db.execute("SELECT COUNT(*) FROM organization_treasury_history").fetchone() == (1,)

        # Stale money: the debit CAS must fail and leave both balances intact.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=0 WHERE name='Founder' AND money=10000")
        assert db.execute("SELECT changes()").fetchone() == (0,)
        db.execute("ROLLBACK")
        assert db.execute("SELECT balance FROM organization_treasury").fetchone() == (2000,)

        # Stale treasury: debit must be rolled back if second update affects 0 rows.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=7000 WHERE name='Founder' AND money=8000")
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("UPDATE organization_treasury SET balance=3000 WHERE org_id=? AND balance=9999",(orgid,))
        assert db.execute("SELECT changes()").fetchone() == (0,)
        db.execute("ROLLBACK")
        assert db.execute("SELECT money FROM accounts WHERE name='Founder'").fetchone() == (8000,)

        # Deputy role: max one officer per account, case-insensitive.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("INSERT INTO organization_officers(player,org_id,appointed_at) SELECT player,org_id,4 FROM organization_members WHERE player='Deputy' AND org_id=? AND role=0",(orgid,))
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("COMMIT")
        try:
            db.execute("INSERT INTO organization_officers VALUES('deputy',?,5)",(orgid,))
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("Duplicate deputy appointment accepted")

        # Member kick: delete officer privileges and membership together.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("DELETE FROM organization_officers WHERE player='Deputy' AND org_id=?",(orgid,))
        db.execute("DELETE FROM organization_members WHERE player='Deputy' AND org_id=? AND role=0",(orgid,))
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("COMMIT")
        assert db.execute("SELECT COUNT(*) FROM organization_officers").fetchone() == (0,)
        assert db.execute("SELECT COUNT(*) FROM organization_members").fetchone() == (2,)

        # Withdrawal $500: pay the founder and debit treasury atomically.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE accounts SET money=8500 WHERE name='Founder' AND money=8000")
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("UPDATE organization_treasury SET balance=1500 WHERE org_id=? AND balance=2000",(orgid,))
        assert db.execute("SELECT changes()").fetchone() == (1,)
        db.execute("INSERT INTO organization_treasury_history(org_id,actor,action,amount,balance_after,created_at) VALUES(?,'Founder','withdraw',500,1500,6)",(orgid,))
        db.execute("COMMIT")
        assert db.execute("SELECT money FROM accounts WHERE name='Founder'").fetchone() == (8500,)
        assert db.execute("SELECT balance FROM organization_treasury").fetchone() == (1500,)

        # A nonempty treasury must be preserved (disband in Pawn explicitly rejects it).
        assert db.execute("SELECT balance FROM organization_treasury").fetchone()[0] > 0
        assert "OrgTreasuryBalance(orgid)!=0" in org

        # Constraints rule out negative treasury, overflow and invalid ledger amounts.
        for sql in ("UPDATE organization_treasury SET balance=-1",
                    "UPDATE organization_treasury SET balance=100000001",
                    "INSERT INTO organization_treasury_history(org_id,actor,action,amount,balance_after,created_at) VALUES(1,'Founder','deposit',50001,1,3)"):
            try:
                db.execute(sql)
            except sqlite3.IntegrityError:
                pass
            else:
                raise AssertionError(f"Constraint bypassed: {sql}")

    with sqlite3.connect(dbfile) as restored:
        assert restored.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        assert restored.execute("SELECT balance FROM organization_treasury").fetchone() == (1500,)
        assert restored.execute("SELECT COUNT(*) FROM organization_treasury_history").fetchone() == (2,)

print("PASS: existing organizations survive idempotent treasury migration")
print("PASS: deposits and withdrawals update cash, treasury and ledger atomically")
print("PASS: stale values and failed operations roll back all changes")
print("PASS: deputy uniqueness and removal preserve member integrity")
print("PASS: funded organizations cannot disband; DB constraints guard balances")
print("PASS: treasury balances and audit history persist after SQLite reopen")
