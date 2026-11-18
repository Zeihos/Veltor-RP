#include <open.mp>
#include <samp_bcrypt>

// VELTOR ROLEPLAY / independent test gamemode (development build).
// Uses open.mp built-ins; no RADMIR SQL dump, plugins or external credentials.
#define C_ORANGE 0xFF9633FF
#define C_WHITE 0xFFFFFFFF
#define C_RED 0xFF5555FF
#define C_GREEN 0x70E2A1FF
#define D_REGISTER 1001
#define D_LOGIN 1002
#define START_X 1482.40
#define START_Y -1745.80
#define START_Z 13.55
#define CAR_PRICE 15000
#define WORK_DELAY 60
#define AUTH_NONE 0
#define AUTH_REGISTER 1
#define AUTH_LOGIN 2
// Temporary staff controls are loaded from SQLite only after bcrypt authentication.

new DB:gDb;
new bool:gAuthed[MAX_PLAYERS];
new gMoney[MAX_PLAYERS];
new gJob[MAX_PLAYERS];
new gOwnedCar[MAX_PLAYERS];
new gCarId[MAX_PLAYERS];
new gLastWork[MAX_PLAYERS];
new gJobXP[MAX_PLAYERS];
new gJobShifts[MAX_PLAYERS];
new bool:gShiftActive[MAX_PLAYERS];
new gShiftStarted[MAX_PLAYERS];
new gAttempts[MAX_PLAYERS];
new gPasswordHash[MAX_PLAYERS][BCRYPT_HASH_LENGTH];
new gAuthPending[MAX_PLAYERS];
new gAuthToken[MAX_PLAYERS];
new gNextAuthToken;
new bool:gStaff[MAX_PLAYERS];
new gMutedUntil[MAX_PLAYERS];
new gLastReport[MAX_PLAYERS];
new gLastChat[MAX_PLAYERS];
new gPlayerName[MAX_PLAYERS][MAX_PLAYER_NAME + 1];

stock bool:DigitsOnly(const value[])
{
    new len=strlen(value);
    if(len<1 || len>10)return false;
    for(new i=0; i<len; i++)if(value[i]<'0' || value[i]>'9')return false;
    return true;
}
#include "veltor_moderation.inc"
#include "veltor_jobs.inc"

stock LoadWorkCooldown(playerid)
{
    new sql[180];
    format(sql,sizeof sql,"SELECT last_work FROM work_cooldown WHERE name='%s' LIMIT 1",gPlayerName[playerid]);
    new DBResult:r=db_query(gDb,sql);
    if(r)
    {
        if(db_num_rows(r)>0)gLastWork[playerid]=db_get_field_assoc_int(r,"last_work");
        db_free_result(r);
    }
    return 1;
}
stock SaveWorkCooldown(playerid)
{
    new sql[180];
    format(sql,sizeof sql,"INSERT OR REPLACE INTO work_cooldown(name,last_work) VALUES('%s',%d)",gPlayerName[playerid],gLastWork[playerid]);
    new DBResult:r=db_query(gDb,sql);
    if(r)db_free_result(r);
    return 1;
}

main() { print("VELTOR ROLEPLAY test gamemode loaded"); }

stock SaveUser(playerid)
{
    if (!gAuthed[playerid] || !gDb) return 0;
    new sql[256];
    format(sql, sizeof sql,
        "UPDATE accounts SET money=%d,job=%d,car=%d WHERE name='%s'",
        gMoney[playerid], gJob[playerid], gOwnedCar[playerid], gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, sql);
    if (!r)
    {
        printf("[VELTOR] Failed to save account: %s", gPlayerName[playerid]);
        return 0;
    }
    db_free_result(r);
    return 1;
}

stock SetCash(playerid, amount)
{
    gMoney[playerid] = amount;
    ResetPlayerMoney(playerid);
    GivePlayerMoney(playerid, amount);
    return 1;
}

