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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#include "neuromancer/neuromancer.h"

#include "neuromancer/detection.h"
#include "neuromancer/gfx.h"
#include "neuromancer/level_handlers.h"
#include "neuromancer/music_player.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/resource.h"
#include "neuromancer/scene.h"
#include "neuromancer/scene_real_world.h"

#include "audio/softsynth/pcspk.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/translation.h"
#include "engines/util.h"
#include "graphics/cursorman.h"

namespace Neuromancer {

NeuromancerEngine::NeuromancerEngine(OSystem *syst, const ADGameDescription *gd)
	: Engine(syst),
	  _gameDescription(gd),
	  _rnd("neuromancer"),
	  _resources(nullptr),
	  _vm(nullptr),
	  _levelHandlers(nullptr),
	  _spriteChain(nullptr),
	  _speaker(nullptr),
	  _musicPlayer(nullptr),
	  _scene(nullptr),
	  _lastMusicTickMs(0),
	  _mouseX(kScreenWidth / 2),
	  _mouseY(kScreenHeight / 2),
	  _currentLevel(0),
	  _exitGame(false) {
	memset(_visitedLevels, 0, sizeof(_visitedLevels));
}

bool NeuromancerEngine::isLevelVisited(uint8 level) const {
	if (level >= 64) return true;
	uint8 mask = (uint8)(0x80 >> (level & 7));
	return (_visitedLevels[level >> 3] & mask) != 0;
}

void NeuromancerEngine::markLevelVisited(uint8 level) {
	if (level >= 64) return;
	uint8 mask = (uint8)(0x80 >> (level & 7));
	_visitedLevels[level >> 3] |= mask;
}

NeuromancerEngine::~NeuromancerEngine() {
	delete _scene;
	delete _spriteChain;
	delete _vm;
	delete _levelHandlers;
	delete _resources;
	delete _musicPlayer;
	if (_speaker) {
		_speaker->quit();
		delete _speaker;
	}
}

void NeuromancerEngine::render() {
	// Place the cursor on its layer at the current mouse position. The
	// first IMH record inside CURSORS.IMH is the default pointer.
	if (!_cursorsImh.empty())
		_spriteChain->addSprite(kLayerCursor, _mouseX, _mouseY, _cursorsImh.data(), false);
	_spriteChain->renderToScreen();
}

// ---------------------------------------------------------------------------
// Save / load
// ---------------------------------------------------------------------------

static const uint8 kSaveVersion = 1;

bool NeuromancerEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	// Only allow saving while the real-world scene is up -- that's where
	// all the persistable state lives. The main menu / title screen have
	// nothing meaningful to save.
	if (!_scene || _scene->id() != kSceneRealWorld) {
		if (msg) *msg = _("Saving is only available during gameplay.");
		return false;
	}
	return true;
}

bool NeuromancerEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	// Loading from the title screen is supported too -- we swap to the
	// real-world scene below on load.
	return true;
}

Common::Error NeuromancerEngine::saveGameStream(Common::WriteStream *stream,
                                                bool isAutosave) {
	stream->writeByte(kSaveVersion);
	Common::Serializer ser(nullptr, stream);
	ser.setVersion(kSaveVersion);
	return syncGame(ser);
}

Common::Error NeuromancerEngine::loadGameStream(Common::SeekableReadStream *stream) {
	uint8 version = stream->readByte();
	if (version != kSaveVersion) {
		warning("Neuromancer: unsupported save version %u (expected %u)",
		        version, kSaveVersion);
		return Common::kReadingFailed;
	}
	Common::Serializer ser(stream, nullptr);
	ser.setVersion(version);
	return syncGame(ser);
}

Common::Error NeuromancerEngine::syncGame(Common::Serializer &s) {
	// Core engine state: current level + visited-levels bitset.
	s.syncAsByte(_currentLevel);
	s.syncBytes(_visitedLevels, sizeof(_visitedLevels));

	// On load we may be sitting at the main-menu scene. Switch to the
	// real-world scene BEFORE its syncGame runs so the destination scene
	// exists. loadLevel happens later inside reinitializeAfterLoad.
	if (s.isLoading()) {
		if (!_scene || _scene->id() != kSceneRealWorld) {
			if (_scene) {
				_scene->deinit();
				delete _scene;
			}
			_scene = createScene(kSceneRealWorld, this);
			if (_scene)
				_scene->init();
		}
	}

	// Scene state (player pose, cash, inventory, ...).
	RealWorldScene *rw = dynamic_cast<RealWorldScene *>(_scene);
	if (!rw) {
		warning("Neuromancer: save/load without a real-world scene");
		return Common::kReadingFailed;
	}
	rw->syncGame(s);

	// VM state (64 KB DSEG + threads + dialog control).
	if (_vm) _vm->syncGame(s);

	// After load: rebuild visuals. loadLevel re-loads the level's PIC +
	// BIH; the scene then reinstates the saved player pose on top.
	if (s.isLoading() && rw) {
		rw->reinitializeAfterLoad();
	}

	return Common::kNoError;
}

