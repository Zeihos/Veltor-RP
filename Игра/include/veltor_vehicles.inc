#if defined _veltor_vehicles_included
    #endinput
#endif
#define _veltor_vehicles_included

// Experimental single-car garage. For production, enforce a dealer and towing rules.
#define CAR_MODEL 560
#define CAR_PRICE 15000
#define CAR_RESALE 7500
#define CAR_MARKER 6
#define CAR_PARK_X 1493.1
#define CAR_PARK_Y -1725.5
#define CAR_PARK_Z 13.3
#define CAR_PARK_A 90.0

stock bool:VehicleSQL(const query[])
{
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;
    db_free_result(r);
    return true;
}

stock bool:InitVehicleDB()
{
    return VehicleSQL("CREATE TABLE IF NOT EXISTS player_vehicles (name TEXT PRIMARY KEY COLLATE NOCASE, model INTEGER NOT NULL, x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, angle REAL NOT NULL, locked INTEGER NOT NULL DEFAULT 1)");
}

stock ResetVehicleParking(playerid)
{
    gCarParkX[playerid] = CAR_PARK_X;
    gCarParkY[playerid] = CAR_PARK_Y;
    gCarParkZ[playerid] = CAR_PARK_Z;
    gCarParkAngle[playerid] = CAR_PARK_A;
    gCarLocked[playerid] = true;
    return 1;
}

stock bool:VehiclePositionValid(Float:x, Float:y, Float:z, Float:a)
{
    return x > -3000.0 && x < 3000.0 &&
        y > -3000.0 && y < 3000.0 &&
        z > -100.0 && z < 1600.0 &&
        a >= 0.0 && a <= 360.0;
}

stock bool:LoadOwnedVehicle(playerid)
{
    ResetVehicleParking(playerid);
    new query[240];
    format(query, sizeof query,
        "SELECT model,x,y,z,angle,locked FROM player_vehicles WHERE name='%s' LIMIT 1",
        gPlayerName[playerid]);
    new DBResult:r = db_query(gDb, query);
    if (!r) return false;

    if (db_num_rows(r) > 0)
    {
        new model = db_get_field_assoc_int(r, "model");
        if (model != CAR_MODEL)
        {
            db_free_result(r);
            return false;
        }
        gOwnedCar[playerid] = model;
        gCarParkX[playerid] = db_get_field_assoc_float(r, "x");
        gCarParkY[playerid] = db_get_field_assoc_float(r, "y");
        gCarParkZ[playerid] = db_get_field_assoc_float(r, "z");
        gCarParkAngle[playerid] = db_get_field_assoc_float(r, "angle");
        gCarLocked[playerid] = (db_get_field_assoc_int(r, "locked") != 0);
        db_free_result(r);
        if (!VehiclePositionValid(
            gCarParkX[playerid], gCarParkY[playerid],
            gCarParkZ[playerid], gCarParkAngle[playerid]))
            ResetVehicleParking(playerid);
        return true;
    }

    db_free_result(r);
    // Existing pre-garage test accounts stored the car model in accounts.car.
    if (gOwnedCar[playerid] == CAR_MODEL)
    {
        format(query, sizeof query,
            "INSERT OR IGNORE INTO player_vehicles(name,model,x,y,z,angle,locked) VALUES('%s',560,1493.1,-1725.5,13.3,90,1)",
            gPlayerName[playerid]);
        if (!VehicleSQL(query)) return false;
    }
    return true;
}

stock SyncVehicleLocks(owner)
{
    if (gCarId[owner] == 0) return 1;
    for (new viewer=0; viewer<MAX_PLAYERS; viewer++)
    {
        if (!IsPlayerConnected(viewer)) continue;
        new doors = (gCarLocked[owner] && viewer != owner) ? 1 : 0;
        SetVehicleParamsForPlayer(gCarId[owner], viewer, 0, doors);
    }
    return 1;
}

stock SyncVehicleLocksForViewer(playerid)
{
    for (new owner=0; owner<MAX_PLAYERS; owner++)
        if (owner != playerid && IsPlayerConnected(owner) && gCarId[owner] != 0)
        {
            new doors = gCarLocked[owner] ? 1 : 0;
            SetVehicleParamsForPlayer(gCarId[owner], playerid, 0, doors);
        }
    return 1;
}

