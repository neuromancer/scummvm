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

#include "audio/softsynth/pcspk.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/system.h"
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

	_scene = createScene(kSceneMainMenu, this);
	if (_scene)
		_scene->init();

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
