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

#include "ags/plugins/ags_lua/ags_lua.h"
#include "ags/plugins/ags_plugin.h"
#include "ags/shared/util/iags_stream.h"
#include "ags/engine/ac/global_display.h"
#include "ags/engine/ac/global_game.h"
#include "ags/engine/ac/global_room.h"
#include "ags/engine/ac/global_character.h"
#include "ags/engine/ac/global_object.h"
#include "ags/engine/ac/global_hotspot.h"
#include "ags/engine/ac/global_region.h"
#include "ags/engine/ac/global_walkable_area.h"
#include "ags/engine/ac/global_mouse.h"
#include "ags/engine/ac/global_inventory_item.h"
#include "ags/engine/ac/global_gui.h"
#include "ags/engine/ac/global_audio.h"
#include "ags/engine/ac/game.h"
#include "ags/engine/ac/character.h"
#include "ags/engine/ac/object.h"
#include "ags/engine/ac/hotspot.h"
#include "ags/engine/ac/region.h"
#include "ags/engine/ac/room.h"
#include "ags/engine/ac/gui.h"
#include "ags/engine/ac/dialog.h"
#include "common/lua/lua.h"
#include "common/lua/lauxlib.h"
#include "common/lua/lualib.h"
#include "common/lua/lua_persistence.h"
#include "common/compression/deflate.h"
#include "common/memstream.h"

