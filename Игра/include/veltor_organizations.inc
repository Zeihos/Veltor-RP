#if defined _veltor_organizations_included
    #endinput
#endif
#define _veltor_organizations_included

// Player-created organizations: server-controlled membership and ownership.
// These are civilian groups, not police/medical staff roles.
#define ORG_CREATE_PRICE 5000
#define ORG_MEMBER_LIMIT 20
#define ORG_INVITE_SECONDS 60
#define ORG_INVITE_COOLDOWN 10
#define ORG_CHAT_COOLDOWN 2

stock bool:OrgExec(const query[])
{
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    db_free_result(r);
    return true;
}

stock bool:OrgChangedOne()
{
    new DBResult:r = db_query(gDb, "SELECT changes() AS affected");
    if (!r) return false;
    new affected = db_num_rows(r) > 0 ?
        db_get_field_assoc_int(r, "affected") : 0;
    db_free_result(r);
    return affected == 1;
}

stock bool:InitOrganizationDB()
{
    if (!OrgExec("CREATE TABLE IF NOT EXISTS organizations (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE COLLATE NOCASE, owner TEXT NOT NULL UNIQUE COLLATE NOCASE, created_at INTEGER NOT NULL)"))
        return false;
    if (!OrgExec("CREATE TABLE IF NOT EXISTS organization_members (player TEXT PRIMARY KEY COLLATE NOCASE, org_id INTEGER NOT NULL, role INTEGER NOT NULL DEFAULT 0 CHECK(role IN (0,1)), joined_at INTEGER NOT NULL)"))
        return false;
    if (!OrgExec("CREATE INDEX IF NOT EXISTS idx_organization_members_org ON organization_members(org_id)"))
        return false;
    return true;
}

stock bool:OrgValidName(const value[])
{
    new len = strlen(value);
    if (len < 4 || len > 20) return false;
    for (new i=0; i<len; i++)
    {
        if ((value[i]>='A' && value[i]<='Z') ||
            (value[i]>='a' && value[i]<='z') ||
            (value[i]>='0' && value[i]<='9') ||
            value[i]=='_') continue;
        return false;
    }
    return true;
}

