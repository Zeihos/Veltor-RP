#if defined _veltor_jobs_included
    #endinput
#endif
#define _veltor_jobs_included

// Three training jobs. Coordinates are development placeholders, not production map markers.
// Rewards are credited only when a player completes a server-validated route on foot.
#define JOB_MIN_TRAVEL 20
#define JOB_REACH_DISTANCE 8.0
#define JOB_MAX_CASH 100000000
#define JOB_XP_PER_SHIFT 20

new Float:gJobHubX[4] = {0.0, 1483.0, 1806.8, 594.0};
new Float:gJobHubY[4] = {0.0, -1718.0, -1919.4, 866.0};
new Float:gJobHubZ[4] = {0.0, 13.5, 13.4, -43.1};
new Float:gJobFinishX[4] = {0.0, 1376.0, 1924.2, 686.0};
new Float:gJobFinishY[4] = {0.0, -1762.0, -1767.8, 885.0};
new Float:gJobFinishZ[4] = {0.0, 13.5, 13.4, -40.0};

stock bool:JobExec(const query[])
{
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    db_free_result(r);
    return true;
}

stock bool:JobAffectedOne()
{
    new DBResult:r = db_query(gDb, "SELECT changes() AS affected");
    if (!r) return false;
    new count = db_num_rows(r) > 0 ? db_get_field_assoc_int(r, "affected") : 0;
    db_free_result(r);
    return count == 1;
}

stock bool:InitJobDB()
{
    return JobExec("CREATE TABLE IF NOT EXISTS job_progress (name TEXT PRIMARY KEY COLLATE NOCASE, shifts INTEGER NOT NULL DEFAULT 0, xp INTEGER NOT NULL DEFAULT 0)");
}

