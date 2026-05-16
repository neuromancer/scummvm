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

#include "interspective/innocent.h"

#include "audio/mididrv.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/error.h"
#include "common/file.h"
#include "common/serializer.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/events.h"
#include "engines/util.h"

#include "interspective/debug.h"
#include "interspective/debugger.h"
#include "interspective/eventmanager.h"
#include "interspective/graphics.h"
#include "interspective/logic.h"
#include "interspective/musicparser.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/sound.h"

using namespace Common;

namespace Interspective {

Engine *Engine::me;

Engine::Engine(OSystem *syst) :
		::Engine(syst) {
	_resources = &Res;
	_resources->setEngine(this);
	_graphics = &Graphics::instance();
	_graphics->setEngine(this);
	_logic = &Logic::instance();
	_logic->setEngine(this);
	_sound = new Sound(this);
	_copyProtection = false;
	me = this;
	_lastTicks = 0;
	_escapeHeld = false;
	_dosMusicEnabled = 0;
	_dosSfxEnabled = 0;

	// Debug channels are now registered via the MetaEngine
	// (InterspectiveMetaEngineDetection::getDebugChannels) so that
	// --debugflags=name works. The legacy addDebugChannel() calls here
	// were silently ignored, leaving "Engine does not support debug
	// level 'script'" warnings whenever the flags were used.
	_rnd = new Common::RandomSource("interspective");
}

Engine::~Engine() {
	MusicParser::destroy();
	delete _sound;
	delete _rnd;
}

bool Engine::hasFeature(EngineFeature f) const {
	return f == kSupportsLoadingDuringRuntime || f == kSupportsSavingDuringRuntime;
}

bool Engine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _logic != nullptr && _resources != nullptr && _resources->mainDat() != nullptr;
}

bool Engine::canSaveGameStateCurrently(Common::U32String *msg) {
	return _logic != nullptr && _resources != nullptr && _resources->mainDat() != nullptr;
}

Common::Error Engine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	enum {
		kSaveVersion = 3
	};

	if (!stream || !_logic || !_resources || !_resources->mainDat())
		return Common::kWritingFailed;

	Common::Serializer s(nullptr, stream);
	if (!s.matchBytes("IUCS", 4))
		return Common::kWritingFailed;
	s.syncVersion(kSaveVersion);

	MainDat *main = _resources->mainDat();
	uint16 mainSize = main->dataSize();
	s.syncAsUint16LE(mainSize);
	s.syncBytes(main->_data, mainSize);

	uint16 blockId = _logic->currentBlock();
	uint16 blockSize = _logic->blockProgram() ? _logic->blockProgram()->codeSize() : 0;
	s.syncAsUint16LE(blockId);
	s.syncAsUint16LE(blockSize);
	if (blockSize != 0)
		s.syncBytes(_logic->blockProgram()->base(), blockSize);

	_logic->synchronize(s);
	return s.err() ? Common::kWritingFailed : Common::kNoError;
}

Common::Error Engine::loadGameStream(Common::SeekableReadStream *stream) {
	enum {
		kSaveVersion = 3
	};

	if (!stream || !_logic || !_resources || !_resources->mainDat())
		return Common::kReadingFailed;

	Common::Serializer s(stream, nullptr);
	if (!s.matchBytes("IUCS", 4))
		return Common::kReadingFailed;
	if (!s.syncVersion(kSaveVersion))
		return Common::kReadingFailed;

	MainDat *main = _resources->mainDat();
	uint16 mainSize = 0;
	s.syncAsUint16LE(mainSize);
	if (mainSize != main->dataSize())
		return Common::kReadingFailed;
	s.syncBytes(main->_data, mainSize);

	uint16 blockId = 0xffff;
	uint16 blockSize = 0;
	s.syncAsUint16LE(blockId);
	s.syncAsUint16LE(blockSize);
	Common::Array<byte> blockData;
	if (blockSize != 0) {
		blockData.resize(blockSize);
		s.syncBytes(&blockData[0], blockSize);
	}

	_logic->synchronize(s);

	if (blockSize != 0) {
		Program *block = _logic->blockProgram();
		if (!block || block->codeSize() != blockSize || _logic->currentBlock() != blockId)
			return Common::kReadingFailed;
		memcpy(block->base(), &blockData[0], blockSize);
	}

	// LoadGame_ReadFromDisk @ 1000:82fd clears the type-5 fullscreen
	// graphic slot (DS:0x677b) after a successful restore.
	_logic->setGraphicSlot(4, 0);

	return s.err() ? Common::kReadingFailed : Common::kNoError;
}

Common::Error Engine::run() {
	initGraphics(320, 200);

	_copyProtection = ConfMan.getBool("copy_protection");
	_startRoom = ConfMan.getInt("boot_param");
	_debugger = &Debug;
	Debug.setEngine(this);
	initDosSoundConfig();
	_resources->init();
	_graphics->init();
	// music is initialized in the singleton
	_logic->init();

	_resources->loadActors();
	_logic->initCode();
	_graphics->hideCursor();
	while(!shouldQuit()) {
		handleEvents();
		_logic->tick();
		_logic->callAnimations();
		_logic->runPostAnimationScripts();
		_graphics->paint();
		_graphics->syncCursorVisibility();
//		_graphics->paintAnimations();
		_graphics->updateScreen();
		_debugger->onFrame();
		delay(40);
	}

	return kNoError;
}

