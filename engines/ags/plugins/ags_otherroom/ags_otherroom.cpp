/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ags/plugins/ags_otherroom/ags_otherroom.h"
#include "ags/engine/ac/global_hotspot.h"
#include "ags/engine/ac/global_region.h"
#include "ags/engine/ac/global_walkable_area.h"

namespace AGS3 {
namespace Plugins {
namespace AGSOtherRoom {

const uint32 Magic = 0xB0BB0000;
const uint32 Version = 1;
const uint32 SaveMagic = Magic + Version;

AGSOtherRoom::AGSOtherRoom() : PluginBase() {
	_loaded = false;
	for (int r = 0; r < MAX_ROOMS; r++) {
		RoomState &rs = _rooms[r];
		for (int i = 0; i < MAX_HOTSPOTS; i++)
			rs.hotspotStatus[i] = eOtherRoom_Unknown;
		for (int i = 0; i < MAX_OBJECTS; i++) {
			rs.objectStatus[i] = eOtherRoom_Unknown;
			rs.objectX[i] = 0;
			rs.objectY[i] = 0;
		}
		for (int i = 0; i < MAX_REGIONS; i++)
			rs.regionStatus[i] = eOtherRoom_Unknown;
		for (int i = 0; i < MAX_WALKABLE_AREAS; i++)
			rs.walkableAreaStatus[i] = eOtherRoom_Unknown;
	}
	for (int i = 0; i < MAX_OBJECTS; i++)
		_objectTransparency[i] = 0;
}

const char *AGSOtherRoom::AGS_GetPluginName() {
	return "OtherRoom plugin recreation";
}

void AGSOtherRoom::AGS_EngineStartup(IAGSEngine *engine) {
	PluginBase::AGS_EngineStartup(engine);

	if (_engine->version < 13)
		_engine->AbortGame("Engine interface is too old, need newer version of AGS.");

	SCRIPT_METHOD(OtherRoom::Hotspot_On^2, AGSOtherRoom::Hotspot_On);
	SCRIPT_METHOD(OtherRoom::Hotspot_Off^2, AGSOtherRoom::Hotspot_Off);
	SCRIPT_METHOD(OtherRoom::Hotspot_Toggle^2, AGSOtherRoom::Hotspot_Toggle);
	SCRIPT_METHOD(OtherRoom::GetHotspot_Status^2, AGSOtherRoom::GetHotspot_Status);

	SCRIPT_METHOD(OtherRoom::Object_On^2, AGSOtherRoom::Object_On);
	SCRIPT_METHOD(OtherRoom::Object_Off^2, AGSOtherRoom::Object_Off);
	SCRIPT_METHOD(OtherRoom::Object_Toggle^2, AGSOtherRoom::Object_Toggle);
	SCRIPT_METHOD(OtherRoom::Object_SetPosition^4, AGSOtherRoom::Object_SetPosition);
	SCRIPT_METHOD(OtherRoom::Object_FadeIn^2, AGSOtherRoom::Object_FadeIn);
	SCRIPT_METHOD(OtherRoom::Object_FadeOut^2, AGSOtherRoom::Object_FadeOut);
	SCRIPT_METHOD(OtherRoom::Object_FadeToggle^2, AGSOtherRoom::Object_FadeToggle);
	SCRIPT_METHOD(OtherRoom::GetObject_Status^2, AGSOtherRoom::GetObject_Status);
	SCRIPT_METHOD(OtherRoom::GetObject_X^2, AGSOtherRoom::GetObject_X);
	SCRIPT_METHOD(OtherRoom::GetObject_Y^2, AGSOtherRoom::GetObject_Y);

	SCRIPT_METHOD(OtherRoom::Region_On^2, AGSOtherRoom::Region_On);
	SCRIPT_METHOD(OtherRoom::Region_Off^2, AGSOtherRoom::Region_Off);
	SCRIPT_METHOD(OtherRoom::Region_Toggle^2, AGSOtherRoom::Region_Toggle);
	SCRIPT_METHOD(OtherRoom::GetRegion_Status^2, AGSOtherRoom::GetRegion_Status);

	SCRIPT_METHOD(OtherRoom::WalkableArea_On^2, AGSOtherRoom::WalkableArea_On);
	SCRIPT_METHOD(OtherRoom::WalkableArea_Off^2, AGSOtherRoom::WalkableArea_Off);
	SCRIPT_METHOD(OtherRoom::GetWalkableArea_Status^2, AGSOtherRoom::GetWalkableArea_Status);

	_engine->RequestEventHook(AGSE_ENTERROOM);
	_engine->RequestEventHook(AGSE_SAVEGAME);
	_engine->RequestEventHook(AGSE_RESTOREGAME);
}

int64 AGSOtherRoom::AGS_EngineOnEvent(int event, NumberPtr data) {
	if (event == AGSE_ENTERROOM) {
		int roomNum = (int)(intptr_t)data;
		_currentRoom = roomNum;
		if (!_loaded) {
			_loaded = true;
			return 0;
		}
		applyRoomState(roomNum);
	} else if (event == AGSE_RESTOREGAME) {
		Serializer s(_engine, data, true);
		syncGame(s);
	} else if (event == AGSE_SAVEGAME) {
		Serializer s(_engine, data, false);
		syncGame(s);
	}
	return 0;
}

void AGSOtherRoom::syncGame(Serializer &s) {
	uint32 saveVersion = SaveMagic;
	s.syncAsInt(saveVersion);
	if (s.isLoading() && saveVersion != SaveMagic) {
		s.unreadInt();
		return;
	}

	for (int r = 0; r < MAX_ROOMS; r++) {
		RoomState &rs = _rooms[r];
		for (int i = 0; i < MAX_HOTSPOTS; i++)
			s.syncAsInt(*(int *)&rs.hotspotStatus[i]);
		for (int i = 0; i < MAX_OBJECTS; i++)
			s.syncAsInt(*(int *)&rs.objectStatus[i]);
		for (int i = 0; i < MAX_OBJECTS; i++)
			s.syncAsInt(rs.objectX[i]);
		for (int i = 0; i < MAX_OBJECTS; i++)
			s.syncAsInt(rs.objectY[i]);
		for (int i = 0; i < MAX_REGIONS; i++)
			s.syncAsInt(*(int *)&rs.regionStatus[i]);
		for (int i = 0; i < MAX_WALKABLE_AREAS; i++)
			s.syncAsInt(*(int *)&rs.walkableAreaStatus[i]);
	}

	if (s.isLoading()) {
		_loaded = true;
	}
}

void AGSOtherRoom::applyRoomState(int room) {
	if (room < 0 || room >= MAX_ROOMS)
		return;
	RoomState &rs = _rooms[room];

	for (int i = 1; i < MAX_HOTSPOTS; i++) {
		switch (rs.hotspotStatus[i]) {
		case eOtherRoom_On:
			AGS3::EnableHotspot(i);
			rs.hotspotStatus[i] = eOtherRoom_On;
			break;
		case eOtherRoom_Off:
			AGS3::DisableHotspot(i);
			rs.hotspotStatus[i] = eOtherRoom_Off;
			break;
		case eOtherRoom_Toggle:
			if (i < _engine->GetNumObjects())
				AGS3::DisableHotspot(i);
			rs.hotspotStatus[i] = eOtherRoom_Off;
			break;
		default:
			break;
		}
	}

	for (int i = 0; i < MAX_OBJECTS; i++) {
		switch (rs.objectStatus[i]) {
		case eOtherRoom_On:
			if (AGSObject *obj = _engine->GetObject(i)) {
				obj->on = 1;
			}
			rs.objectStatus[i] = eOtherRoom_On;
			break;
		case eOtherRoom_Off:
			if (AGSObject *obj = _engine->GetObject(i)) {
				obj->on = 0;
			}
			rs.objectStatus[i] = eOtherRoom_Off;
			break;
		case eOtherRoom_Toggle:
			if (AGSObject *obj = _engine->GetObject(i)) {
				obj->on = obj->on ? 0 : 1;
			}
			rs.objectStatus[i] = eOtherRoom_Off;
			break;
		case eOtherRoom_FadeToggle:
		case eOtherRoom_FadeIn:
		case eOtherRoom_FadeOut:
			break;
		default:
			break;
		}

		if (rs.objectX[i] != 0 || rs.objectY[i] != 0) {
			if (AGSObject *obj = _engine->GetObject(i)) {
				obj->x = rs.objectX[i];
				obj->y = rs.objectY[i];
			}
		}
	}

	for (int i = 0; i < MAX_REGIONS; i++) {
		switch (rs.regionStatus[i]) {
		case eOtherRoom_On:
			AGS3::EnableRegion(i);
			rs.regionStatus[i] = eOtherRoom_On;
			break;
		case eOtherRoom_Off:
			AGS3::DisableRegion(i);
			rs.regionStatus[i] = eOtherRoom_Off;
			break;
		case eOtherRoom_Toggle:
			AGS3::DisableRegion(i);
			rs.regionStatus[i] = eOtherRoom_Off;
			break;
		default:
			break;
		}
	}

	for (int i = 1; i < MAX_WALKABLE_AREAS; i++) {
		switch (rs.walkableAreaStatus[i]) {
		case eOtherRoom_On:
			AGS3::RestoreWalkableArea(i);
			rs.walkableAreaStatus[i] = eOtherRoom_On;
			break;
		case eOtherRoom_Off:
			AGS3::RemoveWalkableArea(i);
			rs.walkableAreaStatus[i] = eOtherRoom_Off;
			break;
		default:
			break;
		}
	}
}

void AGSOtherRoom::Hotspot_On(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, hotspotid);
	if (hotspotid < 1 || hotspotid >= MAX_HOTSPOTS)
		return;
	if (roomid == _currentRoom) {
		AGS3::EnableHotspot(hotspotid);
	}
	_rooms[roomid].hotspotStatus[hotspotid] = eOtherRoom_On;
}

void AGSOtherRoom::Hotspot_Off(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, hotspotid);
	if (hotspotid < 1 || hotspotid >= MAX_HOTSPOTS)
		return;
	if (roomid == _currentRoom) {
		AGS3::DisableHotspot(hotspotid);
	}
	_rooms[roomid].hotspotStatus[hotspotid] = eOtherRoom_Off;
}

void AGSOtherRoom::Hotspot_Toggle(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, hotspotid);
	if (hotspotid < 1 || hotspotid >= MAX_HOTSPOTS)
		return;
	if (roomid == _currentRoom) {
		AGS3::DisableHotspot(hotspotid);
		_rooms[roomid].hotspotStatus[hotspotid] = eOtherRoom_Off;
	} else {
		_rooms[roomid].hotspotStatus[hotspotid] = eOtherRoom_Toggle;
	}
}

