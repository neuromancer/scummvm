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

#include "ags/plugins/ags_ccs/ags_ccs.h"
#include "ags/engine/ac/global_character.h"
#include "ags/engine/ac/global_game.h"

namespace AGS3 {
namespace Plugins {
namespace AGSCcs {

const uint32 Magic = 0xC0C00000;
const uint32 Version = 1;
const uint32 SaveMagic = Magic + Version;

AGSCcs::AGSCcs() : PluginBase() {
	_blankView = -1;
	_numChars = 0;
	_debug = false;
}

const char *AGSCcs::AGS_GetPluginName() {
	return "Character Control System [CCS] plugin recreation";
}

void AGSCcs::AGS_EngineStartup(IAGSEngine *engine) {
	PluginBase::AGS_EngineStartup(engine);

	if (_engine->version < 13)
		_engine->AbortGame("Engine interface is too old, need newer version of AGS.");

	SCRIPT_METHOD(ccCreateCommand^2, AGSCcs::ccCreateCommand);
	SCRIPT_METHOD(ccAppendCommand^2, AGSCcs::ccAppendCommand);
	SCRIPT_METHOD(ccExecuteCommand^2, AGSCcs::ccExecuteCommand);
	SCRIPT_METHOD(ccStopExecution^1, AGSCcs::ccStopExecution);
	SCRIPT_METHOD(ccIsExecuting^1, AGSCcs::ccIsExecuting);
	SCRIPT_METHOD(ccGetCommandID^1, AGSCcs::ccGetCommandID);
	SCRIPT_METHOD(ccGetControlID^1, AGSCcs::ccGetControlID);
	SCRIPT_METHOD(ccGetCharacterRoom^1, AGSCcs::ccGetCharacterRoom);
	SCRIPT_METHOD(ccSetBlankView^1, AGSCcs::ccSetBlankView);
	SCRIPT_METHOD(ccDebug^1, AGSCcs::ccDebug);
	SCRIPT_METHOD(ccPauseExecution^1, AGSCcs::ccPauseExecution);
	SCRIPT_METHOD(ccResumeExecution^1, AGSCcs::ccResumeExecution);
	SCRIPT_METHOD(ccIsPaused^1, AGSCcs::ccIsPaused);

	_engine->RequestEventHook(AGSE_PREGUIDRAW);
	_engine->RequestEventHook(AGSE_SAVEGAME);
	_engine->RequestEventHook(AGSE_RESTOREGAME);
}

int64 AGSCcs::AGS_EngineOnEvent(int event, NumberPtr data) {
	if (event == AGSE_PREGUIDRAW) {
		for (int i = 0; i < MAX_CHARS; i++) {
			if (_charStates[i].executing && !_charStates[i].paused)
				stepExecution(i);
		}
	} else if (event == AGSE_RESTOREGAME) {
		Serializer s(_engine, data, true);
		syncGame(s);
	} else if (event == AGSE_SAVEGAME) {
		Serializer s(_engine, data, false);
		syncGame(s);
	}
	return 0;
}

int AGSCcs::allocCharState(int charId) {
	for (int i = 0; i < MAX_CHARS; i++) {
		if (!_charStates[i].executing) {
			_charStates[i].charId = charId;
			_charStates[i].commandId = -1;
			_charStates[i].ctrlCmdIdx = 0;
			_charStates[i].waitCounter = 0;
			_charStates[i].pauseCount = 0;
			_charStates[i].executing = false;
			_charStates[i].paused = false;
			if (AGSCharacter *ch = _engine->GetCharacter(charId))
				_charStates[i].room = ch->room;
			else
				_charStates[i].room = 0;

			if (charId >= (int)_charPrevRoom.size())
				_charPrevRoom.resize(charId + 1);
			_charPrevRoom[charId] = _charStates[i].room;

			return i;
		}
	}
	return -1;
}

int AGSCcs::findCharState(int charId) {
	for (int i = 0; i < MAX_CHARS; i++) {
		if (_charStates[i].executing && _charStates[i].charId == charId)
			return i;
	}
	return -1;
}

void AGSCcs::freeCharState(int idx) {
	if (idx >= 0 && idx < MAX_CHARS) {
		_charStates[idx].executing = false;
		_charStates[idx].paused = false;
		_charStates[idx].charId = -1;
	}
}

void AGSCcs::parseCommand(int cmdId) {
	if (cmdId < 0 || cmdId >= MAX_COMMANDS)
		return;

	_commands[cmdId].ctrls.clear();
	const char *s = _commands[cmdId].full.c_str();

	while (*s) {
		while (*s == ' ' || *s == '\t')
			s++;

		if (!*s)
			break;

		const char *semi = strchr(s, ';');
		if (!semi)
			break;

		Common::String seg(s, semi - s);
		s = semi + 1;

		seg.trim();
		if (seg.empty())
			continue;

		CcsCtrlCmd cmd;

		int colon = seg.findFirstOf(':');
		Common::String name = (colon > 0) ? seg.substr(0, colon) : seg;
		name.toUppercase();

		if (name == "MOVE") {
			cmd.type = kMove;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				sscanf(params.c_str(), "%d,%d", &cmd.params[0], &cmd.params[1]);
			}
		} else if (name == "ANIMATE") {
			cmd.type = kAnimate;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				sscanf(params.c_str(), "%d,%d,%d,%d", &cmd.params[0], &cmd.params[1], &cmd.params[2], &cmd.params[3]);
			}
		} else if (name == "VIEW") {
			cmd.type = kView;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				sscanf(params.c_str(), "%d", &cmd.params[0]);
			}
		} else if (name == "ROOM") {
			cmd.type = kRoom;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				sscanf(params.c_str(), "%d,%d,%d", &cmd.params[0], &cmd.params[1], &cmd.params[2]);
			}
		} else if (name == "WAIT") {
			cmd.type = kWait;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				sscanf(params.c_str(), "%d", &cmd.params[0]);
			}
		} else if (name == "IF") {
			cmd.type = kIf;
			if (colon > 0) {
				Common::String expr = seg.substr(colon + 1);
				int globalIdx = 0, val = 0;
				char op[8] = {0};
				sscanf(expr.c_str(), "Global(%d)%7s%d", &globalIdx, op, &val);
				cmd.params[0] = globalIdx;
				cmd.params[1] = val;
				if (!strcmp(op, "==")) cmd.params[2] = 0;
				else if (!strcmp(op, "!=")) cmd.params[2] = 1;
				else if (!strcmp(op, ">")) cmd.params[2] = 2;
				else if (!strcmp(op, "<")) cmd.params[2] = 3;
				else if (!strcmp(op, ">=")) cmd.params[2] = 4;
				else if (!strcmp(op, "<=")) cmd.params[2] = 5;
				else cmd.params[2] = 0;
			}
		} else if (name == "SET") {
			cmd.type = kSet;
			if (colon > 0) {
				Common::String expr = seg.substr(colon + 1);
				int globalIdx = 0, val = 0;
				sscanf(expr.c_str(), "Global(%d)=%d", &globalIdx, &val);
				cmd.params[0] = globalIdx;
				cmd.params[1] = val;
			}
		} else if (name == "GOTO") {
			cmd.type = kGoto;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				int tgt = 0;
				sscanf(params.c_str(), "%d", &tgt);
				cmd.targetCmdId = tgt;
				cmd.params[0] = tgt - 1;
			}
		} else if (name == "EXECUTE") {
			cmd.type = kExecute;
			if (colon > 0) {
				Common::String params = seg.substr(colon + 1);
				int tgt = 0;
				sscanf(params.c_str(), "%d", &tgt);
				cmd.targetCmdId = tgt;
			}
		}

		cmd.raw = seg;
		_commands[cmdId].ctrls.push_back(cmd);
	}
}

