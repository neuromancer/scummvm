/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#include "interspective/debugger.h"

#include "common/endian.h"
#include "common/rect.h"

#include "interspective/actor.h"
#include "interspective/eventmanager.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Common {
DECLARE_SINGLETON(Interspective::Debugger);
}

namespace Interspective {
//

Debugger::Debugger()
	: _stepOpcodes(false),
	  _breakOnClickHandler(false),
	  _vm(0) {
	registerCmd("setBackdrop", WRAP_METHOD(Debugger, cmd_setBackdrop));
	registerCmd("paintText", WRAP_METHOD(Debugger, cmd_paintText));
	registerCmd("listExits", WRAP_METHOD(Debugger, cmd_listExits));
	registerCmd("showClickable", WRAP_METHOD(Debugger, cmd_showClickable));
	registerCmd("paintSprite", WRAP_METHOD(Debugger, cmd_paintSprite));
	registerCmd("break", WRAP_METHOD(Debugger, cmd_break));
	registerCmd("step", WRAP_METHOD(Debugger, cmd_step));
	registerCmd("setVar", WRAP_METHOD(Debugger, cmd_setVar));
#define CMD(x) registerCmd(#x, WRAP_METHOD(Debugger, cmd_##x));
	CMD(debugActor);
	CMD(changeRoom);
	CMD(jumpTo);
#undef CMD
}

void Debugger::setEngine(Engine *vm) {
	_vm = vm;
	registerVar("currentRoom", reinterpret_cast<int *>(&(_vm->logic()->_currentRoom)));
}

Logic *Debugger::logic() const {
	return _vm->logic();
}

#define CMD(x) bool Debugger::cmd_##x(int argc, const char **argv)

CMD(debugActor) {
	if (argc == 2) {
		int actorNum = atoi(argv[1]);
		Log.getActor(actorNum)->toggleDebug();
		debugPrintf("Toggled debugging on actor %d. Remember to toggle proper levels, too!\n", actorNum);
	} else
		debugPrintf("Syntax: debugActor <id>\n");
	return true;
}

CMD(break) {
	if (argc == 2) {
		if (!strcmp(argv[1], "click")) {
			debugPrintf("Will break execution on click handler.\n");
			_breakOnClickHandler = true;
		}
	} else
		debugPrintf("Syntax: break <event>     (events are: click)\n");
	return true;
}

void Debugger::clickHandler() {
	if (_breakOnClickHandler) {
		attach();
		onFrame();
	}
}

CMD(step) {
	_stepOpcodes = true;
	//_detach_now = true;
	return false;
}

CMD(setVar) {
	if (argc == 4) {
		if (!strcmp(argv[1], "word")) {
			int var = atoi(argv[2]);
			int val = atoi(argv[3]);
			debugPrintf("word[%d] = %d\n", var, val);
			WRITE_LE_UINT16(_vm->resources()->getGlobalWordVariable(var), val);
		}
	} else
		debugPrintf("Syntax: break <event>     (events are: click)\n");
	return true;
}

CMD(showClickable) {
	EventManager::instance().toggleDebug();
	return true;
}

CMD(listExits) {
	debugPrintf("Room exits:\n");
	foreach_const(Exit *, logic()->room()->exits())
		debugPrintf("  %s\n", +(**it));
	debugPrintf("\n");
	return true;
}

bool Debugger::cmd_setBackdrop(int argc, const char **argv) {
	if (argc == 2) {
		_vm->graphics()->setBackdrop(atoi(argv[1]));
		_vm->graphics()->paintBackdrop();
	} else
		debugPrintf("Syntax: set_backdrop <index>\n");

	return true;
}

bool Debugger::cmd_paintText(int argc, const char **argv) {
	if (argc >= 2) {
		int left = 10;
		int top = 10;
		byte colour = 235;
		if (argc >= 4) {
			left = atoi(argv[2]);
			top = atoi(argv[3]);
			if (argc >= 5)
				colour = atoi(argv[4]);
		}
		_vm->graphics()->paintText(left, top, colour, const_cast<byte *>(reinterpret_cast<const byte *>(argv[1])));
	} else
		debugPrintf("Syntax: paint_text <text> [<left> <top> [<colour>]]\n");

	return true;
}

CMD(changeRoom) {
	// Teleport directly to room N. Bypasses script-driven scene
	// transitions — useful for skipping the intro to reach a room where
	// a bug reproduces. Detaches the debugger so the new room is loaded
	// on the next tick.
	if (argc != 2) {
		debugPrintf("Syntax: changeRoom <room_id>\n");
		debugPrintf("Current room: %u\n", logic()->currentRoom());
		return true;
	}
	const int room = atoi(argv[1]);
	debugPrintf("Changing room to %d (was %u). Detaching.\n", room, logic()->currentRoom());
	logic()->changeRoom(uint16(room));
	return false;
}

CMD(jumpTo) {
	// Run a main-interpreter procedure at the given offset. Combined with
	// `changeRoom`, lets the user resume from any cutscene/script entry
	// point. Useful when boot_param=N drops you in a room but the room's
	// expected setup script hasn't run.
	if (argc != 2) {
		debugPrintf("Syntax: jumpTo <main_offset_hex>\n");
		debugPrintf("(Run a main-interpreter procedure at the given offset)\n");
		return true;
	}
	uint16 off = (uint16)strtoul(argv[1], 0, 16);
	debugPrintf("Queuing main interpreter run at 0x%04x\n", off);
	Log.runLater(CodePointer(off, Log.mainInterpreter()));
	return false;
}

bool Debugger::cmd_paintSprite(int argc, const char **argv) {
	if (argc >= 2) {
		int sprite = atoi(argv[1]);
		int left = 10;
		int top = 10;
		if (argc >= 4) {
			left = atoi(argv[2]);
			top = atoi(argv[3]);
		}
		Sprite *s = _vm->resources()->loadSprite(sprite);
		_vm->graphics()->paint(s, Common::Point(left, top));
		_vm->graphics()->updateScreen();
	} else
		debugPrintf("Syntax: paintSprite <text> [<left> <top>]\n");

	return true;
}

} // End of namespace Interspective
