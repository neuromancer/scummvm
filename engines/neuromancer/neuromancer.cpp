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
#include "neuromancer/level_handlers.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/resource.h"

#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

namespace Neuromancer {

NeuromancerEngine::NeuromancerEngine(OSystem *syst, const ADGameDescription *gd)
	: Engine(syst),
	  _gameDescription(gd),
	  _rnd("neuromancer"),
	  _resources(nullptr),
	  _vm(nullptr),
	  _levelHandlers(nullptr),
	  _currentLevel(0),
	  _exitGame(false) {}

NeuromancerEngine::~NeuromancerEngine() {
	delete _vm;
	delete _levelHandlers;
	delete _resources;
}

Common::Error NeuromancerEngine::run() {
	// 320x200 paletted -- the DOS original's native resolution.
	initGraphics(320, 200);

	_resources = new ResourceManager();
	if (!_resources->open())
		return Common::kNoGameDataFoundError;

	_levelHandlers = new LevelHandlers();
	_vm = new NeuroVM(this);

	// Smoke test: round-trip a known IMH and a known BIH through the
	// resource manager + decompressors, logging sizes. Confirms the
	// offset tables, Huffman tree, and RLE/XOR passes are functional
	// end-to-end before scene code is in place.
	{
		Common::Array<byte> buf;
		buf.resize(64000);
		uint32 imhSize = _resources->load("TITLE.IMH", buf.data());
		uint32 bihSize = _resources->load("R1.BIH",   buf.data());
		debugC(1, kDebugResource, "Neuromancer: smoke test TITLE.IMH -> %u bytes, R1.BIH -> %u bytes",
		       imhSize, bihSize);
	}

	// Black palette to start. Scene code will load the real palette
	// from resources once the main menu is ported.
	byte palette[256 * 3];
	memset(palette, 0, sizeof(palette));
	g_system->getPaletteManager()->setPalette(palette, 0, 256);

	// Main loop skeleton. Scene-specific update / render / input handling
	// will replace the empty body as those subsystems are ported.
	Common::Event event;
	while (!shouldQuit() && !_exitGame) {
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT ||
			    event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				_exitGame = true;
		}

		_vm->tick();
		g_system->updateScreen();
		g_system->delayMillis(16);
	}

	return Common::kNoError;
}

} // End of namespace Neuromancer