stock LoadJobProgress(playerid)
{
    gJobXP[playerid] = 0;
    gJobShifts[playerid] = 0;
    new query[160];
    format(query, sizeof query,
        "SELECT shifts,xp FROM job_progress WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (r)
    {
        if (db_num_rows(r) > 0)
        {
            gJobShifts[playerid] = db_get_field_assoc_int(r, "shifts");
            gJobXP[playerid] = db_get_field_assoc_int(r, "xp");
        }
        db_free_result(r);
    }
    if (gJobShifts[playerid] < 0 || gJobShifts[playerid] > 1000000) gJobShifts[playerid] = 0;
    if (gJobXP[playerid] < 0 || gJobXP[playerid] > 20000000) gJobXP[playerid] = 0;
    return 1;
}

stock JobLevel(playerid)
{
    new level = 1 + gJobXP[playerid] / 100;
    if (level > 20) level = 20;
    return level;
}

stock CancelJobShift(playerid)
{
    gShiftActive[playerid] = false;
    gShiftStarted[playerid] = 0;
    if (IsPlayerConnected(playerid)) DisablePlayerCheckpoint(playerid);
    return 1;
}

stock bool:AtJobLocation(playerid, bool:destination)
{
    new job = gJob[playerid];
    if (job < 1 || job > 3) return false;
    if (GetPlayerInterior(playerid) != 0 || GetPlayerVirtualWorld(playerid) != 0) return false;
    if (IsPlayerInAnyVehicle(playerid)) return false;
    if (destination)
        return IsPlayerInRangeOfPoint(playerid, JOB_REACH_DISTANCE,
            gJobFinishX[job], gJobFinishY[job], gJobFinishZ[job]);
    return IsPlayerInRangeOfPoint(playerid, JOB_REACH_DISTANCE,
        gJobHubX[job], gJobHubY[job], gJobHubZ[job]);
}

stock bool:CommitJobReward(playerid, reward, now)
{
    // All three writes must succeed together or the player's balance is unchanged.
    // SQLite transactions prevent a partial payout on server/database errors.
    new money = gMoney[playerid] + reward;
    new xp = gJobXP[playerid] + JOB_XP_PER_SHIFT;
    new shifts = gJobShifts[playerid] + 1;
    if (money > JOB_MAX_CASH || money < 0 || shifts > 1000000 || xp > 20000000) return false;
    if (!JobExec("BEGIN IMMEDIATE TRANSACTION")) return false;

    new bool:ok = true, query[320];
    format(query, sizeof query,
        "UPDATE accounts SET money=%d,job=%d WHERE name='%s' AND money=%d",
        money, gJob[playerid], gPlayerName[playerid], gMoney[playerid]);
    if (!JobExec(query) || !JobAffectedOne()) ok = false;

    if (ok)
    {
        format(query, sizeof query,
            "INSERT OR REPLACE INTO work_cooldown(name,last_work) VALUES('%s',%d)",
            gPlayerName[playerid], now);
        if (!JobExec(query)) ok = false;
    }
    if (ok)
    {
        format(query, sizeof query,
            "INSERT OR REPLACE INTO job_progress(name,shifts,xp) VALUES('%s',%d,%d)",
            gPlayerName[playerid], shifts, xp);
        if (!JobExec(query)) ok = false;
    }
    if (ok && JobExec("COMMIT"))
    {
        gLastWork[playerid] = now;
        gJobXP[playerid] = xp;
        gJobShifts[playerid] = shifts;
        SetCash(playerid, money);
        return true;
    }
    JobExec("ROLLBACK");
    return false;
}

stock HandleJobCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/jobs", true))
    {
        SendClientMessage(playerid, C_ORANGE, "Training jobs: /job 1 courier | /job 2 mechanic | /job 3 miner");
        SendClientMessage(playerid, C_WHITE, "Select a job, then /jobgps to mark its start point.");
        SendClientMessage(playerid, C_WHITE, "At the start point use /work, travel to checkpoint, then /work again.");
        return 1;
    }

    if (!strcmp(cmdtext, "/jobgps", true))
    {
        if (gShiftActive[playerid])
        {
            SendClientMessage(playerid, C_RED, "You are already on a shift. Follow your active checkpoint.");
            return 1;
        }
        new job = gJob[playerid];
        if (job < 1 || job > 3)
        {
            SendClientMessage(playerid, C_RED, "Choose a job first: /job 1, /job 2 or /job 3.");
            return 1;
        }
        SetPlayerCheckpoint(playerid, gJobHubX[job], gJobHubY[job], gJobHubZ[job], 5.0);
        SendClientMessage(playerid, C_GREEN, "Your workplace is marked on the map.");
        return 1;
    }

    if (!strcmp(cmdtext, "/workstatus", true))
    {
        if (!gShiftActive[playerid])
        {
            SendClientMessage(playerid, C_WHITE, "You have no active shift. Use /jobgps and /work.");
            return 1;
        }
        new remaining = JOB_MIN_TRAVEL - (gettime() - gShiftStarted[playerid]);
        if (remaining < 0) remaining = 0;
        new message[128];
        format(message, sizeof message,
            "Shift active | Job %d | Minimum time remaining: %d seconds",
            gJob[playerid], remaining);
        SendClientMessage(playerid, C_ORANGE, message);
        SendClientMessage(playerid, C_WHITE, "Follow the destination checkpoint; type /work on foot to finish.");
        return 1;
    }

    if (!strcmp(cmdtext, "/cancelwork", true))
    {
        if (!gShiftActive[playerid])
        {
            SendClientMessage(playerid, C_WHITE, "You have no active shift.");
            return 1;
        }
        CancelJobShift(playerid);
        SendClientMessage(playerid, C_WHITE, "Your shift was cancelled without payment.");
        return 1;
    }

    if (!strcmp(cmdtext, "/work", true))
    {
        new job = gJob[playerid];
        if (job < 1 || job > 3)
        {
            SendClientMessage(playerid, C_RED, "Choose a job with /job 1, /job 2 or /job 3.");
            return 1;
        }

        new now = gettime();
        if (!gShiftActive[playerid])
        {
            if (gLastWork[playerid] != 0 &&
                now - gLastWork[playerid] < WORK_DELAY)
            {
                SendClientMessage(playerid, C_RED, "Your next shift is not available yet.");
                return 1;
            }
            if (!AtJobLocation(playerid, false))
            {
                SendClientMessage(playerid, C_RED, "Go to your workplace on foot. Use /jobgps.");
                return 1;
            }
            gShiftActive[playerid] = true;
            gShiftStarted[playerid] = now;
            SetPlayerCheckpoint(playerid, gJobFinishX[job], gJobFinishY[job], gJobFinishZ[job], 5.0);
            SendClientMessage(playerid, C_ORANGE, "Shift started. Reach the destination and type /work.");
            return 1;
        }

        if (now - gShiftStarted[playerid] < JOB_MIN_TRAVEL)
        {
            SendClientMessage(playerid, C_RED, "This shift cannot be completed so quickly.");
            return 1;
        }
        if (!AtJobLocation(playerid, true))
        {
            SendClientMessage(playerid, C_RED, "Go to the destination checkpoint on foot before finishing.");
            return 1;
        }

        new reward = 350 + job * 50 + JobLevel(playerid) * 10;
        if (gMoney[playerid] > JOB_MAX_CASH - reward)
        {
            SendClientMessage(playerid, C_RED, "Cash limit reached.");
            return 1;
        }
        if (!CommitJobReward(playerid, reward, now))
        {
            SendClientMessage(playerid, C_RED, "Could not save shift reward. Please retry.");
            return 1;
        }
        CancelJobShift(playerid);
        new message[144];
        format(message, sizeof message,
            "Shift complete: +$%d, +%d XP | Level %d | Shifts %d",
            reward, JOB_XP_PER_SHIFT, JobLevel(playerid), gJobShifts[playerid]);
        SendClientMessage(playerid, C_GREEN, message);
        return 1;
    }
    return 0;
}
