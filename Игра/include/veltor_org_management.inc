#if defined _veltor_org_management_included
    #endinput
#endif
#define _veltor_org_management_included

// Additional rank and treasury remain separate from the legacy member schema.
#define ORG_MAX_DEPUTIES 3
#define ORG_TREASURY_LIMIT 100000000
#define ORG_TREASURY_TX_LIMIT 50000
#define ORG_TREASURY_COOLDOWN 5

stock bool:OrgManagementInit()
{
    if (!OrgExec("CREATE TABLE IF NOT EXISTS organization_officers (player TEXT PRIMARY KEY COLLATE NOCASE, org_id INTEGER NOT NULL, appointed_at INTEGER NOT NULL)")) return false;
    if (!OrgExec("CREATE INDEX IF NOT EXISTS idx_org_officers_org ON organization_officers(org_id)")) return false;
    if (!OrgExec("CREATE TABLE IF NOT EXISTS organization_treasury (org_id INTEGER PRIMARY KEY, balance INTEGER NOT NULL DEFAULT 0 CHECK(balance>=0 AND balance<=100000000))")) return false;
    if (!OrgExec("CREATE TABLE IF NOT EXISTS organization_treasury_history (id INTEGER PRIMARY KEY AUTOINCREMENT, org_id INTEGER NOT NULL, actor TEXT NOT NULL, action TEXT NOT NULL, amount INTEGER NOT NULL CHECK(amount BETWEEN 1 AND 50000), balance_after INTEGER NOT NULL, created_at INTEGER NOT NULL)")) return false;
    // Add a zero-balance treasury for organizations created by older builds.
    return OrgExec("INSERT OR IGNORE INTO organization_treasury(org_id,balance) SELECT id,0 FROM organizations");
}

stock bool:OrgEnsureTreasury(orgid)
{
    new query[165];
    format(query,sizeof query,
        "INSERT OR IGNORE INTO organization_treasury(org_id,balance) VALUES(%d,0)",orgid);
    return OrgExec(query);
}

stock OrgTreasuryBalance(orgid)
{
    if (orgid<=0) return -1;
    new query[180];
    format(query,sizeof query,
        "SELECT balance FROM organization_treasury WHERE org_id=%d LIMIT 1",orgid);
    new DBResult:r=db_query(gDb,query);
    if (!r) return -1;
    new balance=-1;
    if (db_num_rows(r)>0) balance=db_get_field_assoc_int(r,"balance");
    db_free_result(r);
    return balance>=0 && balance<=ORG_TREASURY_LIMIT ? balance : -1;
}

stock bool:OrgIsDeputy(playerid)
{
    if (gOrganization[playerid]<=0) return false;
    new query[350];
    format(query,sizeof query,
        "SELECT 1 FROM organization_officers o JOIN organization_members m ON m.player=o.player AND m.org_id=o.org_id WHERE o.player='%s' AND o.org_id=%d AND m.role=0 LIMIT 1",
        gPlayerName[playerid],gOrganization[playerid]);
    new DBResult:r=db_query(gDb,query);
    if (!r) return false;
    new exists=db_num_rows(r)>0;
    db_free_result(r);
    return exists;
}

stock bool:OrgCanManage(playerid)
{
    return OrgIsLeader(playerid) || OrgIsDeputy(playerid);
}

stock bool:OrgInviterAuthorized(orgid,const name[])
{
    if (OrgOwnerMatches(orgid,name)) return true;
    new query[350];
    format(query,sizeof query,
        "SELECT 1 FROM organization_officers o JOIN organization_members m ON m.player=o.player AND m.org_id=o.org_id WHERE o.player='%s' AND o.org_id=%d AND m.role=0 LIMIT 1",
        name,orgid);
    new DBResult:r=db_query(gDb,query);
    if (!r) return false;
    new exists=db_num_rows(r)>0;
    db_free_result(r);
    return exists;
}

stock OrgDeputyCount(orgid)
{
    new query[160];
    format(query,sizeof query,
        "SELECT COUNT(*) AS total FROM organization_officers WHERE org_id=%d",orgid);
    new DBResult:r=db_query(gDb,query);
    if (!r) return -1;
    new count=db_num_rows(r)>0 ? db_get_field_assoc_int(r,"total") : -1;
    db_free_result(r);
    return count;
}

