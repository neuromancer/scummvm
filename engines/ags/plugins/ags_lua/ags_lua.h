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

#ifndef AGS_PLUGINS_AGS_LUA_AGS_LUA_H
#define AGS_PLUGINS_AGS_LUA_AGS_LUA_H

#include "ags/plugins/plugin_base.h"
#include "ags/plugins/ags_plugin.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

struct lua_State;

namespace AGS3 {
namespace Plugins {
namespace AGSLua {

struct ScriptEntry {
	Common::String name;
	byte *bytecode;
	size_t size;
};

class AGSLua : public PluginBase {
	SCRIPT_HASH(AGSLua)
private:
	struct LuaManagedTable;
	lua_State *_L;
	Common::HashMap<Common::String, ScriptEntry> _scripts;
	Common::HashMap<Common::String, Common::String> _modules;
	Common::HashMap<intptr_t, LuaManagedTable *> _managedTables;
	intptr_t _nextTableId;
	bool _loaded;

	void loadLscriptsDat();
	void clearManagedTables(bool unref);
	intptr_t refManagedTable();
	void registerBridgeFunctions();
	void registerConstants();
	void registerCharacterMeta();
	void registerObjectMeta();
	void registerHotspotMeta();
	void registerRegionMeta();
	void registerGUIMeta();
	void registerButtonMeta();
	void registerLabelMeta();
	void registerTextBoxMeta();
	void registerSliderMeta();
	void registerListBoxMeta();
	void registerInventoryMeta();
	void registerAudioChannelMeta();
	void warnStub(const char *name);

	void Lua_RunScript(ScriptMethodParams &params);
	void Lua_RequireModule(ScriptMethodParams &params);
	void Lua_SetVar(ScriptMethodParams &params);
	void Lua_GetVar(ScriptMethodParams &params);
	void Lua_Evaluate(ScriptMethodParams &params);
	void Lua_Call(ScriptMethodParams &params);
	void Lua_CreateTable(ScriptMethodParams &params);
	void Lua_LuaValueList(ScriptMethodParams &params);

public:
	IAGSEngine *getEngine() const { return _engine; }
	AGSLua();
	virtual ~AGSLua();

	const char *AGS_GetPluginName() override;
	void AGS_EngineStartup(IAGSEngine *engine) override;
	void AGS_EngineShutdown() override;
	int64 AGS_EngineOnEvent(int event, NumberPtr data) override;
	int AGS_PluginV2() const override { return 1; };
};

} // namespace AGSLua
} // namespace Plugins
} // namespace AGS3

#endif
