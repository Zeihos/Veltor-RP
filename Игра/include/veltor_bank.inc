#if defined _veltor_bank_included
    #endinput
#endif
#define _veltor_bank_included

// Test banking system: cash and bank deposits are separate, with atomic updates.
#define BANK_LIMIT 100000000
#define BANK_ACTION_LIMIT 50000
#define BANK_ATM_X 1481.0
#define BANK_ATM_Y -1739.0
#define BANK_ATM_Z 13.55
#define BANK_MAP_ICON 7

stock bool:BankExec(const sql[])
{
    new DBResult:result = db_query(gDb, sql);
    if (!result) return false;
    db_free_result(result);
    return true;
}

stock bool:BankUpdatedOneRow()
{
    new DBResult:result = db_query(gDb, "SELECT changes() AS affected");
    if (!result) return false;
    new count = db_num_rows(result) > 0 ? db_get_field_assoc_int(result, "affected") : 0;
    db_free_result(result);
    return count == 1;
}

stock bool:InitBankDB()
{
    if (!BankExec("CREATE TABLE IF NOT EXISTS bank_accounts (name TEXT PRIMARY KEY COLLATE NOCASE, balance INTEGER NOT NULL DEFAULT 0 CHECK(balance>=0 AND balance<=100000000))"))
        return false;
    if (!BankExec("CREATE TABLE IF NOT EXISTS bank_history (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, action TEXT NOT NULL, amount INTEGER NOT NULL, created_at INTEGER NOT NULL)"))
        return false;
    if (!BankExec("CREATE TABLE IF NOT EXISTS cash_transfers (id INTEGER PRIMARY KEY AUTOINCREMENT, sender TEXT NOT NULL, receiver TEXT NOT NULL, amount INTEGER NOT NULL, created_at INTEGER NOT NULL)"))
        return false;
    return true;
}

stock bool:LoadBank(playerid)
{
    gBankBalance[playerid] = 0;
    new query[220];
    format(query, sizeof query, "INSERT OR IGNORE INTO bank_accounts(name,balance) VALUES('%s',0)", gPlayerName[playerid]);
    if (!BankExec(query)) return false;
    format(query, sizeof query, "SELECT balance FROM bank_accounts WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:result = db_query(gDb, query);
    if (!result) return false;
    if (db_num_rows(result) < 1)
    {
        db_free_result(result);
        return false;
    }
    new amount = db_get_field_assoc_int(result, "balance");
    db_free_result(result);
    if (amount < 0 || amount > BANK_LIMIT) return false;
    gBankBalance[playerid] = amount;
    return true;
}

stock bool:BankAtATM(playerid)
{
    return GetPlayerInterior(playerid) == 0 &&
        GetPlayerVirtualWorld(playerid) == 0 &&
        !IsPlayerInAnyVehicle(playerid) &&
        IsPlayerInRangeOfPoint(playerid, 12.0, BANK_ATM_X, BANK_ATM_Y, BANK_ATM_Z);
}

stock bool:ParseBankAmount(const command[], offset, &amount)
{
    new value[20];
    if (strlen(command) <= offset || strlen(command) > offset + 8) return false;
    strmid(value, command, offset, strlen(command), sizeof value);
    if (!DigitsOnly(value)) return false;
    amount = strval(value);
    return amount >= 1 && amount <= BANK_ACTION_LIMIT;
}

stock bool:CommitBankOperation(playerid, bool:deposit, amount)
{
    if (amount < 1 || amount > BANK_ACTION_LIMIT) return false;
    new newCash = gMoney[playerid], newBalance = gBankBalance[playerid];
    if (deposit)
    {
        if (amount > newCash || newBalance > BANK_LIMIT - amount) return false;
        newCash -= amount;
        newBalance += amount;
    }
    else
    {
        if (amount > newBalance || newCash > BANK_LIMIT - amount) return false;
        newCash += amount;
        newBalance -= amount;
    }
    if (!BankExec("BEGIN IMMEDIATE TRANSACTION")) return false;
    new bool:ok = true, query[300];
    format(query, sizeof query,
        "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
        newCash, gPlayerName[playerid], gMoney[playerid]);
    if (!BankExec(query) || !BankUpdatedOneRow()) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE bank_accounts SET balance=%d WHERE name='%s' AND balance=%d",
            newBalance, gPlayerName[playerid], gBankBalance[playerid]);
        if (!BankExec(query) || !BankUpdatedOneRow()) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO bank_history(name,action,amount,created_at) VALUES('%s','%s',%d,%d)",
            gPlayerName[playerid], deposit ? ("deposit") : ("withdraw"), amount, gettime());
        if (!BankExec(query)) ok = false;
    }
    if (ok && BankExec("COMMIT"))
    {
        gBankBalance[playerid] = newBalance;
        SetCash(playerid, newCash);
        return true;
    }
    BankExec("ROLLBACK");
    return false;
}

