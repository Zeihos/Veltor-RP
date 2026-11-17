#if defined _veltor_moderation_included
    #endinput
#endif
#define _veltor_moderation_included

// The username is validated by ValidRPName before any SQL interpolation.
// Staff accounts are provisioned offline with tools/staff_admin.py; never via a player command.
#define REPORT_COOLDOWN 60
#define REPORT_MIN_LENGTH 10
#define REPORT_MAX_LENGTH 96
#define CHAT_COOLDOWN 2

stock bool:IsVeltorStaff(playerid)
{
    return gAuthed[playerid] && gStaff[playerid];
}

stock SqlEscapeText(const source[], dest[], maxlen)
{
    new j = 0;
    for (new i = 0, len = strlen(source); i < len && j < maxlen - 1; i++)
    {
        if (source[i] == 39)
        {
            if (j + 2 >= maxlen) break;
            dest[j++] = 39;
            dest[j++] = 39;
        }
        else if (source[i] < 32)
        {
            dest[j++] = ' ';
        }
        else dest[j++] = source[i];
    }
    dest[j] = EOS;
    return j;
}

stock InitModerationDB()
{
    new DBResult:r = db_query(gDb,
        "CREATE TABLE IF NOT EXISTS staff (name TEXT PRIMARY KEY COLLATE NOCASE)");
    if (!r) return 0;
    db_free_result(r);
    r = db_query(gDb,
        "CREATE TABLE IF NOT EXISTS reports (id INTEGER PRIMARY KEY AUTOINCREMENT, reporter TEXT NOT NULL, message TEXT NOT NULL, created_at INTEGER NOT NULL, status INTEGER NOT NULL DEFAULT 0, closed_by TEXT NOT NULL DEFAULT '')");
    if (!r) return 0;
    db_free_result(r);
    r = db_query(gDb,
        "CREATE INDEX IF NOT EXISTS idx_reports_open ON reports (status, id)");
    if (!r) return 0;
    db_free_result(r);
    r = db_query(gDb,
        "CREATE TABLE IF NOT EXISTS mutes (name TEXT PRIMARY KEY COLLATE NOCASE, muted_until INTEGER NOT NULL DEFAULT 0)");
    if (!r) return 0;
    db_free_result(r);
    r = db_query(gDb,
        "CREATE TABLE IF NOT EXISTS moderation_audit (id INTEGER PRIMARY KEY AUTOINCREMENT, actor TEXT NOT NULL, action TEXT NOT NULL, subject TEXT NOT NULL, created_at INTEGER NOT NULL)");
    if (!r) return 0;
    db_free_result(r);
    return 1;
}