stock bool:ValidRPName(const name[])
{
    new len = strlen(name);
    if (len < 3 || len > 24) return false;
    for (new i = 0; i < len; i++)
    {
        if ((name[i] >= 'a' && name[i] <= 'z') ||
            (name[i] >= 'A' && name[i] <= 'Z') ||
            (name[i] >= '0' && name[i] <= '9') || name[i] == '_') continue;
        return false;
    }
    return true;
}

stock InitPlayer(playerid)
{
    gAuthed[playerid] = false;
    gMoney[playerid] = 0;
    gJob[playerid] = 0;
    gOwnedCar[playerid] = 0;
    gCarId[playerid] = 0;
    gLastWork[playerid] = 0;
    gJobXP[playerid] = 0;
    gJobShifts[playerid] = 0;
    gShiftActive[playerid] = false;
    gShiftStarted[playerid] = 0;
    gAttempts[playerid] = 0;
    gAuthPending[playerid] = AUTH_NONE;
    gAuthToken[playerid] = ++gNextAuthToken;
    gPasswordHash[playerid][0] = EOS;
    gStaff[playerid] = false;
    gMutedUntil[playerid] = 0;
    gLastReport[playerid] = 0;
    gLastChat[playerid] = 0;
    GetPlayerName(playerid, gPlayerName[playerid], MAX_PLAYER_NAME + 1);
    return 1;
}