stock bool:CommitCashTransfer(sender, recipient, amount)
{
    if (sender == recipient || !gAuthed[sender] || !gAuthed[recipient] ||
        amount < 1 || amount > BANK_ACTION_LIMIT ||
        amount > gMoney[sender] || gMoney[recipient] > BANK_LIMIT - amount)
        return false;

    new senderBalance = gMoney[sender] - amount;
    new receiverBalance = gMoney[recipient] + amount;
    if (!BankExec("BEGIN IMMEDIATE TRANSACTION")) return false;

    new bool:ok = true, query[340];
    format(query, sizeof query,
        "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
        senderBalance, gPlayerName[sender], gMoney[sender]);
    if (!BankExec(query) || !BankUpdatedOneRow()) ok = false;

    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d WHERE name='%s' AND money=%d",
            receiverBalance, gPlayerName[recipient], gMoney[recipient]);
        if (!BankExec(query) || !BankUpdatedOneRow()) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "INSERT INTO cash_transfers(sender,receiver,amount,created_at) VALUES('%s','%s',%d,%d)",
            gPlayerName[sender],gPlayerName[recipient],amount,gettime());
        if (!BankExec(query)) ok = false;
    }
    if (ok && BankExec("COMMIT"))
    {
        SetCash(sender, senderBalance);
        SetCash(recipient, receiverBalance);
        return true;
    }
    BankExec("ROLLBACK");
    return false;
}

stock HandleBankCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/bank", true))
    {
        new line[144];
        format(line, sizeof line, "VELTOR Bank | Cash: $%d | Account: $%d",
            gMoney[playerid], gBankBalance[playerid]);
        SendClientMessage(playerid, C_GREEN, line);
        SendClientMessage(playerid, C_WHITE, "Commands at City Hall ATM: /deposit amount, /withdraw amount, /atm");
        return 1;
    }
    if (!strcmp(cmdtext, "/atm", true))
    {
        SetPlayerMapIcon(playerid, BANK_MAP_ICON,
            BANK_ATM_X, BANK_ATM_Y, BANK_ATM_Z, 52, C_ORANGE, MAPICON_LOCAL);
        SendClientMessage(playerid, C_GREEN, "City Hall ATM marked on your map.");
        return 1;
    }
    new bool:deposit;
    new offset;
    if (!strcmp(cmdtext, "/deposit ", true, 9))
    {
        deposit = true;
        offset = 9;
    }
    else if (!strcmp(cmdtext, "/withdraw ", true, 10))
    {
        deposit = false;
        offset = 10;
    }
    else if (!strcmp(cmdtext, "/deposit", true) ||
        !strcmp(cmdtext, "/withdraw", true))
    {
        SendClientMessage(playerid, C_WHITE, "Usage: /deposit amount or /withdraw amount (1-50000)");
        return 1;
    }
    else return 0;

    if (!BankAtATM(playerid))
    {
        SendClientMessage(playerid, C_RED, "Go to the ATM near City Hall on foot. Use /atm.");
        return 1;
    }
    new amount;
    if (!ParseBankAmount(cmdtext, offset, amount))
    {
        SendClientMessage(playerid, C_RED, "Invalid amount. Enter a whole number between 1 and 50000.");
        return 1;
    }
    if (!CommitBankOperation(playerid, deposit, amount))
    {
        SendClientMessage(playerid, C_RED, "Bank operation rejected or could not be saved.");
        return 1;
    }
    SendClientMessage(playerid, C_GREEN,
        deposit ? "Deposit completed and saved." : "Withdrawal completed and saved.");
    return 1;
}
