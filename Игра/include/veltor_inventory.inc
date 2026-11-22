#if defined _veltor_inventory_included
    #endinput
#endif
#define _veltor_inventory_included

// Local test inventory. Item IDs and prices are server-owned, not client-provided.
#define ITEM_MEDKIT 1
#define ITEM_ARMOR 2
#define ITEM_MAX_STACK 10
#define ITEM_MEDKIT_PRICE 300
#define ITEM_ARMOR_PRICE 950
#define ITEM_SHOP_DISTANCE 20.0

stock bool:InventoryExec(const query[])
{
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    db_free_result(r);
    return true;
}

stock bool:InventoryChangedOne()
{
    new DBResult:r = db_query(gDb, "SELECT changes() AS affected");
    if (!r) return false;
    new affected = db_num_rows(r) > 0 ? db_get_field_assoc_int(r, "affected") : 0;
    db_free_result(r);
    return affected == 1;
}

stock bool:InitInventoryDB()
{
    if (!InventoryExec("CREATE TABLE IF NOT EXISTS player_items (name TEXT NOT NULL COLLATE NOCASE, item_id INTEGER NOT NULL CHECK(item_id IN (1,2)), quantity INTEGER NOT NULL DEFAULT 0 CHECK(quantity>=0 AND quantity<=10), PRIMARY KEY(name,item_id))"))
        return false;
    if (!InventoryExec("CREATE TABLE IF NOT EXISTS inventory_events (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, item_id INTEGER NOT NULL, action TEXT NOT NULL, quantity INTEGER NOT NULL, cash_delta INTEGER NOT NULL, created_at INTEGER NOT NULL)"))
        return false;
    return true;
}

stock InventoryPrice(item)
{
    switch (item)
    {
        case ITEM_MEDKIT: return ITEM_MEDKIT_PRICE;
        case ITEM_ARMOR: return ITEM_ARMOR_PRICE;
    }
    return 0;
}

stock bool:ReadInventoryItem(const cmdtext[], offset, &item)
{
    new value[12];
    if (strlen(cmdtext) <= offset || strlen(cmdtext) > offset + 2)
        return false;
    strmid(value, cmdtext, offset, strlen(cmdtext), sizeof value);
    if (!DigitsOnly(value)) return false;
    item = strval(value);
    return item == ITEM_MEDKIT || item == ITEM_ARMOR;
}

stock InventoryQuantity(playerid, item)
{
    if (item != ITEM_MEDKIT && item != ITEM_ARMOR) return -1;
    new query[180];
    format(query, sizeof query,
        "SELECT quantity FROM player_items WHERE name='%s' AND item_id=%d LIMIT 1",
        gPlayerName[playerid], item);
    new DBResult:r = db_query(gDb, query);
    if (!r) return -1;
    new quantity = 0;
    if (db_num_rows(r) > 0)
        quantity = db_get_field_assoc_int(r, "quantity");
    db_free_result(r);
    if (quantity < 0 || quantity > ITEM_MAX_STACK) return -1;
    return quantity;
}

stock bool:InventoryAtShop(playerid)
{
    return !IsPlayerInAnyVehicle(playerid) &&
        GetPlayerInterior(playerid) == 0 &&
        GetPlayerVirtualWorld(playerid) == 0 &&
        IsPlayerInRangeOfPoint(playerid, ITEM_SHOP_DISTANCE,
            START_X, START_Y, START_Z);
}

stock bool:InventoryBuy(playerid, item)
{
    new price = InventoryPrice(item);
    if (price < 1 || gMoney[playerid] < price) return false;
    new quantity = InventoryQuantity(playerid, item);
    if (quantity < 0 || quantity >= ITEM_MAX_STACK) return false;
    new remaining = gMoney[playerid] - price;

    if (!InventoryExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new query[360], bool:ok = true;

    format(query, sizeof query,
        "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
        remaining, gPlayerName[playerid], gMoney[playerid]);
    if (!InventoryExec(query) || !InventoryChangedOne()) ok = false;

    if (ok)
    {
        format(query, sizeof query,
            "INSERT OR IGNORE INTO player_items(name,item_id,quantity) VALUES('%s',%d,0)",
            gPlayerName[playerid], item);
        if (!InventoryExec(query)) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE player_items SET quantity=%d WHERE name='%s' AND item_id=%d AND quantity=%d",
            quantity + 1, gPlayerName[playerid], item, quantity);
        if (!InventoryExec(query) || !InventoryChangedOne()) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('%s',%d,'buy',1,-%d,%d)",
            gPlayerName[playerid], item, price, gettime());
        if (!InventoryExec(query)) ok = false;
    }
    if (ok && InventoryExec("COMMIT"))
    {
        SetCash(playerid, remaining);
        return true;
    }
    InventoryExec("ROLLBACK");
    return false;
}

