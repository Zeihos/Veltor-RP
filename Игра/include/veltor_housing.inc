#if defined _veltor_housing_included
    #endinput
#endif
#define _veltor_housing_included

// Three development properties. Location and interior access require client testing.
#define HOUSE_COUNT 3
#define HOUSE_ICON 8
#define HOUSE_INTERIOR 3
#define HOUSE_INSIDE_X 235.508
#define HOUSE_INSIDE_Y 1189.169
#define HOUSE_INSIDE_Z 1080.339

new Float:HouseX[HOUSE_COUNT+1] = {0.0, 1767.6, 2150.2, 1298.7};
new Float:HouseY[HOUSE_COUNT+1] = {0.0, -1917.1, -1800.2, -798.5};
new Float:HouseZ[HOUSE_COUNT+1] = {0.0, 13.5, 13.4, 84.1};
new HousePrice[HOUSE_COUNT+1] = {0, 5000, 12000, 25000};

stock bool:HouseExec(const sql[])
{
    new DBResult:r = db_query(gDb, sql);
    if (!r) return false;
    db_free_result(r);
    return true;
}

stock bool:HouseAffectedOne()
{
    new DBResult:r = db_query(gDb, "SELECT changes() AS affected");
    if (!r) return false;
    new affected = db_num_rows(r) > 0 ? db_get_field_assoc_int(r, "affected") : 0;
    db_free_result(r);
    return affected == 1;
}

stock bool:InitHousingDB()
{
    if (!HouseExec("CREATE TABLE IF NOT EXISTS houses (id INTEGER PRIMARY KEY, price INTEGER NOT NULL, owner TEXT UNIQUE COLLATE NOCASE, locked INTEGER NOT NULL DEFAULT 1, purchased_at INTEGER NOT NULL DEFAULT 0)"))
        return false;
    new query[200];
    for (new i=1; i<=HOUSE_COUNT; i++)
    {
        format(query, sizeof query,
            "INSERT OR IGNORE INTO houses(id,price,owner,locked,purchased_at) VALUES(%d,%d,NULL,1,0)",
            i, HousePrice[i]);
        if (!HouseExec(query)) return false;
    }
    return true;
}

stock bool:LoadHouse(playerid)
{
    gOwnedHouse[playerid] = 0;
    new query[180];
    format(query, sizeof query,
        "SELECT id FROM houses WHERE owner='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    if (db_num_rows(r) > 0)
    {
        new id = db_get_field_assoc_int(r, "id");
        if (id < 1 || id > HOUSE_COUNT)
        {
            db_free_result(r);
            return false;
        }
        gOwnedHouse[playerid] = id;
    }
    db_free_result(r);
    return true;
}

stock bool:ReadHouseId(const cmdtext[], offset, &houseid)
{
    new val[12];
    if (strlen(cmdtext) <= offset || strlen(cmdtext) > offset + 2) return false;
    strmid(val, cmdtext, offset, strlen(cmdtext), sizeof val);
    if (!DigitsOnly(val)) return false;
    houseid = strval(val);
    return houseid >= 1 && houseid <= HOUSE_COUNT;
}

stock bool:HouseAtDoor(playerid, houseid)
{
    if (houseid < 1 || houseid > HOUSE_COUNT) return false;
    return GetPlayerInterior(playerid) == 0 &&
        GetPlayerVirtualWorld(playerid) == 0 &&
        !IsPlayerInAnyVehicle(playerid) &&
        IsPlayerInRangeOfPoint(playerid, 6.0,
            HouseX[houseid], HouseY[houseid], HouseZ[houseid]);
}

stock bool:HouseOwnerIs(playerid, houseid)
{
    if (gOwnedHouse[playerid] != houseid) return false;
    new query[190], owner[MAX_PLAYER_NAME+1];
    format(query, sizeof query, "SELECT owner FROM houses WHERE id=%d", houseid);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    new bool:matched = false;
    if (db_num_rows(r) > 0)
    {
        db_get_field_assoc(r, "owner", owner, sizeof owner);
        matched = !strcmp(owner, gPlayerName[playerid], true);
    }
    db_free_result(r);
    return matched;
}

stock bool:HouseCanEnter(playerid, houseid)
{
    new query[190], owner[MAX_PLAYER_NAME+1];
    format(query, sizeof query, "SELECT owner,locked FROM houses WHERE id=%d", houseid);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    new bool:allowed = false;
    if (db_num_rows(r) > 0)
    {
        db_get_field_assoc(r, "owner", owner, sizeof owner);
        new locked = db_get_field_assoc_int(r, "locked");
        allowed = owner[0] != EOS &&
            (!strcmp(owner,gPlayerName[playerid],true) || !locked);
    }
    db_free_result(r);
    return allowed;
}

stock bool:CommitHouseBuy(playerid, houseid)
{
    if (houseid < 1 || houseid > HOUSE_COUNT ||
        gOwnedHouse[playerid] != 0 || gMoney[playerid] < HousePrice[houseid]) return false;
    new balance = gMoney[playerid] - HousePrice[houseid];
    if (!HouseExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[320];
    format(query, sizeof query,
        "UPDATE houses SET owner='%s',locked=1,purchased_at=%d WHERE id=%d AND owner IS NULL AND price=%d",
        gPlayerName[playerid], gettime(), houseid, HousePrice[houseid]);
    if (!HouseExec(query) || !HouseAffectedOne()) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
            balance, gPlayerName[playerid], gMoney[playerid]);
        if (!HouseExec(query) || !HouseAffectedOne()) ok = false;
    }
    if (ok && HouseExec("COMMIT"))
    {
        gOwnedHouse[playerid] = houseid;
        SetCash(playerid, balance);
        return true;
    }
    HouseExec("ROLLBACK");
    return false;
}