void AGSCcs::stepExecution(int idx) {
	CharState &cs = _charStates[idx];
	if (!cs.executing || cs.paused)
		return;

	if (cs.commandId < 0 || cs.commandId >= MAX_COMMANDS) {
		freeCharState(idx);
		return;
	}

	const Common::Array<CcsCtrlCmd> &ctrls = _commands[cs.commandId].ctrls;
	if (cs.ctrlCmdIdx < 0 || cs.ctrlCmdIdx >= (int)ctrls.size()) {
		freeCharState(idx);
		return;
	}

	if (cs.waitCounter > 0) {
		cs.waitCounter--;
		return;
	}

	const CcsCtrlCmd &cmd = ctrls[cs.ctrlCmdIdx];

	switch (cmd.type) {
	case kMove:
		stepMove(idx, cmd);
		cs.ctrlCmdIdx++;
		break;
	case kAnimate:
		stepAnimate(idx, cmd);
		cs.ctrlCmdIdx++;
		break;
	case kView:
		stepView(idx, cmd);
		cs.ctrlCmdIdx++;
		break;
	case kRoom:
		stepRoom(idx, cmd);
		cs.ctrlCmdIdx++;
		break;
	case kWait:
		stepWait(idx, cmd);
		if (cs.waitCounter > 0)
			return;
		cs.ctrlCmdIdx++;
		break;
	case kIf:
		stepIf(idx, cmd);
		cs.ctrlCmdIdx++;
		break;
	case kSet:
		stepSet(cmd);
		cs.ctrlCmdIdx++;
		break;
	case kGoto:
		stepGoto(idx, cmd);
		break;
	case kExecute:
		stepExecute(idx, cmd);
		break;
	case kNone:
	default:
		cs.ctrlCmdIdx++;
		break;
	}

	if (_debug) {
		_engine->PrintDebugConsole(Common::String::format("CCS: char %d step %d/%d",
			cs.charId, cs.ctrlCmdIdx, (int)ctrls.size()).c_str());
	}
}