stock bool:OrgLoadMember(playerid)
{
    gOrganization[playerid] = 0;
    new query[180];
    format(query, sizeof query,
        "SELECT org_id FROM organization_members WHERE player='%s' LIMIT 1",
        gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    if (db_num_rows(r)>0)
        gOrganization[playerid] = db_get_field_assoc_int(r, "org_id");
    db_free_result(r);
    if (gOrganization[playerid] < 0) return false;
    if (gOrganization[playerid] != 0)
    {
        format(query, sizeof query, "SELECT id FROM organizations WHERE id=%d LIMIT 1",
            gOrganization[playerid]);
        r = db_query(gDb, query);
        if (!r) return false;
        new exists = db_num_rows(r)>0;
        db_free_result(r);
        if (!exists) return false;
    }
    return true;
}

stock bool:OrgIsLeader(playerid)
{
    if (gOrganization[playerid] == 0) return false;
    new query[200];
    format(query, sizeof query,
        "SELECT id FROM organizations WHERE id=%d AND owner='%s' LIMIT 1",
        gOrganization[playerid], gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    new found = db_num_rows(r)>0;
    db_free_result(r);
    return found;
}

stock OrgMemberCount(orgid)
{
    new query[150];
    format(query, sizeof query,
        "SELECT COUNT(*) AS count FROM organization_members WHERE org_id=%d",
        orgid);
    new DBResult:r = db_query(gDb, query);
    if (!r) return -1;
    new count = db_num_rows(r)>0 ? db_get_field_assoc_int(r, "count") : -1;
    db_free_result(r);
    return count;
}

stock bool:OrgOwnerMatches(orgid, const owner[])
{
    if (orgid < 1) return false;
    new query[180], name[MAX_PLAYER_NAME+1];
    format(query, sizeof query,
        "SELECT owner FROM organizations WHERE id=%d LIMIT 1", orgid);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    name[0] = EOS;
    if (db_num_rows(r)>0) db_get_field_assoc(r, "owner", name, sizeof name);
    db_free_result(r);
    return name[0] != EOS && !strcmp(name, owner, true);
}

stock bool:OrgCreate(playerid, const name[])
{
    if (!OrgValidName(name) || gOrganization[playerid] != 0 ||
        gMoney[playerid] < ORG_CREATE_PRICE) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[330], orgid = 0;
    format(query, sizeof query,
        "INSERT INTO organizations(name,owner,created_at) VALUES('%s','%s',%d)",
        name, gPlayerName[playerid], gettime());
    if (!OrgExec(query) || !OrgChangedOne()) ok = false;

    if (ok)
    {
        new DBResult:r = db_query(gDb, "SELECT last_insert_rowid() AS id");
        if (!r) ok = false;
        else
        {
            if (db_num_rows(r)>0) orgid = db_get_field_assoc_int(r, "id");
            db_free_result(r);
            if (orgid < 1) ok = false;
        }
    }
    if (ok && !OrgEnsureTreasury(orgid)) ok=false;
    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO organization_members(player,org_id,role,joined_at) VALUES('%s',%d,1,%d)",
            gPlayerName[playerid], orgid, gettime());
        if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
            gMoney[playerid]-ORG_CREATE_PRICE, gPlayerName[playerid], gMoney[playerid]);
        if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        gOrganization[playerid] = orgid;
        SetCash(playerid, gMoney[playerid]-ORG_CREATE_PRICE);
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgAcceptInvite(playerid)
{
    new orgid = gOrgInvite[playerid];
    if (orgid <= 0 || gettime()>gOrgInviteUntil[playerid] ||
        gOrganization[playerid] != 0) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = OrgInviterAuthorized(orgid, gOrgInviter[playerid]);
    new members = OrgMemberCount(orgid);
    if (members < 1 || members >= ORG_MEMBER_LIMIT) ok = false;
    if (ok)
    {
        new query[250];
        format(query, sizeof query,
            "INSERT INTO organization_members(player,org_id,role,joined_at) VALUES('%s',%d,0,%d)",
            gPlayerName[playerid], orgid, gettime());
        if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        gOrganization[playerid] = orgid;
        gOrgInvite[playerid] = 0;
        gOrgInviteUntil[playerid] = 0;
        gOrgInviter[playerid][0] = EOS;
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgLeave(playerid)
{
    if (gOrganization[playerid]<=0 || OrgIsLeader(playerid)) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new query[240],bool:ok=true;
    format(query,sizeof query,
        "DELETE FROM organization_officers WHERE player='%s' AND org_id=%d",
        gPlayerName[playerid],gOrganization[playerid]);
    if (!OrgExec(query)) ok=false;
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organization_members WHERE player='%s' AND org_id=%d AND role=0",
            gPlayerName[playerid],gOrganization[playerid]);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        gOrganization[playerid]=0;
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgTransferLeader(playerid, target)
{
    if (!OrgIsLeader(playerid) || target == playerid ||
        !IsPlayerConnected(target) || !gAuthed[target] ||
        gOrganization[playerid] != gOrganization[target])
        return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[260], orgid = gOrganization[playerid];
    format(query, sizeof query,
        "UPDATE organizations SET owner='%s' WHERE id=%d AND owner='%s'",
        gPlayerName[target], orgid, gPlayerName[playerid]);
    if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE organization_members SET role=0 WHERE player='%s' AND org_id=%d AND role=1",
            gPlayerName[playerid], orgid);
        if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE organization_members SET role=1 WHERE player='%s' AND org_id=%d AND role=0",
            gPlayerName[target], orgid);
        if (!OrgExec(query) || !OrgChangedOne()) ok = false;
    }
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organization_officers WHERE player='%s' AND org_id=%d",
            gPlayerName[target],orgid);
        if (!OrgExec(query)) ok=false;
    }
    if (ok && OrgExec("COMMIT")) return true;
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgDisband(playerid)
{
    if (!OrgIsLeader(playerid)) return false;
    new orgid=gOrganization[playerid];
    if (OrgMemberCount(orgid)!=1 || OrgTreasuryBalance(orgid)!=0) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new query[260],bool:ok=true;
    format(query,sizeof query,
        "DELETE FROM organization_treasury WHERE org_id=%d AND balance=0",orgid);
    if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organization_officers WHERE org_id=%d",orgid);
        if (!OrgExec(query)) ok=false;
    }
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organization_members WHERE org_id=%d AND player='%s' AND role=1",
            orgid,gPlayerName[playerid]);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organizations WHERE id=%d AND owner='%s'",
            orgid,gPlayerName[playerid]);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        gOrganization[playerid]=0;
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock OrgShowInfo(playerid)
{
    if (gOrganization[playerid] == 0)
    {
        SendClientMessage(playerid, C_WHITE, "You are not in an organization. Use /org create NAME.");
        return 1;
    }
    new query[160], name[24], owner[MAX_PLAYER_NAME+1], msg[150];
    format(query, sizeof query,
        "SELECT name,owner FROM organizations WHERE id=%d LIMIT 1",
        gOrganization[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r || db_num_rows(r)<1)
    {
        if (r) db_free_result(r);
        SendClientMessage(playerid, C_RED, "Organization data unavailable.");
        return 1;
    }
    db_get_field_assoc(r, "name", name, sizeof name);
    db_get_field_assoc(r, "owner", owner, sizeof owner);
    db_free_result(r);
    format(msg, sizeof msg, "Organization: %s | Leader: %s | Members: %d/%d",
        name, owner, OrgMemberCount(gOrganization[playerid]), ORG_MEMBER_LIMIT);
    SendClientMessage(playerid, C_GREEN, msg);
    SendClientMessage(playerid, C_WHITE, "/org members /org invite ID /org leave /oc message");
    return 1;
}

stock OrgShowMembers(playerid)
{
    if (gOrganization[playerid]==0) return OrgShowInfo(playerid);
    new query[180];
    format(query, sizeof query,
        "SELECT m.player,m.role,(SELECT COUNT(*) FROM organization_officers o WHERE o.player=m.player AND o.org_id=m.org_id) AS officer FROM organization_members m WHERE m.org_id=%d ORDER BY m.role DESC,m.player ASC LIMIT 10",
        gOrganization[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r)
    {
        SendClientMessage(playerid, C_RED, "Member list unavailable.");
        return 1;
    }
    new count = db_num_rows(r);
    SendClientMessage(playerid, C_ORANGE, "First 10 organization members:");
    for (new i=0; i<count; i++)
    {
        new name[MAX_PLAYER_NAME+1], message[110];
        db_get_field_assoc(r, "player", name, sizeof name);
        new role=db_get_field_assoc_int(r,"role");
        new deputy=db_get_field_assoc_int(r,"officer");
        format(message,sizeof message,"%s | %s",name,
            role==1 ? ("Leader"):(deputy>0 ? ("Deputy"):("Member")));
        SendClientMessage(playerid, C_WHITE, message);
        db_next_row(r);
    }
    db_free_result(r);
    return 1;
}

stock OrgSendChat(playerid, const cmdtext[])
{
    if (gOrganization[playerid]==0)
    {
        SendClientMessage(playerid, C_RED, "Join an organization first.");
        return 1;
    }
    if (IsMuted(playerid))
    {
        SendClientMessage(playerid, C_RED, "You are muted.");
        return 1;
    }
    new now=gettime();
    if (gLastChat[playerid] != 0 && now-gLastChat[playerid] < ORG_CHAT_COOLDOWN)
    {
        SendClientMessage(playerid, C_RED, "Do not flood the chat.");
        return 1;
    }
    new message[100], out[145];
    strmid(message, cmdtext, 4, strlen(cmdtext), sizeof message);
    new len=strlen(message);
    if (len<1) return 1;
    for (new i=0;i<len;i++)
        if (message[i]<32) message[i]=' ';
    format(out, sizeof out, "[ORG] %s: %s", gPlayerName[playerid], message);
    for (new i=0;i<MAX_PLAYERS;i++)
        if (IsPlayerConnected(i) && gAuthed[i] &&
            gOrganization[i]==gOrganization[playerid])
            SendClientMessage(i, C_ORANGE, out);
    gLastChat[playerid]=now;
    return 1;
}

stock HandleOrganizationCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/oc ", true, 4)) return OrgSendChat(playerid, cmdtext);
    if (!strcmp(cmdtext, "/oc", true))
    {
        SendClientMessage(playerid, C_WHITE, "Usage: /oc message");
        return 1;
    }
    if (!strcmp(cmdtext, "/org", true))
    {
        SendClientMessage(playerid, C_ORANGE,
            "Organization: /org create NAME | /org info | /org members");
        SendClientMessage(playerid, C_WHITE,
            "/org invite ID | /org accept | /org decline | /org leave");
        SendClientMessage(playerid, C_WHITE,
            "/org transfer ID | /org disband CONFIRM | /oc message");
        SendClientMessage(playerid,C_WHITE,
            "/org treasury /org deposit /org withdraw /org ledger");
        SendClientMessage(playerid,C_WHITE,
            "/org promote ID /org demote ID /org kick ID");
        return 1;
    }
    if (!strcmp(cmdtext, "/org create ", true, 12))
    {
        new name[30];
        strmid(name, cmdtext, 12, strlen(cmdtext), sizeof name);
        if (!OrgValidName(name))
        {
            SendClientMessage(playerid, C_RED, "Name: 4-20 Latin letters, digits or _. Cost $5000.");
            return 1;
        }
        if (!OrgCreate(playerid, name))
        {
            SendClientMessage(playerid, C_RED, "Could not create organization. Check cash, name and membership.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Organization created. You are the leader.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org info", true)) return OrgShowInfo(playerid);
    if (!strcmp(cmdtext, "/org members", true)) return OrgShowMembers(playerid);
    if (!strcmp(cmdtext, "/org invite ", true, 12))
    {
        if (!OrgCanManage(playerid))
        {
            SendClientMessage(playerid, C_RED, "Only leaders and deputies can invite.");
            return 1;
        }
        new targetText[14], target;
        strmid(targetText, cmdtext, 12, strlen(cmdtext), sizeof targetText);
        if (!ReadTargetId(targetText, target) || target==playerid ||
            gOrganization[target]!=0 || OrgMemberCount(gOrganization[playerid])>=ORG_MEMBER_LIMIT)
        {
            SendClientMessage(playerid, C_RED, "Invalid player, organization full or already a member.");
            return 1;
        }
        new now=gettime();
        if (gLastOrgInvite[playerid]>0 && now-gLastOrgInvite[playerid] < ORG_INVITE_COOLDOWN)
        {
            SendClientMessage(playerid, C_RED, "Wait before inviting another player.");
            return 1;
        }
        gOrgInvite[target]=gOrganization[playerid];
        gOrgInviteUntil[target]=now+ORG_INVITE_SECONDS;
        format(gOrgInviter[target], MAX_PLAYER_NAME+1, "%s", gPlayerName[playerid]);
        gLastOrgInvite[playerid]=now;
        SendClientMessage(target, C_ORANGE, "Organization invitation: /org accept or /org decline (60 seconds).");
        SendClientMessage(playerid, C_GREEN, "Invitation sent.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org accept", true))
    {
        if (!OrgAcceptInvite(playerid))
        {
            SendClientMessage(playerid, C_RED, "Invitation expired, canceled or organization unavailable.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "You joined the organization. Use /org info.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org decline", true))
    {
        gOrgInvite[playerid]=0;
        gOrgInviteUntil[playerid]=0;
        gOrgInviter[playerid][0]=EOS;
        SendClientMessage(playerid, C_WHITE, "Invitation declined.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org leave", true))
    {
        if (!OrgLeave(playerid))
        {
            SendClientMessage(playerid, C_RED, "Members can leave; leaders must transfer ownership or disband.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "You left your organization.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org transfer ", true, 14))
    {
        new targetText[14], target;
        strmid(targetText, cmdtext, 14, strlen(cmdtext), sizeof targetText);
        if (!ReadTargetId(targetText, target) || !OrgTransferLeader(playerid, target))
        {
            SendClientMessage(playerid, C_RED, "Transfer failed: target must be an online organization member.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Leadership transferred.");
        SendClientMessage(target, C_GREEN, "You are the new organization leader.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org disband CONFIRM", true))
    {
        if (!OrgDisband(playerid))
        {
            SendClientMessage(playerid, C_RED, "Only a leader with no other members can disband.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Organization disbanded. The creation fee is not refunded.");
        return 1;
    }
    if (!strcmp(cmdtext, "/org disband", true))
    {
        SendClientMessage(playerid, C_RED, "This action is permanent. Type /org disband CONFIRM");
        return 1;
    }
    if (!strcmp(cmdtext, "/org ", true, 5))
    {
        SendClientMessage(playerid, C_RED, "Unknown organization command. Type /org.");
        return 1;
    }
    return 0;
}