stock OwnedCarOwner(vehicleid)
{
    for (new owner=0; owner<MAX_PLAYERS; owner++)
        if (IsPlayerConnected(owner) && gAuthed[owner] && gCarId[owner] == vehicleid)
            return owner;
    return INVALID_PLAYER_ID;
}

stock bool:CarIsOccupied(vehicleid)
{
    for (new i=0; i<MAX_PLAYERS; i++)
        if (IsPlayerConnected(i) && IsPlayerInAnyVehicle(i) &&
            GetPlayerVehicleID(i) == vehicleid)
            return true;
    return false;
}

stock bool:CommitCarPurchase(playerid)
{
    new balance = gMoney[playerid] - CAR_PRICE;
    if (balance < 0 || gOwnedCar[playerid] != 0) return false;
    if (!VehicleSQL("BEGIN IMMEDIATE TRANSACTION")) return false;

    new bool:ok = true, query[320];
    format(query, sizeof query,
        "INSERT INTO player_vehicles(name,model,x,y,z,angle,locked) VALUES('%s',560,1493.1,-1725.5,13.3,90,1)",
        gPlayerName[playerid]);
    if (!VehicleSQL(query)) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d,car=560 WHERE name='%s'",
            balance, gPlayerName[playerid]);
        if (!VehicleSQL(query)) ok = false;
    }

    if (ok && VehicleSQL("COMMIT"))
    {
        gOwnedCar[playerid] = CAR_MODEL;
        ResetVehicleParking(playerid);
        SetCash(playerid, balance);
        return true;
    }
    VehicleSQL("ROLLBACK");
    return false;
}

stock bool:CommitCarResale(playerid)
{
    new balance = gMoney[playerid] + CAR_RESALE;
    if (gOwnedCar[playerid] == 0 || balance > 100000000) return false;
    if (!VehicleSQL("BEGIN IMMEDIATE TRANSACTION")) return false;

    new bool:ok = true, query[220];
    format(query, sizeof query,
        "DELETE FROM player_vehicles WHERE name='%s'", gPlayerName[playerid]);
    if (!VehicleSQL(query)) ok = false;
    if (ok)
    {
        format(query, sizeof query,
            "UPDATE accounts SET money=%d,car=0 WHERE name='%s'",
            balance, gPlayerName[playerid]);
        if (!VehicleSQL(query)) ok = false;
    }
    if (ok && VehicleSQL("COMMIT"))
    {
        gOwnedCar[playerid] = 0;
        ResetVehicleParking(playerid);
        RemovePlayerMapIcon(playerid, CAR_MARKER);
        SetCash(playerid, balance);
        return true;
    }
    VehicleSQL("ROLLBACK");
    return false;
}

stock bool:SaveVehicleParking(playerid, Float:x, Float:y, Float:z, Float:angle)
{
    if (!VehiclePositionValid(x, y, z, angle)) return false;
    new query[340];
    format(query, sizeof query,
        "UPDATE player_vehicles SET x=%f,y=%f,z=%f,angle=%f WHERE name='%s'",
        x, y, z, angle, gPlayerName[playerid]);
    if (!VehicleSQL(query)) return false;
    gCarParkX[playerid] = x;
    gCarParkY[playerid] = y;
    gCarParkZ[playerid] = z;
    gCarParkAngle[playerid] = angle;
    return true;
}

stock bool:SaveVehicleLock(playerid, bool:locked)
{
    new query[230];
    format(query, sizeof query,
        "UPDATE player_vehicles SET locked=%d WHERE name='%s'",
        locked ? 1 : 0, gPlayerName[playerid]);
    if (!VehicleSQL(query)) return false;
    gCarLocked[playerid] = locked;
    return true;
}

