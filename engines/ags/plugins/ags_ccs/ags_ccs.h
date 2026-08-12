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

#ifndef AGS_PLUGINS_AGS_CCS_AGS_CCS_H
#define AGS_PLUGINS_AGS_CCS_AGS_CCS_H

#include "ags/plugins/plugin_base.h"
#include "ags/plugins/serializer.h"
#include "common/array.h"
#include "common/str.h"

namespace AGS3 {
namespace Plugins {
namespace AGSCcs {

enum CcsCtrlType {
	kMove,
	kAnimate,
	kView,
	kRoom,
	kWait,
	kIf,
	kSet,
	kGoto,
	kExecute,
	kNone
};

struct CcsCtrlCmd {
	CcsCtrlType type;
	int params[6];
	int targetCmdId;
	Common::String raw;

	CcsCtrlCmd() : type(kNone), targetCmdId(-1) {
		for (int i = 0; i < 6; i++)
			params[i] = 0;
	}
};

class AGSCcs : public PluginBase {
	SCRIPT_HASH(AGSCcs)
private:
	static const int MAX_COMMANDS = 32;
	static const int MAX_CHARS = 30;

	struct CharState {
		int charId;
		int commandId;
		int ctrlCmdIdx;
		int waitCounter;
		int pauseCount;
		bool executing;
		bool paused;
		int room;

		CharState() : charId(-1), commandId(-1), ctrlCmdIdx(0), waitCounter(0),
			pauseCount(0), executing(false), paused(false), room(0) {}
	};

	struct CcsCommand {
		Common::String full;
		Common::Array<CcsCtrlCmd> ctrls;
	};

	CcsCommand _commands[MAX_COMMANDS];
	CharState _charStates[MAX_CHARS];
	int _blankView;
	int _numChars;
	bool _debug;
	Common::Array<int> _charPrevRoom;

	int allocCharState(int charId);
	int findCharState(int charId);
	void freeCharState(int idx);
	void parseCommand(int cmdId);
	void stepExecution(int idx);
	void stepMove(int idx, const CcsCtrlCmd &cmd);
	void stepAnimate(int idx, const CcsCtrlCmd &cmd);
	void stepView(int idx, const CcsCtrlCmd &cmd);
	void stepRoom(int idx, const CcsCtrlCmd &cmd);
	void stepWait(int idx, const CcsCtrlCmd &cmd);
	void stepIf(int idx, const CcsCtrlCmd &cmd);
	void stepSet(const CcsCtrlCmd &cmd);
	void stepGoto(int idx, const CcsCtrlCmd &cmd);
	void stepExecute(int idx, const CcsCtrlCmd &cmd);
	void syncGame(Serializer &s);

	void ccCreateCommand(ScriptMethodParams &params);
	void ccAppendCommand(ScriptMethodParams &params);
	void ccExecuteCommand(ScriptMethodParams &params);
	void ccStopExecution(ScriptMethodParams &params);
	void ccIsExecuting(ScriptMethodParams &params);
	void ccGetCommandID(ScriptMethodParams &params);
	void ccGetControlID(ScriptMethodParams &params);
	void ccGetCharacterRoom(ScriptMethodParams &params);
	void ccSetBlankView(ScriptMethodParams &params);
	void ccDebug(ScriptMethodParams &params);
	void ccPauseExecution(ScriptMethodParams &params);
	void ccResumeExecution(ScriptMethodParams &params);
	void ccIsPaused(ScriptMethodParams &params);

public:
	AGSCcs();
	virtual ~AGSCcs() {}

	const char *AGS_GetPluginName() override;
	void AGS_EngineStartup(IAGSEngine *engine) override;
	int64 AGS_EngineOnEvent(int event, NumberPtr data) override;
	int AGS_PluginV2() const override { return 1; };
};

} // namespace AGSCcs
} // namespace Plugins
} // namespace AGS3

#endif