void AGSCcs::stepMove(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	AGS3::MoveCharacterDirect(cs.charId, cmd.params[0], cmd.params[1]);
	AGSCharacter *ch = _engine->GetCharacter(cs.charId);
	if (ch) {
		cs.room = ch->room;
		if (_debug)
			_engine->PrintDebugConsole(Common::String::format("CCS: MOVE char %d to %d,%d (room %d)",
				cs.charId, cmd.params[0], cmd.params[1], cs.room).c_str());
	}
}

void AGSCcs::stepAnimate(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	AGS3::SetCharacterView(cs.charId, cmd.params[0]);
	AGS3::AnimateCharacter4(cs.charId, cmd.params[1], cmd.params[2], cmd.params[3]);
}

void AGSCcs::stepView(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	AGS3::SetCharacterView(cs.charId, cmd.params[0]);
}

void AGSCcs::stepRoom(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	AGSCharacter *ch = _engine->GetCharacter(cs.charId);
	if (ch) {
		cs.room = cmd.params[0];
		ch->room = cmd.params[0];
		ch->x = cmd.params[1];
		ch->y = cmd.params[2];
	}
}

void AGSCcs::stepWait(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	cs.waitCounter = cmd.params[0];
}

void AGSCcs::stepIf(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	int val = AGS3::GetGlobalInt(cmd.params[0]);
	bool conditionMet = false;
	switch (cmd.params[2]) {
	case 0: conditionMet = (val == cmd.params[1]); break;
	case 1: conditionMet = (val != cmd.params[1]); break;
	case 2: conditionMet = (val > cmd.params[1]);  break;
	case 3: conditionMet = (val < cmd.params[1]);  break;
	case 4: conditionMet = (val >= cmd.params[1]); break;
	case 5: conditionMet = (val <= cmd.params[1]); break;
	default: conditionMet = false; break;
	}

	if (!conditionMet) {
		cs.ctrlCmdIdx++;
		if (cs.ctrlCmdIdx < (int)_commands[cs.commandId].ctrls.size()) {
			cs.ctrlCmdIdx++;
		}
	}
}

void AGSCcs::stepSet(const CcsCtrlCmd &cmd) {
	AGS3::SetGlobalInt(cmd.params[0], cmd.params[1]);
}

void AGSCcs::stepGoto(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	if (cmd.params[0] >= 0 && cmd.params[0] < (int)_commands[cs.commandId].ctrls.size()) {
		cs.ctrlCmdIdx = cmd.params[0];
	} else {
		cs.ctrlCmdIdx++;
	}
}

void AGSCcs::stepExecute(int idx, const CcsCtrlCmd &cmd) {
	CharState &cs = _charStates[idx];
	if (cmd.targetCmdId >= 0 && cmd.targetCmdId < MAX_COMMANDS) {
		cs.commandId = cmd.targetCmdId;
		cs.ctrlCmdIdx = 0;
	}
}