bool Engine::consumeEscapePress(const Common::Event &event) const {
	if (event.type == Common::EVENT_KEYUP && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
		_escapeHeld = false;
		return false;
	}
	if (event.type != Common::EVENT_KEYDOWN || event.kbd.keycode != Common::KEYCODE_ESCAPE)
		return false;
	if (event.kbdRepeat || _escapeHeld)
		return false;
	_escapeHeld = true;
	return true;
}

void Engine::handleEvents() {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		switch(event.type) {

		case Common::EVENT_KEYDOWN:
			if (consumeEscapePress(event) && _logic)
				_logic->requestSkipCutscene();
			break;

		case Common::EVENT_KEYUP:
			consumeEscapePress(event);
			if (event.kbd.keycode == Common::KEYCODE_BACKQUOTE)
				_debugger->attach();
			break;

		case Common::EVENT_MOUSEMOVE:
		case Common::EVENT_LBUTTONDOWN:
		case Common::EVENT_LBUTTONUP:
		case Common::EVENT_RBUTTONDOWN:
		case Common::EVENT_RBUTTONUP:
			_graphics->setCursorPosition(event.mouse);
			if (_logic && (event.type == Common::EVENT_RBUTTONDOWN || event.type == Common::EVENT_RBUTTONUP))
				_logic->setSpeechSkipInput(event.type == Common::EVENT_RBUTTONDOWN);
			if (_logic && event.type == Common::EVENT_RBUTTONDOWN)
				_logic->cycleCursorModeByRightClickLikeDos();
			if (event.type == Common::EVENT_LBUTTONDOWN)
				EventManager::instance().clicked(event.mouse);
			break;

		default:
			break;
		}
	}
}

bool Engine::escapePressed() const {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		switch(event.type) {

		case Common::EVENT_KEYDOWN:
			if (consumeEscapePress(event))
				return true;
			break;

		case Common::EVENT_KEYUP:
			consumeEscapePress(event);
			if (event.kbd.keycode == Common::KEYCODE_BACKQUOTE)
				_debugger->attach();
			break;
		default:
			break;
		}
	}
	return false;
}

void Engine::initDosSoundConfig() {
	_dosMusicEnabled = 0;
	_dosSfxEnabled = 0;

	Common::File config;
	if (!config.open(Common::Path("innocent.ini"))) {
		debugC(1, kDebugLevelMusic, "DOS sound config: innocent.ini missing; music/sfx mask defaults to 0");
		return;
	}

	const int32 size = config.size();
	if (size <= 0)
		return;

	byte *data = new byte[size];
	config.read(data, size);
	parseDosSoundSwitchString(data, size);
	delete[] data;

	debugC(1, kDebugLevelMusic, "DOS sound config: music=%u sfx=%u mask=0x%02x",
		_dosMusicEnabled, _dosSfxEnabled, dosSoundDeviceMask());
}

void Engine::parseDosSoundSwitchString(const byte *data, uint32 length) {
	// Mirrors ParseSwitchString @ 1000:1492 for the two CS sound bytes
	// consumed by Op_12. Mouse, joystick, XMS, and port switches do not
	// affect the sound-device mask and are intentionally ignored here.
	uint32 pos = 0;
	while (pos < length) {
		while (pos < length && data[pos] != '/')
			++pos;
		if (pos >= length)
			return;
		++pos;
		if (pos >= length)
			return;

		const byte option = data[pos++];
		bool enabled = true;
		if (pos < length && data[pos] == '-') {
			enabled = false;
			++pos;
		}

		switch (option) {
		case 'r':
		case 'R':
			if (enabled) {
				_dosMusicEnabled = 4;
				if (_dosSfxEnabled == 0)
					_dosSfxEnabled = 4;
			} else {
				_dosMusicEnabled = 0;
				_dosSfxEnabled = 0;
			}
			break;
		case 'a':
		case 'A':
			if (enabled) {
				_dosMusicEnabled = 1;
				_dosSfxEnabled = 0;
			} else {
				_dosMusicEnabled = 0;
				_dosSfxEnabled = 0;
			}
			break;
		case 'b':
		case 'B':
			if (enabled) {
				_dosSfxEnabled = 2;
				if (_dosMusicEnabled == 0)
					_dosMusicEnabled = 1;
			} else {
				_dosMusicEnabled = 0;
				_dosSfxEnabled = 0;
			}
			break;
		default:
			break;
		}
	}
}

uint16 Engine::getRandom(uint16 max) const {
	return _rnd->getRandomNumber(max);
}

void Engine::delay(int millis) const {
	int target = _lastTicks + millis;
	while ((_lastTicks = _system->getMillis()) < target) {
		_system->delayMillis(target - _lastTicks);
	}
}

} // End of namespace Interspective