void AGSOtherRoom::GetHotspot_Status(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, hotspotid);
	if (hotspotid < 0 || hotspotid >= MAX_HOTSPOTS) {
		params._result = (int)eOtherRoom_DoesNotExist;
		return;
	}
	params._result = (int)_rooms[roomid].hotspotStatus[hotspotid];
}

void AGSOtherRoom::Object_On(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		if (AGSObject *obj = _engine->GetObject(objectid))
			obj->on = 1;
	}
	_rooms[roomid].objectStatus[objectid] = eOtherRoom_On;
}

void AGSOtherRoom::Object_Off(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		if (AGSObject *obj = _engine->GetObject(objectid))
			obj->on = 0;
	}
	_rooms[roomid].objectStatus[objectid] = eOtherRoom_Off;
}

void AGSOtherRoom::Object_Toggle(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		AGSObject *obj = _engine->GetObject(objectid);
		if (obj) {
			obj->on = obj->on ? 0 : 1;
			_rooms[roomid].objectStatus[objectid] = obj->on ? eOtherRoom_On : eOtherRoom_Off;
		}
	} else {
		_rooms[roomid].objectStatus[objectid] = eOtherRoom_Toggle;
	}
}

void AGSOtherRoom::Object_SetPosition(ScriptMethodParams &params) {
	PARAMS4(int, roomid, int, objectid, int, roomx, int, roomy);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	_rooms[roomid].objectX[objectid] = (int16)roomx;
	_rooms[roomid].objectY[objectid] = (int16)roomy;
	if (roomid == _currentRoom) {
		if (AGSObject *obj = _engine->GetObject(objectid)) {
			obj->x = roomx;
			obj->y = roomy;
		}
	}
}

