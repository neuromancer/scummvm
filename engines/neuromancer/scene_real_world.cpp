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

#include "neuromancer/scene_real_world.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/resource.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/str.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// PIC frames are always stored as 152 packed bytes × 112 rows (= 304 × 112
// pixels at 4bpp). Reuromancer relies on a static ImhHeader sitting next to
// the pixel buffer: { dx=0, dy=0, width=152, height=112 } (data.c:92-96).
enum {
	kPicPackedW = 152,
	kPicHeight  = 112,
	kPicBytes   = kPicPackedW * kPicHeight
};

} // anonymous namespace

RealWorldScene::RealWorldScene(NeuromancerEngine *engine)
	: Scene(engine), _next(kSceneRealWorld) {}

RealWorldScene::~RealWorldScene() = default;

void RealWorldScene::init() {
	ResourceManager *res = _engine->resources();

	// NEURO.IMH is a multi-record IMH; the first record is the 320x200 UI
	// frame. SpriteChain reads the header from the start of the buffer so
	// subsequent records are ignored for now.
	_neuroImh.resize(64000);
	uint32 neuroSize = res->load("NEURO.IMH", _neuroImh.data());
	debugC(1, kDebugResource, "RealWorldScene: NEURO.IMH -> %u bytes", neuroSize);

	// Build an in-memory IMH sprite for the PIC: [ImhHeader][pixels].
	_picSprite.resize(sizeof(ImhHeader) + kPicBytes);
	memset(_picSprite.data(), 0, _picSprite.size());
	WRITE_LE_UINT16(_picSprite.data() + 0, 0);          // dx
	WRITE_LE_UINT16(_picSprite.data() + 2, 0);          // dy
	WRITE_LE_UINT16(_picSprite.data() + 4, kPicPackedW);
	WRITE_LE_UINT16(_picSprite.data() + 6, kPicHeight);

	// Load R{level+1}.PIC directly into the pixel region.
	Common::String picName = Common::String::format("R%d.PIC", (int)_engine->currentLevel() + 1);
	uint32 picSize = res->load(picName, _picSprite.data() + sizeof(ImhHeader));
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", picName.c_str(), picSize);

	// Composite: NEURO.IMH at layer 10 (background), PIC at layer 9 (level
	// view, offset by (8, 8) to fit inside the UI frame).
	SpriteChain *chain = _engine->spriteChain();
	chain->addSprite(kLayerBackground, 0, 0, _neuroImh.data(), true);
	chain->addSprite(kLayerLevelBg,    8, 8, _picSprite.data(), true);
}

void RealWorldScene::deinit() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerBackground);
	chain->clearSprite(kLayerLevelBg);
}

SceneId RealWorldScene::update() {
	_engine->render();
	return _next;
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return;
	switch (event.kbd.keycode) {
	case Common::KEYCODE_ESCAPE:
		_next = kSceneMainMenu;
		break;
	case Common::KEYCODE_q:
		_engine->requestQuit();
		break;
	default:
		break;
	}
}

} // End of namespace Neuromancer