stock HandleVehicleCommand(playerid, const cmdtext[])
{
    if (!strcmp(cmdtext, "/garage", true))
    {
        if (gOwnedCar[playerid] == 0)
        {
            SendClientMessage(playerid, C_WHITE, "You do not own a vehicle. Buy a Sultan with /buycar near City Hall.");
            return 1;
        }
        new line[160];
        format(line, sizeof line,
            "Sultan | %s | %s | Parking: %.1f, %.1f",
            gCarId[playerid] == 0 ? ("stored") : ("outside"),
            gCarLocked[playerid] ? ("locked") : ("unlocked"),
            gCarParkX[playerid], gCarParkY[playerid]);
        SendClientMessage(playerid, C_GREEN, line);
        SendClientMessage(playerid, C_WHITE, "Commands: /car /carfind /park /carlock /carstore /carsell");
        return 1;
    }
    if (!strcmp(cmdtext, "/buycar", true))
    {
        if (gOwnedCar[playerid] != 0)
        {
            SendClientMessage(playerid, C_RED, "You already own a vehicle.");
            return 1;
        }
        if (!IsPlayerInRangeOfPoint(playerid, 35.0, START_X, START_Y, START_Z) ||
            GetPlayerInterior(playerid) != 0 || IsPlayerInAnyVehicle(playerid))
        {
            SendClientMessage(playerid, C_RED, "Buy your car at City Hall, on foot.");
            return 1;
        }
        if (gMoney[playerid] < CAR_PRICE)
        {
            SendClientMessage(playerid, C_RED, "Sultan costs $15000.");
            return 1;
        }
        if (!CommitCarPurchase(playerid))
        {
            SendClientMessage(playerid, C_RED, "Could not save purchase. Your cash was not changed.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Sultan purchased and saved! Use /carfind to find parking.");
        return 1;
    }
    if (!strcmp(cmdtext, "/carfind", true))
    {
        if (gOwnedCar[playerid] == 0) return HandleVehicleCommand(playerid, "/garage");
        SetPlayerMapIcon(playerid, CAR_MARKER, gCarParkX[playerid],
            gCarParkY[playerid], gCarParkZ[playerid], 55, C_ORANGE, MAPICON_LOCAL);
        SendClientMessage(playerid, C_GREEN, "Your saved parking location is marked on the map.");
        return 1;
    }
    if (!strcmp(cmdtext, "/car", true))
    {
        if (gOwnedCar[playerid] == 0) return HandleVehicleCommand(playerid, "/garage");
        if (gCarId[playerid] != 0)
        {
            SendClientMessage(playerid, C_WHITE, "Your car is already outside. Use /carfind or /carstore.");
            return 1;
        }
        if (!VehiclePositionValid(gCarParkX[playerid],gCarParkY[playerid],
            gCarParkZ[playerid],gCarParkAngle[playerid]))
        {
            SendClientMessage(playerid, C_RED, "Invalid parking location. Contact an administrator.");
            return 1;
        }
        gCarId[playerid] = CreateVehicle(gOwnedCar[playerid], gCarParkX[playerid],
            gCarParkY[playerid], gCarParkZ[playerid], gCarParkAngle[playerid], 3, 3, -1);
        if (gCarId[playerid] == INVALID_VEHICLE_ID || gCarId[playerid] == 0)
        {
            gCarId[playerid] = 0;
            SendClientMessage(playerid, C_RED, "Server could not create the vehicle.");
            return 1;
        }
        SyncVehicleLocks(playerid);
        SendClientMessage(playerid, C_GREEN, "Your Sultan is at the saved parking location. Use /carfind.");
        return 1;
    }
    if (!strcmp(cmdtext, "/carstore", true))
    {
        if (gCarId[playerid] == 0)
        {
            SendClientMessage(playerid, C_WHITE, "Your car is already stored.");
            return 1;
        }
        if (CarIsOccupied(gCarId[playerid]))
        {
            SendClientMessage(playerid, C_RED, "Everyone must exit the car before storing it.");
            return 1;
        }
        DestroyVehicle(gCarId[playerid]);
        gCarId[playerid] = 0;
        SendClientMessage(playerid, C_GREEN, "Car returned to the garage. Parking location was preserved.");
        return 1;
    }
    if (!strcmp(cmdtext, "/carlock", true))
    {
        if (gCarId[playerid] == 0) return HandleVehicleCommand(playerid, "/garage");
        new Float:x, Float:y, Float:z;
        GetVehiclePos(gCarId[playerid], x, y, z);
        if (!IsPlayerInRangeOfPoint(playerid, 20.0, x, y, z))
        {
            SendClientMessage(playerid, C_RED, "Stand near your vehicle to use its lock.");
            return 1;
        }
        new bool:locked = !gCarLocked[playerid];
        if (!SaveVehicleLock(playerid, locked))
        {
            SendClientMessage(playerid, C_RED, "Could not update vehicle lock.");
            return 1;
        }
        SyncVehicleLocks(playerid);
        SendClientMessage(playerid, C_GREEN, locked ? "Vehicle locked." : "Vehicle unlocked.");
        return 1;
    }
    if (!strcmp(cmdtext, "/park", true))
    {
        if (gCarId[playerid] == 0 || GetPlayerState(playerid) != PLAYER_STATE_DRIVER ||
            GetPlayerVehicleID(playerid) != gCarId[playerid])
        {
            SendClientMessage(playerid, C_RED, "Drive your own car to the parking spot to save it.");
            return 1;
        }
        if (GetPlayerVirtualWorld(playerid) != 0 || GetPlayerInterior(playerid) != 0)
        {
            SendClientMessage(playerid, C_RED, "Parking is only available outdoors in world 0.");
            return 1;
        }
        new Float:x, Float:y, Float:z, Float:a;
        GetVehiclePos(gCarId[playerid], x, y, z);
        GetVehicleZAngle(gCarId[playerid], a);
        new Float:vx,Float:vy,Float:vz;
        GetVehicleVelocity(gCarId[playerid],vx,vy,vz);
        if (floatabs(vx)>0.04 || floatabs(vy)>0.04 || floatabs(vz)>0.04)
        {
            SendClientMessage(playerid, C_RED, "Stop the car before saving the parking spot.");
            return 1;
        }
        if (!SaveVehicleParking(playerid, x, y, z, a))
        {
            SendClientMessage(playerid, C_RED, "Parking location could not be saved.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Parking spot saved. It will be used for future spawns.");
        return 1;
    }
    if (!strcmp(cmdtext, "/carsell", true))
    {
        if (gOwnedCar[playerid] == 0) return HandleVehicleCommand(playerid, "/garage");
        if (gCarId[playerid] != 0)
        {
            SendClientMessage(playerid, C_RED, "Store your vehicle using /carstore before selling.");
            return 1;
        }
        if (!IsPlayerInRangeOfPoint(playerid, 35.0, START_X, START_Y, START_Z) ||
            IsPlayerInAnyVehicle(playerid))
        {
            SendClientMessage(playerid, C_RED, "Sell your vehicle at City Hall, on foot.");
            return 1;
        }
        if (!CommitCarResale(playerid))
        {
            SendClientMessage(playerid, C_RED, "Sale could not be saved. Your cash was not changed.");
            return 1;
        }
        SendClientMessage(playerid, C_GREEN, "Vehicle sold for $7500.");
        return 1;
    }
    return 0;
}

stock OnPlayerVehicleStateChange(playerid, PLAYER_STATE:newstate)
{
    if (newstate != PLAYER_STATE_DRIVER && newstate != PLAYER_STATE_PASSENGER)
        return 1;
    new vehicleid=GetPlayerVehicleID(playerid);
    new owner=OwnedCarOwner(vehicleid);
    if (owner != INVALID_PLAYER_ID && owner != playerid && gCarLocked[owner])
    {
        RemovePlayerFromVehicle(playerid);
        SendClientMessage(playerid, C_RED, "This vehicle is locked by its owner.");
    }
    return 1;
}

stock OnOwnedVehicleDeath(vehicleid)
{
    new owner=OwnedCarOwner(vehicleid);
    if (owner != INVALID_PLAYER_ID)
    {
        gCarId[owner] = 0;
        SendClientMessage(owner, C_RED, "Your vehicle was destroyed. Use /car to retrieve it.");
    }
    return 1;
}