stock ShowAuth(playerid)
{
    new sql[144];
    format(sql, sizeof sql, "SELECT salt,password FROM accounts WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, sql);
    if (!r)
    {
        SendClientMessage(playerid, C_RED, "Account database query failed.");
        Kick(playerid);
        return 0;
    }
    if (db_num_rows(r) > 0)
    {
        db_get_field_assoc(r, "password", gPasswordHash[playerid], BCRYPT_HASH_LENGTH);
        db_free_result(r);
        if (strlen(gPasswordHash[playerid]) != 60 || gPasswordHash[playerid][0] != '$' ||
            gPasswordHash[playerid][1] != '2')
        {
            SendClientMessage(playerid, C_RED, "Legacy accounts must reset their password. Contact an administrator.");
            Kick(playerid);
            return 0;
        }
        ShowPlayerDialog(playerid, D_LOGIN, DIALOG_STYLE_PASSWORD,
            "VELTOR ROLEPLAY | Login", "Enter your account password:", "Login", "Exit");
    }
    else
    {
        db_free_result(r);
        ShowPlayerDialog(playerid, D_REGISTER, DIALOG_STYLE_PASSWORD,
            "VELTOR ROLEPLAY | Registration", "Create a password (at least 8 characters):", "Create", "Exit");
    }
    return 1;
}

stock FinishAuth(playerid)
{
    for(new i=0;i<MAX_PLAYERS;i++)if(i!=playerid && gAuthed[i] && !strcmp(gPlayerName[i],gPlayerName[playerid],true))
    {
        SendClientMessage(playerid,C_RED,"Account is already online.");
        Kick(playerid);
        return 0;
    }
    LoadWorkCooldown(playerid);
    LoadJobProgress(playerid);
    LoadModeration(playerid);
    gAuthed[playerid] = true;
    SetSpawnInfo(playerid, NO_TEAM, 60, START_X, START_Y, START_Z, 90.0, 0, 0, 0, 0, 0, 0);
    SpawnPlayer(playerid);
    SetCash(playerid, gMoney[playerid]);
    SendClientMessage(playerid, C_GREEN, "Welcome to VELTOR ROLEPLAY!");
    SendClientMessage(playerid, C_WHITE, "Commands: /help, /stats, /job, /work, /car, /buycar, /pay, /spawn, /report");
    return 1;
}

public OnGameModeInit()
{
    SetGameModeText("VELTOR ROLEPLAY | TEST");
    bcrypt_set_thread_limit(2);
    ShowPlayerMarkers(PLAYER_MARKERS_MODE_GLOBAL);
    DisableInteriorEnterExits();
    SetWorldTime(12);
    SetWeather(2);
    AddPlayerClass(60, START_X, START_Y, START_Z, 90.0, 0, 0, 0, 0, 0, 0);
    AddStaticVehicleEx(411, 1493.1, -1737.9, 13.2, 90.0, 3, 3, 120);
    AddStaticVehicleEx(400, 1493.1, -1732.0, 13.2, 90.0, 1, 1, 120);
    gDb = db_open("veltor.db");
    if (!gDb)
    {
        print("[VELTOR] Cannot open SQLite database scriptfiles/veltor.db");
        return 0;
    }
    new DBResult:r = db_query(gDb,
        "CREATE TABLE IF NOT EXISTS accounts (name TEXT PRIMARY KEY COLLATE NOCASE, salt TEXT NOT NULL, password TEXT NOT NULL, money INTEGER NOT NULL DEFAULT 2500, job INTEGER NOT NULL DEFAULT 0, car INTEGER NOT NULL DEFAULT 0)");
    if (!r)
    {
        print("[VELTOR] Cannot initialize accounts table");
        db_close(gDb);
        return 0;
    }
    db_free_result(r);
    r=db_query(gDb,"CREATE TABLE IF NOT EXISTS work_cooldown (name TEXT PRIMARY KEY COLLATE NOCASE, last_work INTEGER NOT NULL DEFAULT 0)");
    if(!r)
    {
        print("[VELTOR] Cannot initialize work cooldown table");
        db_close(gDb);
        return 0;
    }
    db_free_result(r);
    if (!InitJobDB())
    {
        print("[VELTOR] Cannot initialize job_progress table");
        db_close(gDb);
        return 0;
    }
    if (!InitModerationDB())
    {
        print("[VELTOR] Could not initialize staff, reports or moderation tables");
        db_close(gDb);
        return 0;
    }
    print("[VELTOR] Clean SQLite account system ready");
    return 1;
}

public OnGameModeExit()
{
    for (new i = 0; i < MAX_PLAYERS; i++)
        if (IsPlayerConnected(i)) SaveUser(i);
    if (gDb) db_close(gDb);
    return 1;
}

public OnPlayerConnect(playerid)
{
    InitPlayer(playerid);
    if (!ValidRPName(gPlayerName[playerid]))
    {
        SendClientMessage(playerid, C_RED, "Use a name containing A-Z, 0-9 and underscore (3-24 characters).");
        Kick(playerid);
        return 1;
    }
    if (!gDb)
    {
        SendClientMessage(playerid, C_RED, "Account database is unavailable.");
        Kick(playerid);
        return 1;
    }
    ShowAuth(playerid);
    return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
    #pragma unused reason
    SaveUser(playerid);
    CancelJobShift(playerid);
    if (gCarId[playerid] != 0) DestroyVehicle(gCarId[playerid]);
    gCarId[playerid] = 0;
    gAuthed[playerid] = false;
    gAuthPending[playerid] = AUTH_NONE;
    gAuthToken[playerid] = 0;
    gStaff[playerid] = false;
    gMutedUntil[playerid] = 0;
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    #pragma unused classid
    SetPlayerPos(playerid, START_X, START_Y, START_Z);
    SetPlayerCameraPos(playerid, 1487.0, -1745.0, 18.0);
    SetPlayerCameraLookAt(playerid, START_X, START_Y, START_Z);
    return 1;
}

public OnPlayerRequestSpawn(playerid)
{
    if (!gAuthed[playerid]) return 0;
    return 1;
}

public OnPlayerDeath(playerid, killerid, WEAPON:reason)
{
    #pragma unused killerid, reason
    if (gAuthed[playerid]) CancelJobShift(playerid);
    return 1;
}

public OnPlayerEnterCheckpoint(playerid)
{
    if (gAuthed[playerid] && gShiftActive[playerid] && AtJobLocation(playerid, true))
        SendClientMessage(playerid, C_GREEN, "Destination reached. Use /work on foot to finish.");
    return 1;
}

public OnPlayerSpawn(playerid)
{
    if (!gAuthed[playerid]) return 1;
    SetPlayerColor(playerid, C_ORANGE);
    return 1;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    #pragma unused listitem
    if (dialogid != D_LOGIN && dialogid != D_REGISTER) return 0;
    if (gAuthed[playerid]) return 1;
    if (!response) { Kick(playerid); return 1; }
    new len = strlen(inputtext);
    if (len < 8 || len > 72)
    {
        SendClientMessage(playerid, C_RED, "Password must be 8-72 characters.");
        ShowAuth(playerid);
        return 1;
    }
    if (gAuthPending[playerid] != AUTH_NONE) return 1;
    if (dialogid == D_REGISTER)
    {
        gAuthPending[playerid] = AUTH_REGISTER;
        bcrypt_hash(playerid, "OnRegistrationHashed", inputtext, BCRYPT_COST, "i", gAuthToken[playerid]);
        return 1;
    }
    gAuthPending[playerid] = AUTH_LOGIN;
    bcrypt_verify(playerid, "OnLoginVerified", inputtext, gPasswordHash[playerid],
        "i", gAuthToken[playerid]);
    return 1;
}

forward OnRegistrationHashed(playerid, token);
public OnRegistrationHashed(playerid, token)
{
    if (!IsPlayerConnected(playerid) || gAuthToken[playerid] != token ||
        gAuthPending[playerid] != AUTH_REGISTER) return 1;
    gAuthPending[playerid] = AUTH_NONE;
    new hash[BCRYPT_HASH_LENGTH];
    bcrypt_get_hash(hash, sizeof hash);
    if (strlen(hash) != 60)
    {
        SendClientMessage(playerid, C_RED, "Password hashing failed.");
        Kick(playerid);
        return 1;
    }
    // Recheck uniqueness after the asynchronous operation.
    new sql[320];
    format(sql, sizeof sql, "SELECT name FROM accounts WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:check = db_query(gDb, sql);
    if (!check) { Kick(playerid); return 1; }
    new exists = db_num_rows(check);
    db_free_result(check);
    if (exists > 0) { SendClientMessage(playerid, C_RED, "Account already exists."); Kick(playerid); return 1; }
    format(sql, sizeof sql,
        "INSERT INTO accounts(name,salt,password,money,job,car) VALUES('%s','','%s',2500,0,0)",
        gPlayerName[playerid], hash);
    new DBResult:r = db_query(gDb, sql);
    if (!r)
    {
        SendClientMessage(playerid, C_RED, "Registration failed.");
        Kick(playerid);
        return 1;
    }
    db_free_result(r);
    gMoney[playerid] = 2500;
    gJob[playerid] = 0;
    gOwnedCar[playerid] = 0;
    FinishAuth(playerid);
    return 1;
}

forward OnLoginVerified(playerid, bool:success, token);
public OnLoginVerified(playerid, bool:success, token)
{
    if (!IsPlayerConnected(playerid) || gAuthToken[playerid] != token ||
        gAuthPending[playerid] != AUTH_LOGIN) return 1;
    gAuthPending[playerid] = AUTH_NONE;
    if (!success)
    {
        gAttempts[playerid]++;
        if (gAttempts[playerid] >= 3) { Kick(playerid); return 1; }
        SendClientMessage(playerid, C_RED, "Incorrect password.");
        ShowAuth(playerid);
        return 1;
    }
    new sql[160];
    format(sql, sizeof sql, "SELECT money,job,car FROM accounts WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, sql);
    if (!r || db_num_rows(r) == 0)
    {
        if (r) db_free_result(r);
        Kick(playerid);
        return 1;
    }
    gMoney[playerid] = db_get_field_assoc_int(r, "money");
    gJob[playerid] = db_get_field_assoc_int(r, "job");
    gOwnedCar[playerid] = db_get_field_assoc_int(r, "car");
    db_free_result(r);
    if (gMoney[playerid] < 0 || gMoney[playerid] > 100000000) gMoney[playerid] = 2500;
    if (gJob[playerid] < 0 || gJob[playerid] > 3) gJob[playerid] = 0;
    if (gOwnedCar[playerid] != 0 && gOwnedCar[playerid] != 560) gOwnedCar[playerid] = 0;
    FinishAuth(playerid);
    return 1;
}

public OnPlayerText(playerid, text[])
{
    #pragma unused text
    if (!gAuthed[playerid]) return 0;
    if (IsMuted(playerid))
    {
        SendClientMessage(playerid, C_RED, "Your chat is temporarily restricted.");
        return 0;
    }
    new now = gettime();
    if (gLastChat[playerid] && now - gLastChat[playerid] < CHAT_COOLDOWN)
    {
        SendClientMessage(playerid, C_RED, "Please do not flood the chat.");
        return 0;
    }
    gLastChat[playerid] = now;
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!gAuthed[playerid]) return 1;
    if (HandleModerationCommand(playerid, cmdtext)) return 1;
    if (HandleJobCommand(playerid, cmdtext)) return 1;
    if (!strcmp(cmdtext, "/help", true))
    {
        SendClientMessage(playerid, C_ORANGE, "VELTOR RP | Commands");
        SendClientMessage(playerid, C_WHITE, "/stats /jobs /job 1-3 /jobgps /work /workstatus /cancelwork");
        SendClientMessage(playerid, C_WHITE, "/me action /do description /b text /report message /ahelp (staff)");
        return 1;
    }
    if (!strcmp(cmdtext, "/stats", true))
    {
        new msg[144];
        format(msg, sizeof msg, "Name: %s | $%d | Job: %d | Lv%d | XP: %d | Shifts: %d",
            gPlayerName[playerid], gMoney[playerid], gJob[playerid], JobLevel(playerid), gJobXP[playerid], gJobShifts[playerid]);
        SendClientMessage(playerid, C_GREEN, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/spawn", true))
    {
        CancelJobShift(playerid);
        if (IsPlayerInAnyVehicle(playerid)) RemovePlayerFromVehicle(playerid);
        SetPlayerInterior(playerid, 0);
        SetPlayerVirtualWorld(playerid, 0);
        SetPlayerPos(playerid, START_X, START_Y, START_Z);
        SendClientMessage(playerid, C_WHITE, "You returned to City Hall.");
        return 1;
    }
    if (!strcmp(cmdtext, "/job", true))
    {
        SendClientMessage(playerid, C_WHITE, "Jobs: /job 1 courier | /job 2 mechanic | /job 3 miner");
        return 1;
    }
    if (!strcmp(cmdtext, "/job 1", true) || !strcmp(cmdtext, "/job 2", true) || !strcmp(cmdtext, "/job 3", true))
    {
        CancelJobShift(playerid);
        gJob[playerid] = cmdtext[5] - '0';
        SaveUser(playerid);
        SendClientMessage(playerid, C_GREEN, "Job selected. Use /jobgps to find the workplace.");
        return 1;
    }
    if (!strcmp(cmdtext, "/buycar", true))
    {
        if (gOwnedCar[playerid] != 0) { SendClientMessage(playerid, C_RED, "You already own a car."); return 1; }
        if (gMoney[playerid] < CAR_PRICE) { SendClientMessage(playerid, C_RED, "Sultan costs $15000."); return 1; }
        gOwnedCar[playerid] = 560;
        SetCash(playerid, gMoney[playerid] - CAR_PRICE);
        SaveUser(playerid);
        SendClientMessage(playerid, C_GREEN, "You bought a Sultan. Use /car to spawn it.");
        return 1;
    }
    if (!strcmp(cmdtext, "/car", true))
    {
        if (!gOwnedCar[playerid]) { SendClientMessage(playerid, C_RED, "Buy a car with /buycar first."); return 1; }
        if (gCarId[playerid] != 0) DestroyVehicle(gCarId[playerid]);
        new Float:x, Float:y, Float:z, Float:a;
        GetPlayerPos(playerid, x, y, z);
        GetPlayerFacingAngle(playerid, a);
        gCarId[playerid] = CreateVehicle(gOwnedCar[playerid], x + 4.0, y, z + 0.5, a, 3, 3, -1);
        SendClientMessage(playerid, C_GREEN, "Your personal vehicle is nearby.");
        return 1;
    }
    if (!strcmp(cmdtext, "/pay", true) || !strcmp(cmdtext, "/pay ", true))
    {
        SendClientMessage(playerid, C_WHITE, "Usage: /pay playerid amount");
        return 1;
    }
    if (!strcmp(cmdtext, "/pay ", true, 5))
    {
        new arg[64];
        strmid(arg, cmdtext, 5, strlen(cmdtext), sizeof arg);
        new s = strfind(arg, " ");
        if (s < 1) { SendClientMessage(playerid, C_WHITE, "Usage: /pay playerid amount"); return 1; }
        new idText[12], amountText[20];
        strmid(idText, arg, 0, s, sizeof idText);
        strmid(amountText, arg, s + 1, strlen(arg), sizeof amountText);
        if (!DigitsOnly(idText) || !DigitsOnly(amountText) || strlen(idText)>5 || strlen(amountText)>6)
        {
            SendClientMessage(playerid, C_RED, "Invalid player ID or amount.");
            return 1;
        }
        new target = strval(idText), amount = strval(amountText);
        if (target<0 || target>=MAX_PLAYERS || !IsPlayerConnected(target) || !gAuthed[target] || target == playerid ||
            amount < 1 || amount > 50000 || amount > gMoney[playerid] || gMoney[target] > 100000000 - amount)
        {
            SendClientMessage(playerid, C_RED, "Invalid recipient or amount (maximum $50000).");
            return 1;
        }
        SetCash(playerid, gMoney[playerid] - amount);
        SetCash(target, gMoney[target] + amount);
        SaveUser(playerid); SaveUser(target);
        SendClientMessage(playerid, C_GREEN, "Transfer completed.");
        SendClientMessage(target, C_GREEN, "You received a player-to-player transfer.");
        return 1;
    }
    if (!strcmp(cmdtext, "/me ", true, 4))
    {
        if (strlen(cmdtext) <= 4) return 1;
        new msg[144];
        new action[115];
        strmid(action, cmdtext, 4, strlen(cmdtext), sizeof action);
        format(msg, sizeof msg, "* %s %s", gPlayerName[playerid], action);
        SendClientMessageToAll(C_ORANGE, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/do ", true, 4))
    {
        if (strlen(cmdtext) <= 4) return 1;
        new msg[144];
        new action[115];
        strmid(action, cmdtext, 4, strlen(cmdtext), sizeof action);
        format(msg, sizeof msg, "* %s (%s)", action, gPlayerName[playerid]);
        SendClientMessageToAll(C_ORANGE, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/b ", true, 3))
    {
        if (strlen(cmdtext) <= 3) return 1;
        new msg[144];
        new action[115];
        strmid(action, cmdtext, 3, strlen(cmdtext), sizeof action);
        format(msg, sizeof msg, "(( %s: %s ))", gPlayerName[playerid], action);
        SendClientMessageToAll(C_WHITE, msg);
        return 1;
    }
    SendClientMessage(playerid, C_RED, "Unknown command. Type /help.");
    return 1;
}