Common::Error NeuromancerEngine::run() {
	// 320x200 paletted, matching the DOS original.
	initGraphics(kScreenWidth, kScreenHeight);
	setEgaPalette();

	// Hide the system cursor; we composite our own via the sprite chain.
	CursorMan.showMouse(false);

	_resources = new ResourceManager();
	if (!_resources->open())
		return Common::kNoGameDataFoundError;

	_levelHandlers = new LevelHandlers();
	_vm = new NeuroVM(this);
	_spriteChain = new SpriteChain();

	// Music: use ScummVM's Audio::PCSpeaker synth. The synth manages its
	// own mixer stream internally via init(); the MusicPlayer feeds it PIT
	// divisor values at ~541 Hz via playQueue().
	_speaker = new Audio::PCSpeaker();
	_speaker->init();
	_musicPlayer = new MusicPlayer(_speaker);
	_lastMusicTickMs = g_system->getMillis();

	// Load the cursor sprite sheet once; it stays resident for the whole
	// session. Only the first IMH record is used for now.
	_cursorsImh.resize(64000);
	uint32 cursorsSize = _resources->load("CURSORS.IMH", _cursorsImh.data());
	debugC(1, kDebugResource, "Neuromancer: CURSORS.IMH -> %u bytes", cursorsSize);

	// Load ROOMPOS.BIH once. All 58 level roompos tables live here; the
	// real-world scene indexes this data per-level to hit-test clicks.
	_roomposData.resize(4096);
	uint32 roomposSz = _resources->load("ROOMPOS.BIH", _roomposData.data());
	_roomposData.resize(roomposSz);
	debugC(1, kDebugResource, "Neuromancer: ROOMPOS.BIH -> %u bytes", roomposSz);

	// Load SPRITES.IMH once (the player character's walk-cycle sheet).
	// Individual frames are addressed by byte offset inside this buffer;
	// each frame starts with its own ImhHeader so SpriteChain::addSprite
	// can unpack it directly.
	_spritesImh.resize(64000);
	uint32 spritesSz = _resources->load("SPRITES.IMH", _spritesImh.data());
	_spritesImh.resize(spritesSz);
	debugC(1, kDebugResource, "Neuromancer: SPRITES.IMH -> %u bytes", spritesSz);

	// If the launcher passed a save slot to auto-load, skip straight to
	// the real-world scene and apply the saved state right after init().
	int pendingSlot = -1;
	if (ConfMan.hasKey("save_slot"))
		pendingSlot = ConfMan.getInt("save_slot");

	_scene = createScene(pendingSlot >= 0 ? kSceneRealWorld : kSceneMainMenu, this);
	if (_scene)
		_scene->init();

	if (pendingSlot >= 0) {
		Common::Error err = loadGameState(pendingSlot);
		if (err.getCode() != Common::kNoError)
			warning("Neuromancer: auto-load slot %d failed: %s",
			        pendingSlot, err.getDesc().c_str());
	}

	Common::Event event;
	while (!shouldQuit() && !_exitGame) {
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT ||
			    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				_exitGame = true;
				break;
			}
			if (event.type == Common::EVENT_MOUSEMOVE) {
				_mouseX = event.mouse.x;
				_mouseY = event.mouse.y;
			}
			if (_scene)
				_scene->handleEvent(event);
		}

		if (!_scene)
			break;

		SceneId next = _scene->update();
		if (next != _scene->id()) {
			_scene->deinit();
			delete _scene;
			_scene = createScene(next, this);
			if (!_scene) {
				debugC(1, kDebugGeneral, "Neuromancer: target scene %d unavailable, staying in menu", (int)next);
				_scene = createScene(kSceneMainMenu, this);
			}
			if (_scene)
				_scene->init();
		}

		// Pump music forward by the elapsed wall time since the last tick.
		// MusicPlayer tracks fractional sub-tick time internally so the
		// aggregate tempo stays accurate even at variable frame rates.
		{
			uint32 now = g_system->getMillis();
			uint32 dt = now - _lastMusicTickMs;
			if (_musicPlayer)
				_musicPlayer->tick(dt);
			_lastMusicTickMs = now;
		}

		g_system->delayMillis(16);
	}

	if (_scene)
		_scene->deinit();

	return Common::kNoError;
}

} // End of namespace Neuromancer