stock LoadModeration(playerid)
{
    new query[190];
    gStaff[playerid] = false;
    gMutedUntil[playerid] = 0;
    format(query, sizeof query,
        "SELECT name FROM staff WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (r)
    {
        gStaff[playerid] = (db_num_rows(r) > 0);
        db_free_result(r);
    }
    format(query, sizeof query,
        "SELECT muted_until FROM mutes WHERE name='%s' LIMIT 1", gPlayerName[playerid]);
    r = db_query(gDb, query);
    if (r)
    {
        if (db_num_rows(r) > 0)
            gMutedUntil[playerid] = db_get_field_assoc_int(r, "muted_until");
        db_free_result(r);
    }
    return 1;
}

stock AuditModeration(playerid, const action[], const target[])
{
    new query[340];
    format(query, sizeof query,
        "INSERT INTO moderation_audit(actor,action,subject,created_at) VALUES('%s','%s','%s',%d)",
        gPlayerName[playerid], action, target, gettime());
    new DBResult:r = db_query(gDb, query);
    if (r) db_free_result(r);
    return 1;
}

stock bool:IsMuted(playerid)
{
    return gMutedUntil[playerid] > gettime();
}

stock ShowOpenReports(playerid)
{
    new DBResult:r = db_query(gDb,
        "SELECT id,reporter,message FROM reports WHERE status=0 ORDER BY id DESC LIMIT 5");
    if (!r)
    {
        SendClientMessage(playerid, C_RED, "Unable to load reports.");
        return 1;
    }
    new count = db_num_rows(r);
    if (count == 0)
    {
        SendClientMessage(playerid, C_GREEN, "There are no open reports.");
        db_free_result(r);
        return 1;
    }
    SendClientMessage(playerid, C_ORANGE, "Last five open reports:");
    for (new i = 0; i < count; i++)
    {
        new reporter[25], message[REPORT_MAX_LENGTH + 1], line[150];
        db_get_field_assoc(r, "reporter", reporter, sizeof reporter);
        db_get_field_assoc(r, "message", message, sizeof message);
        format(line, sizeof line, "#%d | %s: %s",
            db_get_field_assoc_int(r, "id"), reporter, message);
        SendClientMessage(playerid, C_WHITE, line);
        db_next_row(r);
    }
    db_free_result(r);
    SendClientMessage(playerid, C_WHITE, "Close with /rclose ID.");
    return 1;
}

stock bool:ReadTargetId(const value[], &target)
{
    if (!DigitsOnly(value) || strlen(value) > 5) return false;
    target = strval(value);
    return target >= 0 && target < MAX_PLAYERS &&
        IsPlayerConnected(target) && gAuthed[target];
}

stock HandleModerationCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/ahelp", true))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        SendClientMessage(playerid, C_ORANGE,
            "Staff: /reports /rclose ID /kick ID /mute ID minutes /unmute ID");
        return 1;
    }
    if (!strcmp(cmdtext, "/reports", true))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        return ShowOpenReports(playerid);
    }
    if (!strcmp(cmdtext, "/report", true))
    {
        SendClientMessage(playerid, C_WHITE, "Usage: /report describe the problem.");
        return 1;
    }
    if (!strcmp(cmdtext, "/report ", true, 8))
    {
        if (gLastReport[playerid] && gettime() - gLastReport[playerid] < REPORT_COOLDOWN)
        {
            SendClientMessage(playerid, C_RED, "Please wait before sending another report.");
            return 1;
        }
        new detail[REPORT_MAX_LENGTH + 1], safe[(REPORT_MAX_LENGTH * 2) + 1];
        strmid(detail, cmdtext, 8, strlen(cmdtext), sizeof detail);
        if (strlen(detail) < REPORT_MIN_LENGTH)
        {
            SendClientMessage(playerid, C_RED, "Report must contain at least 10 characters.");
            return 1;
        }
        SqlEscapeText(detail, safe, sizeof safe);
        new query[420];
        format(query, sizeof query,
            "INSERT INTO reports(reporter,message,created_at,status) VALUES('%s','%s',%d,0)",
            gPlayerName[playerid], safe, gettime());
        new DBResult:r = db_query(gDb, query);
        if (!r)
        {
            SendClientMessage(playerid, C_RED, "Could not save report. Please retry later.");
            return 1;
        }
        db_free_result(r);
        gLastReport[playerid] = gettime();
        SendClientMessage(playerid, C_GREEN, "Report saved. The staff can review it.");
        printf("[VELTOR REPORT] submitted by %s", gPlayerName[playerid]);
        return 1;
    }

    // Player chat and RP text must respect a mute even when sent via commands.
    if ((!strcmp(cmdtext, "/me ", true, 4) ||
        !strcmp(cmdtext, "/do ", true, 4) ||
        !strcmp(cmdtext, "/b ", true, 3)) && IsMuted(playerid))
    {
        SendClientMessage(playerid, C_RED, "Chat is temporarily disabled on your account.");
        return 1;
    }

    if (!strcmp(cmdtext, "/rclose ", true, 8))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        new idText[16], reportId, query[320];
        strmid(idText, cmdtext, 8, strlen(cmdtext), sizeof idText);
        if (!DigitsOnly(idText) || strlen(idText)>9 || (reportId = strval(idText)) < 1)
        {
            SendClientMessage(playerid, C_RED, "Usage: /rclose report_id");
            return 1;
        }
        format(query, sizeof query,
            "SELECT id FROM reports WHERE id=%d AND status=0 LIMIT 1", reportId);
        new DBResult:r = db_query(gDb, query);
        if (!r) return 1;
        new exists = db_num_rows(r);
        db_free_result(r);
        if (!exists)
        {
            SendClientMessage(playerid, C_RED, "Open report not found.");
            return 1;
        }
        format(query, sizeof query,
            "UPDATE reports SET status=1,closed_by='%s' WHERE id=%d AND status=0",
            gPlayerName[playerid], reportId);
        r = db_query(gDb, query);
        if (!r) { SendClientMessage(playerid, C_RED, "Unable to close report."); return 1; }
        db_free_result(r);
        new subject[18];
        format(subject, sizeof subject, "report #%d", reportId);
        AuditModeration(playerid, "close-report", subject);
        SendClientMessage(playerid, C_GREEN, "Report closed.");
        return 1;
    }

    if (!strcmp(cmdtext, "/kick ", true, 6))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        new idText[16], target;
        strmid(idText, cmdtext, 6, strlen(cmdtext), sizeof idText);
        if (!ReadTargetId(idText, target) || target == playerid || gStaff[target])
        {
            SendClientMessage(playerid, C_RED, "Invalid player or staff-protected target.");
            return 1;
        }
        AuditModeration(playerid, "kick", gPlayerName[target]);
        printf("[VELTOR STAFF] %s kicked %s", gPlayerName[playerid], gPlayerName[target]);
        SendClientMessage(target, C_RED, "You have been disconnected by staff.");
        Kick(target);
        SendClientMessage(playerid, C_GREEN, "Player kicked.");
        return 1;
    }

    if (!strcmp(cmdtext, "/mute ", true, 6))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        new arguments[40], idText[12], minuteText[12];
        strmid(arguments, cmdtext, 6, strlen(cmdtext), sizeof arguments);
        new divider = strfind(arguments, " ");
        if (divider < 1)
        {
            SendClientMessage(playerid, C_WHITE, "Usage: /mute ID minutes (1-1440)");
            return 1;
        }
        strmid(idText, arguments, 0, divider, sizeof idText);
        strmid(minuteText, arguments, divider + 1, strlen(arguments), sizeof minuteText);
        new target, minutes;
        if (!ReadTargetId(idText, target) || target == playerid || gStaff[target] ||
            !DigitsOnly(minuteText) || strlen(minuteText) > 4 ||
            (minutes = strval(minuteText)) < 1 || minutes > 1440)
        {
            SendClientMessage(playerid, C_RED, "Invalid target or duration (1-1440 minutes).");
            return 1;
        }
        new until = gettime() + minutes * 60, query[220];
        format(query, sizeof query,
            "INSERT OR REPLACE INTO mutes(name,muted_until) VALUES('%s',%d)",
            gPlayerName[target], until);
        new DBResult:r = db_query(gDb, query);
        if (!r) { SendClientMessage(playerid, C_RED, "Unable to save mute."); return 1; }
        db_free_result(r);
        gMutedUntil[target] = until;
        AuditModeration(playerid, "mute", gPlayerName[target]);
        SendClientMessage(target, C_RED, "You were muted by a staff member.");
        SendClientMessage(playerid, C_GREEN, "Mute saved.");
        return 1;
    }

    if (!strcmp(cmdtext, "/unmute ", true, 8))
    {
        if (!IsVeltorStaff(playerid)) return 0;
        new idText[16], target;
        strmid(idText, cmdtext, 8, strlen(cmdtext), sizeof idText);
        if (!ReadTargetId(idText, target) || gStaff[target])
        {
            SendClientMessage(playerid, C_RED, "Invalid target.");
            return 1;
        }
        new query[220];
        format(query, sizeof query, "DELETE FROM mutes WHERE name='%s'", gPlayerName[target]);
        new DBResult:r = db_query(gDb, query);
        if (!r) { SendClientMessage(playerid, C_RED, "Unable to remove mute."); return 1; }
        db_free_result(r);
        gMutedUntil[target] = 0;
        AuditModeration(playerid, "unmute", gPlayerName[target]);
        SendClientMessage(target, C_GREEN, "You were unmuted.");
        SendClientMessage(playerid, C_GREEN, "Mute removed.");
        return 1;
    }
    return 0;
}
