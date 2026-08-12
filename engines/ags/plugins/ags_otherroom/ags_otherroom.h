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

#ifndef AGS_PLUGINS_AGS_OTHERROOM_AGS_OTHERROOM_H
#define AGS_PLUGINS_AGS_OTHERROOM_AGS_OTHERROOM_H

#include "ags/plugins/plugin_base.h"
#include "ags/plugins/serializer.h"
#include "common/array.h"

namespace AGS3 {
namespace Plugins {
namespace AGSOtherRoom {

enum OtherRoomStatus {
	eOtherRoom_Unknown = 0,
	eOtherRoom_Off,
	eOtherRoom_On,
	eOtherRoom_Toggle,
	eOtherRoom_FadeIn,
	eOtherRoom_FadeOut,
	eOtherRoom_FadeToggle,
	eOtherRoom_DoesNotExist
};

class AGSOtherRoom : public PluginBase {
	SCRIPT_HASH(AGSOtherRoom)
private:
	static const int MAX_ROOMS = 1000;
	static const int MAX_HOTSPOTS = 30;
	static const int MAX_OBJECTS = 20;
	static const int MAX_REGIONS = 16;
	static const int MAX_WALKABLE_AREAS = 16;

	struct RoomState {
		OtherRoomStatus hotspotStatus[MAX_HOTSPOTS];
		OtherRoomStatus objectStatus[MAX_OBJECTS];
		int16 objectX[MAX_OBJECTS];
		int16 objectY[MAX_OBJECTS];
		OtherRoomStatus regionStatus[MAX_REGIONS];
		OtherRoomStatus walkableAreaStatus[MAX_WALKABLE_AREAS];
	};

	RoomState _rooms[MAX_ROOMS];
	uint8 _objectTransparency[MAX_OBJECTS];
	int _currentRoom;
	bool _loaded;

	void applyRoomState(int room);
	void syncGame(Serializer &s);

	void Hotspot_On(ScriptMethodParams &params);
	void Hotspot_Off(ScriptMethodParams &params);
	void Hotspot_Toggle(ScriptMethodParams &params);
	void GetHotspot_Status(ScriptMethodParams &params);

	void Object_On(ScriptMethodParams &params);
	void Object_Off(ScriptMethodParams &params);
	void Object_Toggle(ScriptMethodParams &params);
	void Object_SetPosition(ScriptMethodParams &params);
	void Object_FadeIn(ScriptMethodParams &params);
	void Object_FadeOut(ScriptMethodParams &params);
	void Object_FadeToggle(ScriptMethodParams &params);
	void GetObject_Status(ScriptMethodParams &params);
	void GetObject_X(ScriptMethodParams &params);
	void GetObject_Y(ScriptMethodParams &params);

	void Region_On(ScriptMethodParams &params);
	void Region_Off(ScriptMethodParams &params);
	void Region_Toggle(ScriptMethodParams &params);
	void GetRegion_Status(ScriptMethodParams &params);

	void WalkableArea_On(ScriptMethodParams &params);
	void WalkableArea_Off(ScriptMethodParams &params);
	void GetWalkableArea_Status(ScriptMethodParams &params);

public:
	AGSOtherRoom();
	virtual ~AGSOtherRoom() {}

	const char *AGS_GetPluginName() override;
	void AGS_EngineStartup(IAGSEngine *engine) override;
	int64 AGS_EngineOnEvent(int event, NumberPtr data) override;
	int AGS_PluginV2() const override { return 1; };
};

} // namespace AGSOtherRoom
} // namespace Plugins
} // namespace AGS3

#endif
