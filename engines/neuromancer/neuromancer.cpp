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

#include "neuromancer/decompress.h"
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

// The DOS original runs in 16-colour EGA, which the Reuromancer port hard-codes
// as an RGBA table (drawing_control.c). ScummVM wants RGB triplets.
static const byte kEgaPalette[16 * 3] = {
	0x00, 0x00, 0x00,   // 0  black
	0x00, 0x00, 0xAA,   // 1  blue
	0x00, 0xAA, 0x00,   // 2  green
	0x00, 0xAA, 0xAA,   // 3  cyan
	0xAA, 0x00, 0x00,   // 4  red
	0xAA, 0x00, 0xAA,   // 5  magenta
	0xAA, 0x55, 0x00,   // 6  brown
	0xAA, 0xAA, 0xAA,   // 7  light gray
	0x55, 0x55, 0x55,   // 8  dark gray
	0x55, 0x55, 0xFF,   // 9  light blue
	0x00, 0xFF, 0x55,   // 10 light green
	0x55, 0xFF, 0xFF,   // 11 light cyan
	0xFF, 0x55, 0x55,   // 12 light red
	0xFF, 0x55, 0xFF,   // 13 light magenta
	0xFF, 0xFF, 0x55,   // 14 yellow
	0xFF, 0xFF, 0xFF,   // 15 white
};

// Unpack a single 4bpp IMH frame into an 8bpp buffer.
//
// IMH layout after decompression is: [ImhHeader (8 bytes)][packed pixels].
// `width` in the header is *packed* (bytes per row -- two pixels per byte).
// High nibble is the left pixel, low nibble is the right pixel.
// Copies into `dst` at offset (hdr.dx, hdr.dy), clipped to (dstW, dstH).
static void blitImh4bpp(const byte *imh, byte *dst, int dstW, int dstH) {
	uint16 dx     = READ_LE_UINT16(imh + 0);
	uint16 dy     = READ_LE_UINT16(imh + 2);
	uint16 packedW = READ_LE_UINT16(imh + 4);
	uint16 height  = READ_LE_UINT16(imh + 6);
	const byte *pix = imh + sizeof(ImhHeader);

	int outW = (int)packedW * 2;
	for (int y = 0; y < height; y++) {
		int dy2 = (int)dy + y;
		if (dy2 < 0 || dy2 >= dstH)
			continue;
		byte *row = dst + dy2 * dstW;
		const byte *src = pix + y * packedW;
		for (int b = 0; b < packedW; b++) {
			int x = (int)dx + b * 2;
			byte v = src[b];
			if (x >= 0 && x < dstW)
				row[x] = v >> 4;
			if (x + 1 >= 0 && x + 1 < dstW)
				row[x + 1] = v & 0x0F;
		}
	}
	(void)outW;
}

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
	// 320x200 paletted, matching the DOS original.
	initGraphics(320, 200);

	_resources = new ResourceManager();
	if (!_resources->open())
		return Common::kNoGameDataFoundError;

	_levelHandlers = new LevelHandlers();
	_vm = new NeuroVM(this);

	// 16-colour EGA palette, placed at indices 0..15. Remaining entries stay
	// black -- the game only uses 0..15 in the pixel stream.
	byte palette[256 * 3];
	memset(palette, 0, sizeof(palette));
	memcpy(palette, kEgaPalette, sizeof(kEgaPalette));
	g_system->getPaletteManager()->setPalette(palette, 0, 256);

	// Show the title screen. Equivalent to scene_main_menu.c:init() loading
	// TITLE.IMH into g_seg010.background and adding it to the sprite chain.
	{
		Common::Array<byte> imh;
		imh.resize(64000);
		uint32 sz = _resources->load("TITLE.IMH", imh.data());
		debugC(1, kDebugResource, "Neuromancer: TITLE.IMH decompressed to %u bytes", sz);

		Common::Array<byte> screen;
		screen.resize(320 * 200);
		if (sz >= sizeof(ImhHeader))
			blitImh4bpp(imh.data(), screen.data(), 320, 200);

		g_system->copyRectToScreen(screen.data(), 320, 0, 0, 320, 200);
		g_system->updateScreen();
	}

	// Wait for the player to dismiss the title. Keypress, mouse click,
	// or quit event all break the loop.
	Common::Event event;
	while (!shouldQuit() && !_exitGame) {
		while (g_system->getEventManager()->pollEvent(event)) {
			switch (event.type) {
			case Common::EVENT_QUIT:
			case Common::EVENT_RETURN_TO_LAUNCHER:
			case Common::EVENT_KEYDOWN:
			case Common::EVENT_LBUTTONDOWN:
			case Common::EVENT_RBUTTONDOWN:
				_exitGame = true;
				break;
			default:
				break;
			}
		}

		_vm->tick();
		g_system->updateScreen();
		g_system->delayMillis(16);
	}

	return Common::kNoError;
}

} // End of namespace Neuromancer