void AGSOtherRoom::Object_FadeIn(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		if (AGSObject *obj = _engine->GetObject(objectid)) {
			obj->on = 1;
			obj->transparent = 0;
		}
	}
	_rooms[roomid].objectStatus[objectid] = eOtherRoom_On;
	_objectTransparency[objectid] = 0;
}

void AGSOtherRoom::Object_FadeOut(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		if (AGSObject *obj = _engine->GetObject(objectid)) {
			obj->on = 0;
		}
	}
	_rooms[roomid].objectStatus[objectid] = eOtherRoom_Off;
}

void AGSOtherRoom::Object_FadeToggle(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS)
		return;
	if (roomid == _currentRoom) {
		AGSObject *obj = _engine->GetObject(objectid);
		if (obj) {
			obj->on = obj->on ? 0 : 1;
			_rooms[roomid].objectStatus[objectid] = obj->on ? eOtherRoom_On : eOtherRoom_Off;
		}
	} else {
		_rooms[roomid].objectStatus[objectid] = eOtherRoom_FadeToggle;
	}
}

void AGSOtherRoom::GetObject_Status(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS) {
		params._result = (int)eOtherRoom_DoesNotExist;
		return;
	}
	params._result = (int)_rooms[roomid].objectStatus[objectid];
}

void AGSOtherRoom::GetObject_X(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS) {
		params._result = -29746;
		return;
	}
	OtherRoomStatus st = _rooms[roomid].objectStatus[objectid];
	if (st == eOtherRoom_Unknown || st == eOtherRoom_DoesNotExist) {
		params._result = -29746;
		return;
	}
	params._result = _rooms[roomid].objectX[objectid];
}