namespace AGS3 {
namespace Plugins {
namespace AGSLua {

const uint32 Magic = 0xADD0C000;
const uint32 Version = 1;
const uint32 SaveMagic = Magic + Version;

static const char *AGS_LUA_REGISTRY = "agsLuaPlugin";

static AGSLua *getPlugin(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, AGS_LUA_REGISTRY);
	AGSLua *p = (AGSLua *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return p;
}

struct AGSLua::LuaManagedTable {
	lua_State *L;
	int ref;
	LuaManagedTable() : L(nullptr), ref(LUA_NOREF) {}
};

static int stubIndex(lua_State *L) {
	return 0;
}

// Globals exposed by luaL_openlibs (plus the "ags" bridge table) contain C
// functions that the Pluto persistence cannot serialize. They are listed here
// so they can be stored as permanents: on save they are written by reference,
// and on load they are resolved against the freshly registered state.
static void pushPermanentsTable(lua_State *L, bool forUnpersist) {
	static const char *permNames[] = {
		"coroutine", "debug", "io", "math", "os", "package", "string", "table",
		"assert", "collectgarbage", "dofile", "error", "gcinfo", "getfenv",
		"getmetatable", "ipairs", "load", "loadfile", "loadstring", "module",
		"newproxy", "next", "pairs", "pcall", "print", "rawequal", "rawget",
		"rawset", "require", "select", "setfenv", "setmetatable", "tonumber",
		"tostring", "type", "unpack", "xpcall", "_VERSION", "ags",
		nullptr
	};

	lua_newtable(L);
	for (int i = 0; permNames[i]; i++) {
		lua_getglobal(L, permNames[i]);
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		lua_pushstring(L, permNames[i]);
		if (forUnpersist)
			lua_settable(L, -3);        // permTbl[name] = obj
		else {
			lua_insert(L, -2);          // swap so obj is the key
			lua_settable(L, -3);        // permTbl[obj] = name
		}
	}
}

static int persistLuaClosure(lua_State *L) {
	Common::WriteStream *ws = (Common::WriteStream *)lua_touserdata(L, lua_upvalueindex(1));
	pushPermanentsTable(L, false);
	lua_getglobal(L, "_G");
	Lua::persistLua(L, ws);
	lua_pop(L, 2);
	return 0;
}

static int unpersistLuaClosure(lua_State *L) {
	Common::ReadStream *rs = (Common::ReadStream *)lua_touserdata(L, lua_upvalueindex(1));
	pushPermanentsTable(L, true);
	Lua::unpersistLua(L, rs);
	// unpersistLua leaves the restored global table on the stack
	lua_remove(L, -2);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (lua_isstring(L, -2) && strcmp(lua_tostring(L, -2), "_G") != 0) {
			lua_pushvalue(L, -2);
			lua_pushvalue(L, -2);
			lua_settable(L, LUA_GLOBALSINDEX);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return 0;
}

// --- Bridge functions (Lua → AGS) ---

static int bridgeDisplay(lua_State *L) {
	const char *text = lua_tostring(L, 1);
	if (text)
		AGS3::Display(text);
	return 0;
}

static int bridgeGetLocationType(lua_State *L) {
	int x = (int)lua_tointeger(L, 1);
	int y = (int)lua_tointeger(L, 2);
	lua_pushinteger(L, AGS3::GetLocationType(x, y));
	return 1;
}

static int bridgeGetHotspotAt(lua_State *L) {
	int x = (int)lua_tointeger(L, 1);
	int y = (int)lua_tointeger(L, 2);
	lua_pushinteger(L, AGS3::GetHotspotIDAtScreen(x, y));
	return 1;
}

static int bridgeGetWalkableAreaAt(lua_State *L) {
	int x = (int)lua_tointeger(L, 1);
	int y = (int)lua_tointeger(L, 2);
	lua_pushinteger(L, AGS3::GetWalkableAreaAtScreen(x, y));
	return 1;
}

static int bridgeGetPlayerChar(lua_State *L) {
	lua_pushinteger(L, AGS3::GetPlayerCharacter());
	return 1;
}

static int bridgeSaveGameSlot(lua_State *L) {
	int slot = (int)lua_tointeger(L, 1);
	const char *desc = lua_tostring(L, 2);
	if (desc)
		AGS3::save_game(slot, desc);
	return 0;
}

static int bridgeRestoreGameSlot(lua_State *L) {
	int slot = (int)lua_tointeger(L, 1);
	AGS3::RestoreGameSlot(slot);
	return 0;
}

static int bridgeNewRoom(lua_State *L) {
	int room = (int)lua_tointeger(L, 1);
	AGS3::NewRoom(room);
	return 0;
}

static int bridgeQuitGame(lua_State *L) {
	AGS3::QuitGame(0);
	return 0;
}

static int bridgeGetGameOption(lua_State *L) {
	int opt = (int)lua_tointeger(L, 1);
	lua_pushinteger(L, AGS3::GetGameOption(opt));
	return 1;
}

static int bridgeSetGameOption(lua_State *L) {
	int opt = (int)lua_tointeger(L, 1);
	int val = (int)lua_tointeger(L, 2);
	AGS3::SetGameOption(opt, val);
	return 0;
}

static int bridgeGetGlobalInt(lua_State *L) {
	int idx = (int)lua_tointeger(L, 1);
	lua_pushinteger(L, AGS3::GetGlobalInt(idx));
	return 1;
}

static int bridgeSetGlobalInt(lua_State *L) {
	int idx = (int)lua_tointeger(L, 1);
	int val = (int)lua_tointeger(L, 2);
	AGS3::SetGlobalInt(idx, val);
	return 0;
}

// --- Character metatable ---

static int charIndex(lua_State *L) {
	lua_getfield(L, 1, "id");
	int id = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	const char *key = lua_tostring(L, 2);
	if (!key) return 0;
	AGSCharacter *ch = nullptr;
	AGSLua *p = getPlugin(L);
	if (p && p->getEngine()) ch = p->getEngine()->GetCharacter(id);
	if (!ch) return 0;

	if (!strcmp(key, "x")) { lua_pushinteger(L, ch->x); return 1; }
	if (!strcmp(key, "y")) { lua_pushinteger(L, ch->y); return 1; }
	if (!strcmp(key, "room")) { lua_pushinteger(L, ch->room); return 1; }
	if (!strcmp(key, "view")) { lua_pushinteger(L, ch->view); return 1; }
	if (!strcmp(key, "loop")) { lua_pushinteger(L, ch->loop); return 1; }
	if (!strcmp(key, "frame")) { lua_pushinteger(L, ch->frame); return 1; }
	if (!strcmp(key, "walking")) { lua_pushinteger(L, ch->walking); return 1; }
	if (!strcmp(key, "animating")) { lua_pushinteger(L, ch->animating); return 1; }
	if (!strcmp(key, "baseline")) { lua_pushinteger(L, ch->baseline); return 1; }
	if (!strcmp(key, "talkcolor")) { lua_pushinteger(L, ch->talkcolor); return 1; }
	if (!strcmp(key, "walkspeed")) { lua_pushinteger(L, ch->walkspeed); return 1; }
	if (!strcmp(key, "idleview")) { lua_pushinteger(L, ch->idleview); return 1; }
	if (!strcmp(key, "actx")) { lua_pushinteger(L, ch->actx); return 1; }
	if (!strcmp(key, "acty")) { lua_pushinteger(L, ch->acty); return 1; }
	if (!strcmp(key, "transparency")) { lua_pushinteger(L, ch->transparency); return 1; }
	if (!strcmp(key, "flags")) { lua_pushinteger(L, ch->flags); return 1; }
	return 0;
}

static int charNewIndex(lua_State *L) {
	lua_getfield(L, 1, "id");
	int id = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	const char *key = lua_tostring(L, 2);
	if (!key) return 0;
	AGSCharacter *ch = nullptr;
	AGSLua *p = getPlugin(L);
	if (p && p->getEngine()) ch = p->getEngine()->GetCharacter(id);
	if (!ch) return 0;
	int val = (int)lua_tointeger(L, 3);

	if (!strcmp(key, "x")) { ch->x = val; return 0; }
	if (!strcmp(key, "y")) { ch->y = val; return 0; }
	if (!strcmp(key, "room")) { ch->room = val; return 0; }
	if (!strcmp(key, "view")) { ch->view = val; return 0; }
	if (!strcmp(key, "loop")) { ch->loop = (short)val; return 0; }
	if (!strcmp(key, "frame")) { ch->frame = (short)val; return 0; }
	if (!strcmp(key, "baseline")) { ch->baseline = (short)val; return 0; }
	if (!strcmp(key, "transparency")) { ch->transparency = (short)val; return 0; }
	if (!strcmp(key, "walkspeed")) { ch->walkspeed = (short)val; return 0; }
	return 0;
}

static int charGet(lua_State *L) {
	int id = (int)lua_tointeger(L, 1);
	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	luaL_getmetatable(L, "agsCharacter");
	lua_setmetatable(L, -2);
	return 1;
}

// --- Object metatable ---

static int objIndex(lua_State *L) {
	lua_getfield(L, 1, "id");
	int id = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	const char *key = lua_tostring(L, 2);
	if (!key) return 0;
	AGSObject *obj = nullptr;
	AGSLua *p = getPlugin(L);
	if (p && p->getEngine()) obj = p->getEngine()->GetObject(id);
	if (!obj) return 0;

	if (!strcmp(key, "x")) { lua_pushinteger(L, obj->x); return 1; }
	if (!strcmp(key, "y")) { lua_pushinteger(L, obj->y); return 1; }
	if (!strcmp(key, "on")) { lua_pushinteger(L, obj->on); return 1; }
	if (!strcmp(key, "view")) { lua_pushinteger(L, obj->view); return 1; }
	if (!strcmp(key, "loop")) { lua_pushinteger(L, obj->loop); return 1; }
	if (!strcmp(key, "frame")) { lua_pushinteger(L, obj->frame); return 1; }
	if (!strcmp(key, "transparent")) { lua_pushinteger(L, obj->transparent); return 1; }
	if (!strcmp(key, "baseline")) { lua_pushinteger(L, obj->baseline); return 1; }
	return 0;
}

static int objNewIndex(lua_State *L) {
	lua_getfield(L, 1, "id");
	int id = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	const char *key = lua_tostring(L, 2);
	if (!key) return 0;
	AGSObject *obj = nullptr;
	AGSLua *p = getPlugin(L);
	if (p && p->getEngine()) obj = p->getEngine()->GetObject(id);
	if (!obj) return 0;
	int val = (int)lua_tointeger(L, 3);

	if (!strcmp(key, "x")) { obj->x = val; return 0; }
	if (!strcmp(key, "y")) { obj->y = val; return 0; }
	if (!strcmp(key, "on")) { obj->on = (int8)val; return 0; }
	return 0;
}

static int objGet(lua_State *L) {
	int id = (int)lua_tointeger(L, 1);
	lua_newtable(L);
	lua_pushinteger(L, id);
	luaL_getmetatable(L, "agsObject");
	lua_setmetatable(L, -2);
	return 1;
}

// --- Simple metatable getters ---

static int hotspotGet(lua_State *L) {
	int id = (int)lua_tointeger(L, 1);
	lua_newtable(L);
	lua_pushinteger(L, id);
	luaL_getmetatable(L, "agsHotspot");
	lua_setmetatable(L, -2);
	return 1;
}

static int regionGet(lua_State *L) {
	int id = (int)lua_tointeger(L, 1);
	lua_newtable(L);
	lua_pushinteger(L, id);
	luaL_getmetatable(L, "agsRegion");
	lua_setmetatable(L, -2);
	return 1;
}

static int guiGet(lua_State *L) {
	int id = (int)lua_tointeger(L, 1);
	lua_newtable(L);
	lua_pushinteger(L, id);
	luaL_getmetatable(L, "agsGUI");
	lua_setmetatable(L, -2);
	return 1;
}

static int simpleStubIndex(lua_State *L) {
	return 0;
}

static int stubGetterNoArg(lua_State *L) {
	const char *mt = (const char *)lua_touserdata(L, lua_upvalueindex(1));
	lua_newtable(L);
	luaL_getmetatable(L, mt);
	lua_setmetatable(L, -2);
	return 1;
}

// --- Plugin class ---

AGSLua::AGSLua() : PluginBase() {
	_L = nullptr;
	_loaded = false;
	_nextTableId = 1;
}

AGSLua::~AGSLua() {
	if (_L) {
		clearManagedTables(true);
		lua_close(_L);
	}
	for (Common::HashMap<Common::String, ScriptEntry>::iterator it = _scripts.begin(); it != _scripts.end(); ++it)
		free(it->_value.bytecode);
	_scripts.clear();
}

const char *AGSLua::AGS_GetPluginName() {
	return "AGS Lua plugin recreation";
}

void AGSLua::AGS_EngineStartup(IAGSEngine *engine) {
	PluginBase::AGS_EngineStartup(engine);

	if (_engine->version < 13)
		_engine->AbortGame("Engine interface is too old, need newer version of AGS.");

	SCRIPT_METHOD(Lua::RunScript^1, AGSLua::Lua_RunScript);
	SCRIPT_METHOD(Lua::RequireModule^1, AGSLua::Lua_RequireModule);
	SCRIPT_METHOD(Lua::SetVar^2, AGSLua::Lua_SetVar);
	SCRIPT_METHOD(Lua::GetVar^1, AGSLua::Lua_GetVar);
	SCRIPT_METHOD(Lua::Evaluate^1, AGSLua::Lua_Evaluate);
	// The games were compiled against the upstream agslua headers, whose exact
	// declared parameter counts we cannot verify; register a range of arities
	// so the compiled import names resolve regardless of the declared count.
	SCRIPT_METHOD(Lua::Call^1, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::Call^2, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::Call^3, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::Call^4, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::Call^5, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::Call^6, AGSLua::Lua_Call);
	SCRIPT_METHOD(Lua::CreateTable^1, AGSLua::Lua_CreateTable);
	SCRIPT_METHOD(Lua::CreateTable^2, AGSLua::Lua_CreateTable);
	SCRIPT_METHOD(Lua::CreateTable^4, AGSLua::Lua_CreateTable);
	SCRIPT_METHOD(Lua::CreateTable^6, AGSLua::Lua_CreateTable);
	SCRIPT_METHOD(Lua::CreateTable^8, AGSLua::Lua_CreateTable);
	SCRIPT_METHOD(Lua::LuaValueList^1, AGSLua::Lua_LuaValueList);
	SCRIPT_METHOD(Lua::LuaValueList^2, AGSLua::Lua_LuaValueList);
	SCRIPT_METHOD(Lua::LuaValueList^4, AGSLua::Lua_LuaValueList);
	SCRIPT_METHOD(Lua::LuaValueList^6, AGSLua::Lua_LuaValueList);
	SCRIPT_METHOD(Lua::LuaValueList^8, AGSLua::Lua_LuaValueList);

	_engine->RequestEventHook(AGSE_SAVEGAME);
	_engine->RequestEventHook(AGSE_RESTOREGAME);

	_L = luaL_newstate();
	luaL_openlibs(_L);

	lua_pushlightuserdata(_L, this);
	lua_setfield(_L, LUA_REGISTRYINDEX, AGS_LUA_REGISTRY);

	registerBridgeFunctions();
	registerConstants();

	loadLscriptsDat();
}

void AGSLua::AGS_EngineShutdown() {
	if (_L) {
		clearManagedTables(true);
		lua_close(_L);
		_L = nullptr;
	}
}

intptr_t AGSLua::refManagedTable() {
	LuaManagedTable *mt = new LuaManagedTable();
	mt->L = _L;
	mt->ref = luaL_ref(_L, LUA_REGISTRYINDEX);
	intptr_t id = _nextTableId++;
	_managedTables[id] = mt;
	return id;
}

void AGSLua::clearManagedTables(bool unref) {
	for (Common::HashMap<intptr_t, LuaManagedTable *>::iterator it = _managedTables.begin(); it != _managedTables.end(); ++it) {
		LuaManagedTable *mt = it->_value;
		if (unref && mt->L && mt->ref != LUA_NOREF)
			luaL_unref(mt->L, LUA_REGISTRYINDEX, mt->ref);
		delete mt;
	}
	_managedTables.clear();
}

void AGSLua::loadLscriptsDat() {
	_modules.clear();
	IAGSStream *s = _engine->OpenFileStream("lscripts.dat", AGSSTREAM_FILE_OPEN, AGSSTREAM_MODE_READ);
	if (!s) {
		_loaded = false;
		return;
	}

	int64_t fileLen = s->GetLength();
	if (fileLen < 16) {
		s->Dispose();
		return;
	}

	byte footer[16];
	s->Seek(fileLen - 16, AGSSTREAM_SEEK_SET);
	s->Read(footer, 16);
	uint32 tocOffset = READ_LE_UINT32(footer);
	uint32 tocCompressedLen = READ_LE_UINT32(footer + 4);

	if (tocOffset == 0 || tocCompressedLen == 0 ||
		(int64_t)(tocOffset + tocCompressedLen) > fileLen - 16) {
		s->Dispose();
		return;
	}

	byte *compressed = (byte *)malloc(tocCompressedLen);
	s->Seek(tocOffset, AGSSTREAM_SEEK_SET);
	s->Read(compressed, tocCompressedLen);
	s->Dispose();

	unsigned long dstLen = 1024 * 1024;
	byte *decompressed = (byte *)malloc(dstLen);
	if (!Common::inflateZlib(decompressed, &dstLen, compressed, tocCompressedLen)) {
		free(compressed);
		free(decompressed);
		return;
	}
	free(compressed);

	if (luaL_loadbuffer(_L, (const char *)decompressed, dstLen, "toc")) {
		warning("ags_lua: failed to parse TOC");
		free(decompressed);
		return;
	}
	free(decompressed);

	if (lua_pcall(_L, 0, 1, 0)) {
		warning("ags_lua: failed to execute TOC: %s", lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	if (!lua_istable(_L, -1)) {
		lua_pop(_L, 1);
		return;
	}

	lua_getfield(_L, -1, "files");
	if (lua_istable(_L, -1)) {
		lua_pushnil(_L);
		while (lua_next(_L, -2) != 0) {
			const char *name = lua_tostring(_L, -2);
			if (name && lua_istable(_L, -1)) {
				lua_getfield(_L, -1, "offset");
				lua_getfield(_L, -2, "length");
				uint32 off = (uint32)lua_tointeger(_L, -2);
				uint32 len = (uint32)lua_tointeger(_L, -1);
				lua_pop(_L, 2);

				IAGSStream *fs = _engine->OpenFileStream("lscripts.dat",
					AGSSTREAM_FILE_OPEN, AGSSTREAM_MODE_READ);
				if (fs) {
					byte *scriptComp = (byte *)malloc(len);
					fs->Seek(off, AGSSTREAM_SEEK_SET);
					fs->Read(scriptComp, len);
					fs->Dispose();

					unsigned long ulen = 1024 * 1024;
					byte *udata = (byte *)malloc(ulen);
					if (Common::inflateZlib(udata, &ulen, scriptComp, len)) {
						ScriptEntry entry;
						entry.name = name;
						entry.bytecode = udata;
						entry.size = ulen;
						_scripts[name] = entry;
					} else {
						free(udata);
					}
					free(scriptComp);
				}
			}
			lua_pop(_L, 1);
		}
	}
	lua_pop(_L, 1);

	lua_getfield(_L, -1, "modules");
	if (lua_istable(_L, -1)) {
		lua_pushnil(_L);
		while (lua_next(_L, -2) != 0) {
			const char *mname = lua_tostring(_L, -2);
			const char *fname = lua_tostring(_L, -1);
			if (mname && fname)
				_modules[mname] = fname;
			lua_pop(_L, 1);
		}
	}
	lua_pop(_L, 1);
	lua_pop(_L, 1);

	_loaded = true;
}

void AGSLua::registerBridgeFunctions() {
	static const luaL_Reg agsFuncs[] = {
		{"Display", bridgeDisplay},
		{"GetLocationType", bridgeGetLocationType},
		{"GetHotspotAt", bridgeGetHotspotAt},
		{"GetWalkableAreaAt", bridgeGetWalkableAreaAt},
		{"GetPlayerCharacter", bridgeGetPlayerChar},
		{"SaveGameSlot", bridgeSaveGameSlot},
		{"RestoreGameSlot", bridgeRestoreGameSlot},
		{"NewRoom", bridgeNewRoom},
		{"QuitGame", bridgeQuitGame},
		{"GetGameOption", bridgeGetGameOption},
		{"SetGameOption", bridgeSetGameOption},
		{"GetGlobalInt", bridgeGetGlobalInt},
		{"SetGlobalInt", bridgeSetGlobalInt},
		{nullptr, nullptr}
	};

	lua_newtable(_L);
	luaL_register(_L, nullptr, agsFuncs);

	lua_newtable(_L);
	lua_pushstring(_L, "__index");
	lua_pushcclosure(_L, stubIndex, 0);
	lua_settable(_L, -3);
	lua_setmetatable(_L, -2);

	lua_setglobal(_L, "ags");

	registerCharacterMeta();
	registerObjectMeta();
	registerHotspotMeta();
	registerRegionMeta();
	registerGUIMeta();
	registerButtonMeta();
	registerLabelMeta();
	registerTextBoxMeta();
	registerSliderMeta();
	registerListBoxMeta();
	registerInventoryMeta();
	registerAudioChannelMeta();
}

void AGSLua::registerConstants() {
	lua_getglobal(_L, "ags");

	struct ConstEntry { const char *name; int value; };

	static const ConstEntry consts[] = {
		{"eModeInteract", 0}, {"eModeUse", 1}, {"eModeLook", 2},
		{"eModeTalk", 3}, {"eModePickUp", 4}, {"eModeCustom1", 5},
		{"eModeCustom2", 6}, {"eModeCustom3", 7}, {"eModeCustom4", 8},
		{"eKeyEscape", 27}, {"eKeyEnter", 13}, {"eKeySpace", 32},
		{"eKeyUp", 273}, {"eKeyDown", 274}, {"eKeyLeft", 275}, {"eKeyRight", 276},
		{"eKeyA", 65}, {"eKeyB", 66}, {"eKeyC", 67}, {"eKeyD", 68},
		{"eKeyE", 69}, {"eKeyF", 70}, {"eKeyG", 71}, {"eKeyH", 72},
		{"eKeyI", 73}, {"eKeyJ", 74}, {"eKeyK", 75}, {"eKeyL", 76},
		{"eKeyM", 77}, {"eKeyN", 78}, {"eKeyO", 79}, {"eKeyP", 80},
		{"eKeyQ", 81}, {"eKeyR", 82}, {"eKeyS", 83}, {"eKeyT", 84},
		{"eKeyU", 85}, {"eKeyV", 86}, {"eKeyW", 87}, {"eKeyX", 88},
		{"eKeyY", 89}, {"eKeyZ", 90},
		{"eEventWalkIntoScreen", 0}, {"eEventLookAt", 1}, {"eEventTalkTo", 2},
		{"eEventUseInv", 3}, {"eEventUse", 4}, {"eEventStandOn", 5},
		{"eEventAnyClick", 6},
		{"eLocationNothing", 0}, {"eLocationHotspot", 1}, {"eLocationObject", 2},
		{"eLocationCharacter", 3},
		{"eAudioPriorityVeryLow", 1}, {"eAudioPriorityLow", 2},
		{"eAudioPriorityNormal", 3}, {"eAudioPriorityHigh", 4},
		{"eAudioPriorityVeryHigh", 5},
		{"eSkipNone", 0}, {"eSkipKeyMouse", 1}, {"eSkipAnyKey", 2},
		{"eSkipMouse", 3},
		{nullptr, 0}
	};

	for (int i = 0; consts[i].name; i++) {
		lua_pushinteger(_L, consts[i].value);
		lua_setfield(_L, -2, consts[i].name);
	}

	lua_pop(_L, 1);
}

static void regSimpleMeta(lua_State *L, const char *name) {
	luaL_newmetatable(L, name);
	lua_pushcclosure(L, simpleStubIndex, 0);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void AGSLua::registerCharacterMeta() {
	luaL_newmetatable(_L, "agsCharacter");
	lua_pushcclosure(_L, charIndex, 0);
	lua_setfield(_L, -2, "__index");
	lua_pushcclosure(_L, charNewIndex, 0);
	lua_setfield(_L, -2, "__newindex");
	lua_pop(_L, 1);

	lua_getglobal(_L, "ags");
	lua_pushcclosure(_L, charGet, 0);
	lua_setfield(_L, -2, "Character");
	lua_pop(_L, 1);
}

void AGSLua::registerObjectMeta() {
	luaL_newmetatable(_L, "agsObject");
	lua_pushcclosure(_L, objIndex, 0);
	lua_setfield(_L, -2, "__index");
	lua_pushcclosure(_L, objNewIndex, 0);
	lua_setfield(_L, -2, "__newindex");
	lua_pop(_L, 1);

	lua_getglobal(_L, "ags");
	lua_pushcclosure(_L, objGet, 0);
	lua_setfield(_L, -2, "Object");
	lua_pop(_L, 1);
}

void AGSLua::registerHotspotMeta() {
	regSimpleMeta(_L, "agsHotspot");
	lua_getglobal(_L, "ags");
	lua_pushcclosure(_L, hotspotGet, 0);
	lua_setfield(_L, -2, "Hotspot");
	lua_pop(_L, 1);
}

void AGSLua::registerRegionMeta() {
	regSimpleMeta(_L, "agsRegion");
	lua_getglobal(_L, "ags");
	lua_pushcclosure(_L, regionGet, 0);
	lua_setfield(_L, -2, "Region");
	lua_pop(_L, 1);
}

void AGSLua::registerGUIMeta() {
	regSimpleMeta(_L, "agsGUI");
	lua_getglobal(_L, "ags");
	lua_pushcclosure(_L, guiGet, 0);
	lua_setfield(_L, -2, "GUI");
	lua_pop(_L, 1);
}

void AGSLua::registerButtonMeta() {
	regSimpleMeta(_L, "agsButton");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsButton");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "Button");
	lua_pop(_L, 1);
}

void AGSLua::registerLabelMeta() {
	regSimpleMeta(_L, "agsLabel");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsLabel");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "Label");
	lua_pop(_L, 1);
}

void AGSLua::registerTextBoxMeta() {
	regSimpleMeta(_L, "agsTextBox");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsTextBox");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "TextBox");
	lua_pop(_L, 1);
}

void AGSLua::registerSliderMeta() {
	regSimpleMeta(_L, "agsSlider");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsSlider");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "Slider");
	lua_pop(_L, 1);
}

void AGSLua::registerListBoxMeta() {
	regSimpleMeta(_L, "agsListBox");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsListBox");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "ListBox");
	lua_pop(_L, 1);
}

void AGSLua::registerInventoryMeta() {
	regSimpleMeta(_L, "agsInventory");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsInventory");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "InventoryItem");
	lua_pop(_L, 1);
}

void AGSLua::registerAudioChannelMeta() {
	regSimpleMeta(_L, "agsAudioChannel");
	lua_getglobal(_L, "ags");
	lua_pushstring(_L, "agsAudioChannel");
	lua_pushcclosure(_L, stubGetterNoArg, 1);
	lua_setfield(_L, -2, "AudioChannel");
	lua_pop(_L, 1);
}

// --- AGS→Lua API ---

void AGSLua::Lua_RunScript(ScriptMethodParams &params) {
	PARAMS1(const char *, name);
	if (!_L || !_loaded) return;

	if (!_scripts.contains(name)) {
		warning("ags_lua: script '%s' not found", name);
		return;
	}

	ScriptEntry &entry = _scripts[name];
	if (luaL_loadbuffer(_L, (const char *)entry.bytecode, entry.size, name)) {
		warning("ags_lua: error loading '%s': %s", name, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	if (lua_pcall(_L, 0, 0, 0)) {
		warning("ags_lua: error running '%s': %s", name, lua_tostring(_L, -1));
		lua_pop(_L, 1);
	}
}

void AGSLua::Lua_RequireModule(ScriptMethodParams &params) {
	PARAMS1(const char *, name);
	if (!_L || !_loaded) return;

	Common::String scriptName;
	if (_scripts.contains(name)) {
		scriptName = name;
	} else if (_modules.contains(name)) {
		scriptName = _modules[name];
	} else {
		warning("ags_lua: module '%s' not found", name);
		return;
	}

	lua_getglobal(_L, "package");
	if (!lua_istable(_L, -1)) { lua_pop(_L, 1); return; }
	lua_getfield(_L, -1, "loaded");
	if (!lua_istable(_L, -1)) { lua_pop(_L, 2); return; }
	lua_getfield(_L, -1, name);
	if (!lua_isnil(_L, -1)) {
		lua_pop(_L, 3);
		return;
	}
	lua_pop(_L, 2);

	ScriptEntry &entry = _scripts[scriptName];
	if (luaL_loadbuffer(_L, (const char *)entry.bytecode, entry.size, name)) {
		warning("ags_lua: error loading module '%s': %s", name, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	if (lua_pcall(_L, 0, 1, 0)) {
		warning("ags_lua: error running module '%s': %s", name, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	lua_getglobal(_L, "package");
	lua_getfield(_L, -1, "loaded");
	lua_pushvalue(_L, -3);
	lua_setfield(_L, -2, name);
	lua_pop(_L, 3);
}

void AGSLua::Lua_SetVar(ScriptMethodParams &params) {
	PARAMS2(const char *, name, int, value);
	if (!_L) return;
	lua_pushinteger(_L, value);
	lua_setglobal(_L, name);
}

void AGSLua::Lua_GetVar(ScriptMethodParams &params) {
	PARAMS1(const char *, name);
	if (!_L) return;
	lua_getglobal(_L, name);
	params._result = (int)lua_tointeger(_L, -1);
	lua_pop(_L, 1);
}

void AGSLua::Lua_Evaluate(ScriptMethodParams &params) {
	PARAMS1(const char *, expr);
	if (!_L) return;

	if (luaL_loadstring(_L, expr)) {
		warning("ags_lua: error compiling '%s': %s", expr, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	if (lua_pcall(_L, 0, 1, 0)) {
		warning("ags_lua: error evaluating '%s': %s", expr, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	params._result = (int)lua_tointeger(_L, -1);
	lua_pop(_L, 1);
}

void AGSLua::Lua_Call(ScriptMethodParams &params) {
	if (!_L) return;

	const char *funcName = (const char *)params[0];
	int numExtra = params.size() - 1;

	lua_getglobal(_L, funcName);
	if (!lua_isfunction(_L, -1)) {
		warning("ags_lua: function '%s' not found", funcName);
		lua_pop(_L, 1);
		return;
	}

	for (int i = 0; i < numExtra; i++) {
		intptr_t v = params[i + 1];
		if (_managedTables.contains(v)) {
			LuaManagedTable *mt = _managedTables[v];
			lua_rawgeti(_L, LUA_REGISTRYINDEX, mt->ref);
		} else {
			lua_pushinteger(_L, (int)v);
		}
	}

	if (lua_pcall(_L, numExtra, 1, 0)) {
		warning("ags_lua: error calling '%s': %s", funcName, lua_tostring(_L, -1));
		lua_pop(_L, 1);
		return;
	}

	params._result = (int)lua_tointeger(_L, -1);
	lua_pop(_L, 1);
}

void AGSLua::Lua_CreateTable(ScriptMethodParams &params) {
	if (!_L) return;

	lua_newtable(_L);
	int numArgs = params.size() / 2;
	for (int i = 0; i < numArgs; i++) {
		const char *key = (const char *)params[i * 2];
		int val = (int)params[i * 2 + 1];
		lua_pushstring(_L, key ? key : "");
		lua_pushinteger(_L, val);
		lua_settable(_L, -3);
	}

	params._result = refManagedTable();
}

void AGSLua::Lua_LuaValueList(ScriptMethodParams &params) {
	if (!_L) return;

	lua_newtable(_L);
	for (size_t i = 0; i < params.size(); i++) {
		lua_pushinteger(_L, (int)params[i]);
		lua_rawseti(_L, -2, (int)i + 1);
	}

	params._result = refManagedTable();
}

// --- Save / Load ---

int64 AGSLua::AGS_EngineOnEvent(int event, NumberPtr data) {
	long fileHandle = (long)(intptr_t)data;

	if (event == AGSE_RESTOREGAME) {
		byte buf[4];
		_engine->FRead(buf, 4, fileHandle);
		uint32 saveVersion = READ_LE_UINT32(buf);
		if (saveVersion != SaveMagic)
			return 0;

		if (_L) {
			clearManagedTables(true);
			lua_close(_L);
			_L = nullptr;
		}
		_L = luaL_newstate();
		luaL_openlibs(_L);
		lua_pushlightuserdata(_L, this);
		lua_setfield(_L, LUA_REGISTRYINDEX, AGS_LUA_REGISTRY);
		registerBridgeFunctions();
		registerConstants();

		_engine->FRead(buf, 4, fileHandle);
		int32 dataSize = (int32)READ_LE_UINT32(buf);
		if (dataSize > 0) {
			byte *ldata = (byte *)malloc(dataSize);
			_engine->FRead(ldata, dataSize, fileHandle);
			Common::MemoryReadStream rs(ldata, dataSize, DisposeAfterUse::YES);
			lua_pushlightuserdata(_L, &rs);
			lua_pushcclosure(_L, unpersistLuaClosure, 1);
			if (lua_pcall(_L, 0, 0, 0) != 0) {
				warning("ags_lua: failed to restore Lua state: %s", lua_tostring(_L, -1));
				lua_pop(_L, 1);
			}
		}
	} else if (event == AGSE_SAVEGAME) {
		byte buf[4];
		WRITE_LE_UINT32(buf, SaveMagic);
		_engine->FWrite(buf, 4, fileHandle);

		Common::MemoryWriteStreamDynamic ws(DisposeAfterUse::YES);
		if (_L) {
			lua_pushlightuserdata(_L, &ws);
			lua_pushcclosure(_L, persistLuaClosure, 1);
			if (lua_pcall(_L, 0, 0, 0) != 0) {
				warning("ags_lua: failed to persist Lua state: %s", lua_tostring(_L, -1));
				lua_pop(_L, 1);
			}
		}

		int32 dataSize = (int32)ws.size();
		WRITE_LE_UINT32(buf, dataSize);
		_engine->FWrite(buf, 4, fileHandle);
		if (dataSize > 0)
			_engine->FWrite(ws.getData(), dataSize, fileHandle);
	}
	return 0;
}

} // namespace AGSLua
} // namespace Plugins
} // namespace AGS3
