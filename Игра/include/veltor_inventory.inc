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
    if (!InventoryExec("CREATE TABLE IF NOT EXISTS item_transfers (id INTEGER PRIMARY KEY AUTOINCREMENT, sender TEXT NOT NULL, receiver TEXT NOT NULL, item_id INTEGER NOT NULL CHECK(item_id IN (1,2)), quantity INTEGER NOT NULL CHECK(quantity BETWEEN 1 AND 5), created_at INTEGER NOT NULL)"))
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


// Player-to-player gifts are performed atomically: remove from sender, add to
// recipient, then record both events before COMMIT. Never trust the client count.
#define ITEM_GIVE_RANGE 6.0
#define ITEM_GIVE_LIMIT 5
#define ITEM_GIVE_COOLDOWN 10

stock bool:InventoryTransferNear(sender, recipient)
{
    if (sender == recipient || recipient < 0 || recipient >= MAX_PLAYERS ||
        !IsPlayerConnected(recipient) || !gAuthed[recipient])
        return false;
    if (GetPlayerState(sender) != PLAYER_STATE_ONFOOT ||
        GetPlayerState(recipient) != PLAYER_STATE_ONFOOT)
        return false;
    if (GetPlayerVirtualWorld(sender) != GetPlayerVirtualWorld(recipient) ||
        GetPlayerInterior(sender) != GetPlayerInterior(recipient))
        return false;
    new Float:x, Float:y, Float:z;
    GetPlayerPos(recipient, x, y, z);
    return IsPlayerInRangeOfPoint(sender, ITEM_GIVE_RANGE, x, y, z);
}

stock bool:ParseItemTransfer(const cmdtext[], &recipient, &item, &quantity)
{
    // Exactly three numeric arguments: target ID, item ID, amount.
    // Short fixed buffers also prevent integer overflow through strval().
    new args[36], idText[8], itemText[8], qtyText[8];
    if (strlen(cmdtext) <= 10 || strlen(cmdtext) >= 36) return false;
    strmid(args, cmdtext, 10, strlen(cmdtext), sizeof args);
    new first = strfind(args, " ");
    if (first < 1) return false;
    new second = strfind(args, " ", false, first + 1);
    if (second <= first + 1) return false;
    strmid(idText, args, 0, first, sizeof idText);
    strmid(itemText, args, first + 1, second, sizeof itemText);
    strmid(qtyText, args, second + 1, strlen(args), sizeof qtyText);
    if (!DigitsOnly(idText) || strlen(idText) > 4 ||
        !DigitsOnly(itemText) || strlen(itemText) > 1 ||
        !DigitsOnly(qtyText) || strlen(qtyText) > 1)
        return false;
    recipient = strval(idText);
    item = strval(itemText);
    quantity = strval(qtyText);
    return recipient >= 0 && recipient < MAX_PLAYERS &&
        (item == ITEM_MEDKIT || item == ITEM_ARMOR) &&
        quantity >= 1 && quantity <= ITEM_GIVE_LIMIT;
}

stock bool:InventoryTransfer(sender, recipient, item, quantity)
{
    if (!InventoryTransferNear(sender, recipient) ||
        (item != ITEM_MEDKIT && item != ITEM_ARMOR) ||
        quantity < 1 || quantity > ITEM_GIVE_LIMIT)
        return false;

    new owned = InventoryQuantity(sender, item);
    new available = InventoryQuantity(recipient, item);
    if (owned < quantity || available < 0 ||
        available > ITEM_MAX_STACK - quantity)
        return false;

    if (!InventoryExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[440];

    format(query, sizeof query,
        "UPDATE player_items SET quantity=%d WHERE name='%s' AND item_id=%d AND quantity=%d",
        owned - quantity, gPlayerName[sender], item, owned);
    if (!InventoryExec(query) || !InventoryChangedOne()) ok = false;

    if (ok)
    {
        format(query, sizeof query,
            "INSERT OR IGNORE INTO player_items(name,item_id,quantity) VALUES('%s',%d,0)",
            gPlayerName[recipient], item);
        if (!InventoryExec(query)) ok = false;
    }

    if (ok)
    {
        format(query, sizeof query,
            "UPDATE player_items SET quantity=%d WHERE name='%s' AND item_id=%d AND quantity=%d",
            available + quantity, gPlayerName[recipient], item, available);
        if (!InventoryExec(query) || !InventoryChangedOne()) ok = false;
    }

    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO item_transfers(sender,receiver,item_id,quantity,created_at) VALUES('%s','%s',%d,%d,%d)",
            gPlayerName[sender], gPlayerName[recipient], item, quantity, gettime());
        if (!InventoryExec(query)) ok = false;
    }

    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('%s',%d,'give',%d,0,%d)",
            gPlayerName[sender], item, quantity, gettime());
        if (!InventoryExec(query)) ok = false;
    }

    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO inventory_events(name,item_id,action,quantity,cash_delta,created_at) VALUES('%s',%d,'receive',%d,0,%d)",
            gPlayerName[recipient], item, quantity, gettime());
        if (!InventoryExec(query)) ok = false;
    }

    if (ok && InventoryExec("COMMIT")) return true;
    InventoryExec("ROLLBACK");
    return false;
}