stock bool:CommitHouseSell(playerid)
{
    new houseid = gOwnedHouse[playerid];
    if (houseid < 1 || houseid > HOUSE_COUNT) return false;
    new resale = HousePrice[houseid] / 2;
    if (gMoney[playerid] > 100000000 - resale) return false;
    new balance = gMoney[playerid] + resale;
    if (!HouseExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[320];
    format(query, sizeof query,
        "UPDATE houses SET owner=NULL,locked=1,purchased_at=0 WHERE id=%d AND owner='%s' AND price=%d",
        houseid, gPlayerName[playerid], HousePrice[houseid]);
    if (!HouseExec(query) || !HouseAffectedOne()) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
            balance, gPlayerName[playerid], gMoney[playerid]);
        if (!HouseExec(query) || !HouseAffectedOne()) ok = false;
    }
    if (ok && HouseExec("COMMIT"))
    {
        gOwnedHouse[playerid] = 0;
        RemovePlayerMapIcon(playerid, HOUSE_ICON);
        SetCash(playerid, balance);
        return true;
    }
    HouseExec("ROLLBACK");
    return false;
}

stock HandleHousingCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/houses", true))
    {
        SendClientMessage(playerid, C_ORANGE, "Available test properties (visit the door to buy):");
        for (new id=1; id<=HOUSE_COUNT; id++)
        {
            new msg[140];
            format(msg, sizeof msg, "House #%d | Price $%d | Location %.0f, %.0f",
                id, HousePrice[id], HouseX[id], HouseY[id]);
            SendClientMessage(playerid, C_WHITE, msg);
        }
        SendClientMessage(playerid, C_WHITE,
            "Commands: /house ID /buyhouse ID /home /enterhome /exithome /hlock /sellhouse");
        return 1;
    }
    if (!strcmp(cmdtext, "/house ", true, 7))
    {
        new houseid;
        if (!ReadHouseId(cmdtext, 7, houseid))
        {
            SendClientMessage(playerid, C_RED, "Usage: /house ID (1-3)");
            return 1;
        }
        new query[100], owner[MAX_PLAYER_NAME+1], msg[160];
        format(query, sizeof query, "SELECT owner,locked FROM houses WHERE id=%d", houseid);
        new DBResult:r = db_query(gDb, query);
        if (!r)
        {
            SendClientMessage(playerid, C_RED, "Housing database unavailable.");
            return 1;
        }
        owner[0] = EOS;
        new locked = 1;
        if (db_num_rows(r) > 0)
        {
            db_get_field_assoc(r, "owner", owner, sizeof owner);
            locked = db_get_field_assoc_int(r, "locked");
        }
        db_free_result(r);
        format(msg, sizeof msg, "House #%d | $%d | %s | %s",
            houseid, HousePrice[houseid],
            owner[0] ? owner : ("For sale"), locked ? ("locked") : ("unlocked"));
        SendClientMessage(playerid, C_GREEN, msg);
        SetPlayerMapIcon(playerid, HOUSE_ICON, HouseX[houseid], HouseY[houseid],
            HouseZ[houseid], 31, C_ORANGE, MAPICON_LOCAL);
        return 1;
    }
    if (!strcmp(cmdtext, "/buyhouse ", true, 10))
    {
        new houseid;
        if (!ReadHouseId(cmdtext, 10, houseid) || !HouseAtDoor(playerid, houseid))
        {
            SendClientMessage(playerid, C_RED, "Enter a valid house ID and stand at its door.");
            return 1;
        }
        if (!CommitHouseBuy(playerid, houseid))
        {
            SendClientMessage(playerid, C_RED, "Property unavailable, already owned or insufficient cash.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "House purchased. Use /home to mark it on your map.");
        return 1;
    }
    if (!strcmp(cmdtext, "/home", true))
    {
        if (gOwnedHouse[playerid] < 1)
        {
            SendClientMessage(playerid, C_WHITE, "You do not own a house. See /houses.");
            return 1;
        }
        new id = gOwnedHouse[playerid];
        SetPlayerMapIcon(playerid, HOUSE_ICON, HouseX[id], HouseY[id],
            HouseZ[id], 31, C_ORANGE, MAPICON_LOCAL);
        SendClientMessage(playerid, C_GREEN, "Your house is marked on the map.");
        return 1;
    }
    if (!strcmp(cmdtext, "/sellhouse", true))
    {
        new id = gOwnedHouse[playerid];
        if (!HouseAtDoor(playerid, id) || !HouseOwnerIs(playerid, id))
        {
            SendClientMessage(playerid, C_RED, "Stand at the door of your own house to sell.");
            return 1;
        }
        if (!CommitHouseSell(playerid))
        {
            SendClientMessage(playerid, C_RED, "House sale failed. No money or ownership was changed.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "House sold for 50% of its purchase price.");
        return 1;
    }
    if (!strcmp(cmdtext, "/hlock", true))
    {
        new id = gOwnedHouse[playerid];
        if (!HouseAtDoor(playerid, id) || !HouseOwnerIs(playerid, id))
        {
            SendClientMessage(playerid, C_RED, "Stand at your house door to change its lock.");
            return 1;
        }
        new query[150];
        format(query, sizeof query, "SELECT locked FROM houses WHERE id=%d", id);
        new DBResult:r = db_query(gDb, query);
        if (!r || db_num_rows(r) < 1)
        {
            if (r) db_free_result(r);
            return 1;
        }
        new locked = db_get_field_assoc_int(r, "locked") ? 0 : 1;
        db_free_result(r);
        format(query, sizeof query,
            "UPDATE houses SET locked=%d WHERE id=%d AND owner='%s'",
            locked, id, gPlayerName[playerid]);
        if (!HouseExec(query))
        {
            SendClientMessage(playerid, C_RED, "Could not update lock.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN,
            locked ? "House locked." : "House unlocked for visitors.");
        return 1;
    }
    if (!strcmp(cmdtext, "/enterhome", true))
    {
        if (gInsideHouse[playerid] != 0)
        {
            SendClientMessage(playerid, C_RED, "You are already indoors.");
            return 1;
        }
        new id = 0;
        for (new i=1;i<=HOUSE_COUNT;i++)
        {
            if (HouseAtDoor(playerid, i))
            {
                id = i;
                break;
            }
        }
        if (id == 0 || !HouseCanEnter(playerid, id))
        {
            SendClientMessage(playerid, C_RED, "No accessible house door nearby (private homes may be locked).");
            return 1;
        }
        CancelJobShift(playerid);
        gInsideHouse[playerid] = id;
        SetPlayerInterior(playerid, HOUSE_INTERIOR);
        SetPlayerVirtualWorld(playerid, 2000+id);
        SetPlayerPos(playerid, HOUSE_INSIDE_X,HOUSE_INSIDE_Y,HOUSE_INSIDE_Z);
        SendClientMessage(playerid, C_GREEN, "Welcome inside. Use /exithome to leave.");
        return 1;
    }
    if (!strcmp(cmdtext, "/exithome", true))
    {
        new id = gInsideHouse[playerid];
        if (id < 1 || id > HOUSE_COUNT)
        {
            SendClientMessage(playerid, C_RED, "You are not inside a property.");
            return 1;
        }
        gInsideHouse[playerid] = 0;
        SetPlayerInterior(playerid, 0);
        SetPlayerVirtualWorld(playerid, 0);
        SetPlayerPos(playerid, HouseX[id],HouseY[id],HouseZ[id]);
        SendClientMessage(playerid, C_GREEN, "You left the house.");
        return 1;
    }
    return 0;
}