void AGSOtherRoom::GetObject_Y(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, objectid);
	if (objectid < 0 || objectid >= MAX_OBJECTS) {
		params._result = -29746;
		return;
	}
	OtherRoomStatus st = _rooms[roomid].objectStatus[objectid];
	if (st == eOtherRoom_Unknown || st == eOtherRoom_DoesNotExist) {
		params._result = -29746;
		return;
	}
	params._result = _rooms[roomid].objectY[objectid];
}

void AGSOtherRoom::Region_On(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, regionid);
	if (regionid < 0 || regionid >= MAX_REGIONS)
		return;
	if (roomid == _currentRoom)
		AGS3::EnableRegion(regionid);
	_rooms[roomid].regionStatus[regionid] = eOtherRoom_On;
}

void AGSOtherRoom::Region_Off(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, regionid);
	if (regionid < 0 || regionid >= MAX_REGIONS)
		return;
	if (roomid == _currentRoom)
		AGS3::DisableRegion(regionid);
	_rooms[roomid].regionStatus[regionid] = eOtherRoom_Off;
}

void AGSOtherRoom::Region_Toggle(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, regionid);
	if (regionid < 0 || regionid >= MAX_REGIONS)
		return;
	if (roomid == _currentRoom) {
		AGS3::DisableRegion(regionid);
		_rooms[roomid].regionStatus[regionid] = eOtherRoom_Off;
	} else {
		_rooms[roomid].regionStatus[regionid] = eOtherRoom_Toggle;
	}
}

void AGSOtherRoom::GetRegion_Status(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, regionid);
	if (regionid < 0 || regionid >= MAX_REGIONS) {
		params._result = (int)eOtherRoom_DoesNotExist;
		return;
	}
	params._result = (int)_rooms[roomid].regionStatus[regionid];
}

void AGSOtherRoom::WalkableArea_On(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, areaid);
	if (areaid < 1 || areaid >= MAX_WALKABLE_AREAS)
		return;
	if (roomid == _currentRoom)
		AGS3::RestoreWalkableArea(areaid);
	_rooms[roomid].walkableAreaStatus[areaid] = eOtherRoom_On;
}

void AGSOtherRoom::WalkableArea_Off(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, areaid);
	if (areaid < 1 || areaid >= MAX_WALKABLE_AREAS)
		return;
	if (roomid == _currentRoom)
		AGS3::RemoveWalkableArea(areaid);
	_rooms[roomid].walkableAreaStatus[areaid] = eOtherRoom_Off;
}

void AGSOtherRoom::GetWalkableArea_Status(ScriptMethodParams &params) {
	PARAMS2(int, roomid, int, areaid);
	if (areaid < 0 || areaid >= MAX_WALKABLE_AREAS) {
		params._result = (int)eOtherRoom_DoesNotExist;
		return;
	}
	params._result = (int)_rooms[roomid].walkableAreaStatus[areaid];
}

} // namespace AGSOtherRoom
} // namespace Plugins
} // namespace AGS3