stock bool:OrgPromoteDeputy(playerid,target)
{
    if (!OrgIsLeader(playerid) || playerid==target || !gAuthed[target] ||
        gOrganization[playerid]<=0 || gOrganization[playerid]!=gOrganization[target])
        return false;
    new count=OrgDeputyCount(gOrganization[playerid]);
    if (count<0 || count>=ORG_MAX_DEPUTIES) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new query[300];
    format(query,sizeof query,
        "INSERT INTO organization_officers(player,org_id,appointed_at) SELECT player,org_id,%d FROM organization_members WHERE player='%s' AND org_id=%d AND role=0",
        gettime(),gPlayerName[target],gOrganization[playerid]);
    if (OrgExec(query) && OrgChangedOne() && OrgExec("COMMIT")) return true;
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgDemoteDeputy(playerid,target)
{
    if (!OrgIsLeader(playerid) || playerid==target || !gAuthed[target] ||
        gOrganization[playerid]<=0 || gOrganization[playerid]!=gOrganization[target])
        return false;
    new query[240];
    format(query,sizeof query,
        "DELETE FROM organization_officers WHERE player='%s' AND org_id=%d",
        gPlayerName[target],gOrganization[playerid]);
    return OrgExec(query) && OrgChangedOne();
}

stock bool:OrgKickMember(playerid,target)
{
    if (!OrgCanManage(playerid) || playerid==target || !gAuthed[target] ||
        gOrganization[playerid]<=0 || gOrganization[playerid]!=gOrganization[target] ||
        OrgIsLeader(target)) return false;
    if (!OrgIsLeader(playerid) && OrgIsDeputy(target)) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok=true,query[260],orgid=gOrganization[playerid];
    format(query,sizeof query,
        "DELETE FROM organization_officers WHERE player='%s' AND org_id=%d",
        gPlayerName[target],orgid);
    if (!OrgExec(query)) ok=false;
    if (ok)
    {
        format(query,sizeof query,
            "DELETE FROM organization_members WHERE player='%s' AND org_id=%d AND role=0",
            gPlayerName[target],orgid);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        gOrganization[target]=0;
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock bool:OrgTreasuryTransaction(playerid,bool:deposit,amount)
{
    new orgid=gOrganization[playerid];
    if (orgid<=0 || amount<1 || amount>ORG_TREASURY_TX_LIMIT ||
        !BankAtATM(playerid)) return false;
    if (!deposit && !OrgIsLeader(playerid)) return false;
    if (!OrgExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new previous=OrgTreasuryBalance(orgid),cash=gMoney[playerid],balance=previous;
    new bool:ok=previous>=0 && cash>=0 && cash<=ORG_TREASURY_LIMIT;
    if (deposit)
    {
        if (cash<amount || previous>ORG_TREASURY_LIMIT-amount) ok=false;
        else {cash-=amount; balance+=amount;}
    }
    else
    {
        if (previous<amount || cash>ORG_TREASURY_LIMIT-amount) ok=false;
        else {cash+=amount; balance-=amount;}
    }
    new query[360];
    if (ok)
    {
        format(query,sizeof query,
            "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
            cash,gPlayerName[playerid],gMoney[playerid]);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok)
    {
        format(query,sizeof query,
            "UPDATE organization_treasury SET balance=%d WHERE org_id=%d AND balance=%d",
            balance,orgid,previous);
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok)
    {
        format(query,sizeof query,
            "INSERT INTO organization_treasury_history(org_id,actor,action,amount,balance_after,created_at) VALUES(%d,'%s','%s',%d,%d,%d)",
            orgid,gPlayerName[playerid],deposit ? ("deposit"):("withdraw"),amount,balance,gettime());
        if (!OrgExec(query) || !OrgChangedOne()) ok=false;
    }
    if (ok && OrgExec("COMMIT"))
    {
        SetCash(playerid,cash);
        return true;
    }
    OrgExec("ROLLBACK");
    return false;
}

stock OrgTreasuryHistory(playerid)
{
    if (gOrganization[playerid]<=0) return 1;
    new query[230];
    format(query,sizeof query,
        "SELECT actor,action,amount FROM organization_treasury_history WHERE org_id=%d ORDER BY id DESC LIMIT 5",
        gOrganization[playerid]);
    new DBResult:r=db_query(gDb,query);
    if (!r) return 1;
    new count=db_num_rows(r);
    if (count==0) SendClientMessage(playerid,C_WHITE,"Treasury history is empty.");
    else
    {
        SendClientMessage(playerid,C_ORANGE,"Last five treasury transactions:");
        for (new i=0;i<count;i++)
        {
            new actor[MAX_PLAYER_NAME+1],action[12],line[140];
            db_get_field_assoc(r,"actor",actor,sizeof actor);
            db_get_field_assoc(r,"action",action,sizeof action);
            format(line,sizeof line,"%s | %s | $%d",
                actor,action,db_get_field_assoc_int(r,"amount"));
            SendClientMessage(playerid,C_WHITE,line);
            db_next_row(r);
        }
    }
    db_free_result(r);
    return 1;
}

stock bool:OrgParseAmount(const text[],offset,&amount)
{
    new digits[12];
    if (strlen(text)<=offset || strlen(text)>offset+5) return false;
    strmid(digits,text,offset,strlen(text),sizeof digits);
    if (!DigitsOnly(digits)) return false;
    amount=strval(digits);
    return amount>=1 && amount<=ORG_TREASURY_TX_LIMIT;
}

stock HandleOrgManagementCommand(playerid,const cmdtext[])
{
    if (!strcmp(cmdtext,"/org treasury",true))
    {
        if (gOrganization[playerid]<=0)
        {
            SendClientMessage(playerid,C_RED,"Join an organization first.");
            return 1;
        }
        new money=OrgTreasuryBalance(gOrganization[playerid]),line[136];
        if (money<0) SendClientMessage(playerid,C_RED,"Treasury is unavailable.");
        else
        {
            format(line,sizeof line,"Organization treasury: $%d | Your cash: $%d",
                money,gMoney[playerid]);
            SendClientMessage(playerid,C_GREEN,line);
        }
        SendClientMessage(playerid,C_WHITE,
            "At the City Hall ATM: /org deposit AMOUNT (members) | /org withdraw AMOUNT (leader)");
        return 1;
    }
    if (!strcmp(cmdtext,"/org ledger",true))
    {
        if (gOrganization[playerid]<=0)
        {
            SendClientMessage(playerid,C_RED,"Join an organization first.");
            return 1;
        }
        return OrgTreasuryHistory(playerid);
    }
    new bool:deposit;
    new offset=0;
    if (!strcmp(cmdtext,"/org deposit ",true,13))
    {
        deposit=true;
        offset=13;
    }
    else if (!strcmp(cmdtext,"/org withdraw ",true,14))
    {
        deposit=false;
        offset=14;
    }
    if (offset)
    {
        new amount,now=gettime();
        if (!OrgParseAmount(cmdtext,offset,amount))
        {
            SendClientMessage(playerid,C_RED,"Amount must be 1-50000.");
            return 1;
        }
        if (gLastOrgTreasury[playerid]>0 &&
            now-gLastOrgTreasury[playerid]<ORG_TREASURY_COOLDOWN)
        {
            SendClientMessage(playerid,C_RED,"Please wait before another treasury operation.");
            return 1;
        }
        if (!OrgTreasuryTransaction(playerid,deposit,amount))
        {
            SendClientMessage(playerid,C_RED,"Treasury operation failed. Visit City Hall ATM on foot.");
            return 1;
        }
        gLastOrgTreasury[playerid]=now;
        SendClientMessage(playerid,C_GREEN,
            deposit ? ("Organization deposit saved.") : ("Organization withdrawal saved."));
        return 1;
    }
    if (!strcmp(cmdtext,"/org deposit",true) || !strcmp(cmdtext,"/org withdraw",true))
    {
        SendClientMessage(playerid,C_WHITE,"Usage: /org deposit 100 or /org withdraw 100");
        return 1;
    }

    new action=0,target=INVALID_PLAYER_ID;
    if (!strcmp(cmdtext,"/org promote ",true,13)) action=1;
    else if (!strcmp(cmdtext,"/org demote ",true,12)) action=2;
    else if (!strcmp(cmdtext,"/org kick ",true,10)) action=3;
    if (action)
    {
        new start=action==1 ? 13 : (action==2 ? 12 : 10),value[12];
        strmid(value,cmdtext,start,strlen(cmdtext),sizeof value);
        if (!ReadTargetId(value,target))
        {
            SendClientMessage(playerid,C_RED,"Specify an online member ID.");
            return 1;
        }
        new bool:ok=false;
        switch (action)
        {
            case 1: ok=OrgPromoteDeputy(playerid,target);
            case 2: ok=OrgDemoteDeputy(playerid,target);
            case 3: ok=OrgKickMember(playerid,target);
        }
        if (!ok)
        {
            SendClientMessage(playerid,C_RED,"Action rejected. Check your rank, target and limits.");
            return 1;
        }
        SendClientMessage(playerid,C_GREEN,"Organization roles or membership updated.");
        SendClientMessage(target,C_ORANGE,"Your organization rank or membership has changed.");
        return 1;
    }
    return 0;
}
