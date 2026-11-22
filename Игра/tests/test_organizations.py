#!/usr/bin/env python3
"""SQLite organization and membership regression tests.

Checks DB constraints and transaction equivalence. Real open.mp client behavior
(invitations, chat, player ID reuse) still requires multiplayer testing.
"""
from __future__ import annotations

from pathlib import Path
import sqlite3
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "include" / "veltor_organizations.inc").read_text("utf-8")
game = (root / "gamemodes" / "veltor.pwn").read_text("utf-8")

for phrase in (
    "CREATE TABLE IF NOT EXISTS organizations",
    "CREATE TABLE IF NOT EXISTS organization_members",
    "UNIQUE COLLATE NOCASE",
    "player TEXT PRIMARY KEY COLLATE NOCASE",
    "CHECK(role IN (0,1))",
    "BEGIN IMMEDIATE TRANSACTION",
    "COMMIT", "ROLLBACK",
    "OrgChangedOne()", "OrgOwnerMatches(orgid, gOrgInviter[playerid])",
    "OrgMemberCount(orgid)", "ORG_MEMBER_LIMIT",
    "now+ORG_INVITE_SECONDS", "ORG_INVITE_COOLDOWN",
    "gOrganization[playerid]", "IsMuted(playerid)",
    "ORG_CHAT_COOLDOWN", "UPDATE accounts SET money=",
    "DELETE FROM organization_members WHERE player=",
    "DELETE FROM organizations WHERE id=",
    "role=1", "role=0",
):
    assert phrase in src, f"Organization safety contract missing: {phrase}"
assert '#include "veltor_organizations.inc"' in game
assert "if (HandleOrganizationCommand(playerid, cmdtext)) return 1;" in game
assert "gOrganization[playerid] = 0;" in game

with tempfile.TemporaryDirectory() as td:
    path = Path(td) / "veltor.sqlite3"
    with sqlite3.connect(path, isolation_level=None) as db:
        db.execute("CREATE TABLE accounts(name TEXT PRIMARY KEY COLLATE NOCASE, money INTEGER NOT NULL)")
        for name in ["Leader", "Player_A", "Player_B", "Other"]:
            db.execute("INSERT INTO accounts(name,money) VALUES(?,6000)", (name,))
        db.execute("""CREATE TABLE organizations(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE COLLATE NOCASE,
            owner TEXT NOT NULL UNIQUE COLLATE NOCASE,
            created_at INTEGER NOT NULL)""")
        db.execute("""CREATE TABLE organization_members(
            player TEXT PRIMARY KEY COLLATE NOCASE,
            org_id INTEGER NOT NULL,
            role INTEGER NOT NULL DEFAULT 0 CHECK(role IN (0,1)),
            joined_at INTEGER NOT NULL)""")
        db.execute("CREATE INDEX idx_organization_members_org ON organization_members(org_id)")

        # Creating a group and paying for it must be one transaction.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("INSERT INTO organizations(name,owner,created_at) VALUES('Test_Group','Leader',1)")
        org = db.execute("SELECT last_insert_rowid()").fetchone()[0]
        db.execute(
            "INSERT INTO organization_members(player,org_id,role,joined_at) VALUES('Leader',?,1,1)",
            (org,),
        )
        db.execute("UPDATE accounts SET money=1000 WHERE name='Leader' AND money=6000")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("COMMIT")
        assert db.execute("SELECT money FROM accounts WHERE name='Leader'").fetchone()[0] == 1000

        # Names are unique ignoring case; failed creation cannot cost money.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        try:
            db.execute("INSERT INTO organizations(name,owner,created_at) VALUES('test_group','Other',2)")
        except sqlite3.IntegrityError:
            db.execute("ROLLBACK")
        else:
            raise AssertionError("Duplicate organization name was accepted")
        assert db.execute("SELECT money FROM accounts WHERE name='Other'").fetchone()[0] == 6000

        # Invitations only become memberships when accepted; unique player restriction.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("INSERT INTO organization_members(player,org_id,role,joined_at) VALUES('Player_A',?,0,3)", (org,))
        db.execute("COMMIT")
        try:
            db.execute("INSERT INTO organization_members(player,org_id,role,joined_at) VALUES('player_a',?,0,4)", (org,))
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("Same account joined twice")

        # Leadership transfer is atomic and has exactly one owner + leader.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE organizations SET owner='Player_A' WHERE id=? AND owner='Leader'", (org,))
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("UPDATE organization_members SET role=0 WHERE player='Leader' AND role=1")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("UPDATE organization_members SET role=1 WHERE player='Player_A' AND role=0")
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("COMMIT")
        assert db.execute("SELECT owner FROM organizations").fetchone()[0] == "Player_A"
        assert db.execute("SELECT player FROM organization_members WHERE role=1").fetchone()[0] == "Player_A"

        # Invalid target role must roll back owner transfer.
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("UPDATE organizations SET owner='Player_B' WHERE id=? AND owner='Player_A'", (org,))
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("UPDATE organization_members SET role=1 WHERE player='Player_B' AND role=0")
        assert db.execute("SELECT changes()").fetchone()[0] == 0
        db.execute("ROLLBACK")
        assert db.execute("SELECT owner FROM organizations").fetchone()[0] == "Player_A"

        # Only members with role=0 can leave.
        db.execute("DELETE FROM organization_members WHERE player='Player_A' AND role=0")
        assert db.execute("SELECT changes()").fetchone()[0] == 0
        db.execute("DELETE FROM organization_members WHERE player='Leader' AND role=0")
        assert db.execute("SELECT changes()").fetchone()[0] == 1

        # With only the leader left, disband must delete both records.
        assert db.execute("SELECT COUNT(*) FROM organization_members WHERE org_id=?", (org,)).fetchone()[0] == 1
        db.execute("BEGIN IMMEDIATE TRANSACTION")
        db.execute("DELETE FROM organization_members WHERE org_id=? AND player='Player_A' AND role=1", (org,))
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("DELETE FROM organizations WHERE id=? AND owner='Player_A'", (org,))
        assert db.execute("SELECT changes()").fetchone()[0] == 1
        db.execute("COMMIT")
        assert db.execute("SELECT COUNT(*) FROM organizations").fetchone()[0] == 0
        assert db.execute("SELECT COUNT(*) FROM organization_members").fetchone()[0] == 0

    with sqlite3.connect(path) as reopened:
        assert reopened.execute("SELECT money FROM accounts WHERE name='Leader'").fetchone() == (1000,)
        assert reopened.execute("PRAGMA integrity_check").fetchone() == ("ok",)

print("PASS: organization creation atomically charges founder")
print("PASS: case-insensitive uniqueness blocks duplicate group and memberships")
print("PASS: leadership transfer and invalid-target rollback are consistent")
print("PASS: leader cannot accidentally leave; disband clears all membership")
print("PASS: data remains valid after SQLite reopen")