void AGSCcs::syncGame(Serializer &s) {
	uint32 sv = SaveMagic;
	s.syncAsInt(sv);
	if (s.isLoading() && sv != SaveMagic) {
		s.unreadInt();
		return;
	}

	s.syncAsInt(_blankView);
	s.syncAsInt(_numChars);

	// Commands are re-sent by the game script on restore, so
	// only save/load the per-character execution state below.

	for (int i = 0; i < MAX_CHARS; i++) {
		CharState &cs = _charStates[i];
		s.syncAsInt(cs.charId);
		s.syncAsInt(cs.commandId);
		s.syncAsInt(cs.ctrlCmdIdx);
		s.syncAsInt(cs.waitCounter);
		s.syncAsInt(cs.pauseCount);
		s.syncAsBool(cs.executing);
		s.syncAsBool(cs.paused);
		s.syncAsInt(cs.room);
	}

	if (s.isLoading()) {
		for (int i = 0; i < MAX_COMMANDS; i++) {
			if (!_commands[i].full.empty())
				parseCommand(i);
		}
		int maxId = 0;
		for (int i = 0; i < MAX_CHARS; i++) {
			if (_charStates[i].charId > maxId)
				maxId = _charStates[i].charId;
		}
		_charPrevRoom.resize(maxId + 1);
		for (int i = 0; i < MAX_CHARS; i++) {
			if (_charStates[i].executing && _charStates[i].charId >= 0) {
				_charPrevRoom[_charStates[i].charId] = _charStates[i].room;
			}
		}
	}
}

void AGSCcs::ccCreateCommand(ScriptMethodParams &params) {
	PARAMS2(int, cmdId, const char *, cmdStr);
	if (cmdId < 0 || cmdId >= MAX_COMMANDS)
		return;
	_commands[cmdId].full = cmdStr;
	parseCommand(cmdId);
}

void AGSCcs::ccAppendCommand(ScriptMethodParams &params) {
	PARAMS2(int, cmdId, const char *, cmdStr);
	if (cmdId < 0 || cmdId >= MAX_COMMANDS)
		return;
	_commands[cmdId].full += cmdStr;
	parseCommand(cmdId);
}

void AGSCcs::ccExecuteCommand(ScriptMethodParams &params) {
	PARAMS2(int, charId, int, cmdId);
	if (charId < 0 || cmdId < 0 || cmdId >= MAX_COMMANDS)
		return;

	int idx = findCharState(charId);
	if (idx < 0)
		idx = allocCharState(charId);

	if (idx < 0)
		return;

	_charStates[idx].charId = charId;
	_charStates[idx].commandId = cmdId;
	_charStates[idx].ctrlCmdIdx = 0;
	_charStates[idx].waitCounter = 0;
	_charStates[idx].executing = true;

	AGSCharacter *ch = _engine->GetCharacter(charId);
	if (ch) {
		_charStates[idx].room = ch->room;
		if (charId < (int)_charPrevRoom.size())
			_charPrevRoom[charId] = ch->room;
	}
}

void AGSCcs::ccStopExecution(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	if (charId < 0)
		return;

	AGS3::StopMoving(charId);

	int idx = findCharState(charId);
	freeCharState(idx);
}

void AGSCcs::ccIsExecuting(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	params._result = (idx >= 0 && _charStates[idx].executing) ? 1 : 0;
}

void AGSCcs::ccGetCommandID(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	params._result = (idx >= 0) ? _charStates[idx].commandId : -1;
}

void AGSCcs::ccGetControlID(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	params._result = (idx >= 0) ? (_charStates[idx].ctrlCmdIdx + 1) : -1;
}

void AGSCcs::ccGetCharacterRoom(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	if (charId >= 0 && charId < (int)_charPrevRoom.size()) {
		params._result = _charPrevRoom[charId];
	} else {
		AGSCharacter *ch = _engine->GetCharacter(charId);
		params._result = ch ? ch->room : 0;
	}
}

void AGSCcs::ccSetBlankView(ScriptMethodParams &params) {
	PARAMS1(int, viewNum);
	_blankView = viewNum;
}

void AGSCcs::ccDebug(ScriptMethodParams &params) {
	PARAMS1(int, val);
	_debug = (val != 0);
}

void AGSCcs::ccPauseExecution(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	if (idx >= 0) {
		_charStates[idx].paused = true;
		_charStates[idx].pauseCount++;
	}
}

void AGSCcs::ccResumeExecution(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	if (idx >= 0 && _charStates[idx].pauseCount > 0) {
		_charStates[idx].pauseCount--;
		if (_charStates[idx].pauseCount <= 0) {
			_charStates[idx].paused = false;
			_charStates[idx].pauseCount = 0;
		}
	}
}

void AGSCcs::ccIsPaused(ScriptMethodParams &params) {
	PARAMS1(int, charId);
	int idx = findCharState(charId);
	params._result = (idx >= 0 && _charStates[idx].paused) ? 1 : 0;
}

} // namespace AGSCcs
} // namespace Plugins
} // namespace AGS3