stock ShowInventoryHistory(playerid)
{
    new query[220];
    format(query, sizeof query,
        "SELECT action,item_id,quantity FROM inventory_events WHERE name='%s' ORDER BY id DESC LIMIT 5",
        gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r)
    {
        SendClientMessage(playerid, C_RED, "Inventory history unavailable.");
        return 1;
    }
    new count = db_num_rows(r);
    if (count == 0) SendClientMessage(playerid, C_WHITE, "No inventory activity yet.");
    else
    {
        SendClientMessage(playerid, C_ORANGE, "Last 5 inventory operations:");
        for (new i=0; i<count; i++)
        {
            new action[18], line[100];
            db_get_field_assoc(r, "action", action, sizeof action);
            format(line, sizeof line, "%s | item %d | amount %d",
                action, db_get_field_assoc_int(r, "item_id"),
                db_get_field_assoc_int(r, "quantity"));
            SendClientMessage(playerid, C_WHITE, line);
            db_next_row(r);
        }
    }
    db_free_result(r);
    return 1;
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
        SendClientMessage(playerid, C_WHITE, "/shop /buyitem ID /useitem ID /giveitem ID ITEM QTY");
        SendClientMessage(playerid, C_WHITE, "/itemhistory shows your last 5 inventory actions.");
        return 1;
    }
    if (!strcmp(cmdtext, "/itemhistory", true))
        return ShowInventoryHistory(playerid);
    if (!strcmp(cmdtext, "/giveitem", true))
    {
        SendClientMessage(playerid, C_WHITE, "Usage: /giveitem PLAYER_ID ITEM_ID QUANTITY (1-5)");
        return 1;
    }
    if (!strcmp(cmdtext, "/giveitem ", true, 10))
    {
        new target, item, quantity;
        if (!ParseItemTransfer(cmdtext, target, item, quantity))
        {
            SendClientMessage(playerid, C_RED, "Usage: /giveitem PLAYER_ID ITEM_ID QUANTITY (1-5)");
            return 1;
        }
        if (!InventoryTransferNear(playerid, target))
        {
            SendClientMessage(playerid, C_RED, "Target must be online, near you and in the same world, on foot.");
            return 1;
        }
        new now = gettime();
        if (gLastItemGive[playerid] > 0 &&
            now - gLastItemGive[playerid] < ITEM_GIVE_COOLDOWN)
        {
            SendClientMessage(playerid, C_RED, "Wait before transferring another item.");
            return 1;
        }
        if (!InventoryTransfer(playerid, target, item, quantity))
        {
            SendClientMessage(playerid, C_RED, "Transfer failed. Check quantities, recipient capacity and database.");
            return 1;
        }
        gLastItemGive[playerid] = now;
        new line[144];
        format(line, sizeof line, "Sent item %d x%d to %s.",
            item, quantity, gPlayerName[target]);
        SendClientMessage(playerid, C_GREEN, line);
        format(line, sizeof line, "Received item %d x%d from %s.",
            item, quantity, gPlayerName[playerid]);
        SendClientMessage(target, C_GREEN, line);
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