stock bool:InventoryUse(playerid, item)
{
    new quantity = InventoryQuantity(playerid, item);
    if (quantity <= 0) return false;

    if (!InventoryExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[340];
    format(query, sizeof query,
        "UPDATE player_items SET quantity=%d WHERE name='%s' AND item_id=%d AND quantity=%d",
        quantity - 1, gPlayerName[playerid], item, quantity);
    if (!InventoryExec(query) || !InventoryChangedOne()) ok = false;

    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('%s',%d,'use',1,0,%d)",
            gPlayerName[playerid], item, gettime());
        if (!InventoryExec(query)) ok = false;
    }
    if (ok && InventoryExec("COMMIT")) return true;
    InventoryExec("ROLLBACK");
    return false;
}

stock HandleInventoryCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/shop", true))
    {
        SendClientMessage(playerid, C_ORANGE, "VELTOR SUPPLIES | City Hall");
        SendClientMessage(playerid, C_WHITE, "1 - First aid kit: $300 (+35 health, max 100)");
        SendClientMessage(playerid, C_WHITE, "2 - Armor plate: $950 (+50 armor, max 100)");
        SendClientMessage(playerid, C_WHITE, "Buy nearby on foot: /buyitem ID | Use: /useitem ID");
        return 1;
    }
    if (!strcmp(cmdtext, "/inventory", true))
    {
        new kits = InventoryQuantity(playerid, ITEM_MEDKIT);
        new armor = InventoryQuantity(playerid, ITEM_ARMOR);
        if (kits < 0 || armor < 0)
        {
            SendClientMessage(playerid, C_RED, "Inventory unavailable.");
            return 1;
        }
        new line[128];
        format(line, sizeof line,
            "Inventory | First aid: %d/%d | Armor plates: %d/%d",
            kits, ITEM_MAX_STACK, armor, ITEM_MAX_STACK);
        SendClientMessage(playerid, C_GREEN, line);
        SendClientMessage(playerid, C_WHITE, "/shop /buyitem ID /useitem ID");
        return 1;
    }
    if (!strcmp(cmdtext, "/buyitem", true) || !strcmp(cmdtext, "/useitem", true))
    {
        SendClientMessage(playerid, C_WHITE, "Use /buyitem 1-2 or /useitem 1-2.");
        return 1;
    }
    if (!strcmp(cmdtext, "/buyitem ", true, 9))
    {
        new item;
        if (!ReadInventoryItem(cmdtext, 9, item))
        {
            SendClientMessage(playerid, C_RED, "Unknown item ID. See /shop.");
            return 1;
        }
        if (!InventoryAtShop(playerid))
        {
            SendClientMessage(playerid, C_RED, "Buy supplies at City Hall on foot.");
            return 1;
        }
        if (!InventoryBuy(playerid, item))
        {
            SendClientMessage(playerid, C_RED, "Purchase failed: check cash, inventory capacity and database.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Item purchased and saved. Check /inventory.");
        return 1;
    }
    if (!strcmp(cmdtext, "/useitem ", true, 9))
    {
        new item;
        if (!ReadInventoryItem(cmdtext, 9, item))
        {
            SendClientMessage(playerid, C_RED, "Unknown item ID. See /inventory.");
            return 1;
        }
        if (IsPlayerInAnyVehicle(playerid) ||
            GetPlayerState(playerid) != PLAYER_STATE_ONFOOT)
        {
            SendClientMessage(playerid, C_RED, "Use supplies on foot.");
            return 1;
        }
        new Float:health, Float:armor;
        GetPlayerHealth(playerid, health);
        GetPlayerArmour(playerid, armor);
        if (health <= 0.0)
        {
            SendClientMessage(playerid, C_RED, "Supplies cannot be used while dead.");
            return 1;
        }
        if (item == ITEM_MEDKIT && health >= 100.0)
        {
            SendClientMessage(playerid, C_WHITE, "Your health is already full.");
            return 1;
        }
        if (item == ITEM_ARMOR && armor >= 100.0)
        {
            SendClientMessage(playerid, C_WHITE, "Your armor is already full.");
            return 1;
        }
        if (!InventoryUse(playerid, item))
        {
            SendClientMessage(playerid, C_RED, "You have no item or inventory save failed.");
            return 1;
        }
        if (item == ITEM_MEDKIT)
        {
            health += 35.0;
            if (health > 100.0) health = 100.0;
            SetPlayerHealth(playerid, health);
            SendClientMessage(playerid, C_GREEN, "First aid applied (+35 health, max 100).");
        }
        else
        {
            armor += 50.0;
            if (armor > 100.0) armor = 100.0;
            SetPlayerArmour(playerid, armor);
            SendClientMessage(playerid, C_GREEN, "Armor plate applied (+50 armor, max 100).");
        }
        return 1;
    }
    return 0;
}
