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
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/resource.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/str.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

enum {
	kPicPackedW = 152,
	kPicHeight  = 112,
	kPicBytes   = kPicPackedW * kPicHeight,

	// Scroll text box: 17 chars x 7 lines at (88, 134). Dimensions match
	// Reuromancer/NeuromancerWin64/window_animation.c:290-301 for NWM_NEURO_UI.
	// w=68 packed bytes = 136 px; h = b-t+1 = 58 px (we use 56 for 7 rows).
	kScrollX          = 88,
	kScrollY          = 134,
	kScrollWidthPx    = 136,
	kScrollHeightPx   = 56,
	kScrollPackedW    = kScrollWidthPx / 2,
	kScrollBytes      = kScrollPackedW * kScrollHeightPx,
	kScrollColumns    = kScrollWidthPx / 8,   // 17
	kScrollRows       = kScrollHeightPx / 8,  // 7

	// Dialog bubble (stub geometry; real version uses BUBBLES.IMH framing).
	kBubbleX          = 40,
	kBubbleY          = 24,
	kBubbleWidthPx    = 240,
	kBubbleHeightPx   = 48,
	kBubblePackedW    = kBubbleWidthPx / 2,
	kBubbleBytes      = kBubblePackedW * kBubbleHeightPx,
	kBubbleColumns    = kBubbleWidthPx / 8,   // 30
	kBubbleRows       = kBubbleHeightPx / 8,  // 6

	kIndicatorWidthPx  = 72,
	kIndicatorHeightPx = 12,
	kIndicatorPackedW  = kIndicatorWidthPx / 2,
	kIndicatorBytes    = kIndicatorPackedW * kIndicatorHeightPx,

	kMaxLevel = 57
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

RealWorldScene::RealWorldScene(NeuromancerEngine *engine)
	: Scene(engine), _next(kSceneRealWorld), _textVisible(false), _introPending(false) {}

RealWorldScene::~RealWorldScene() = default;

void RealWorldScene::init() {
	ResourceManager *res = _engine->resources();

	_neuroImh.resize(64000);
	uint32 neuroSize = res->load("NEURO.IMH", _neuroImh.data());
	debugC(1, kDebugResource, "RealWorldScene: NEURO.IMH -> %u bytes", neuroSize);

	_picSprite.resize(sizeof(ImhHeader) + kPicBytes);
	writeImhHeader(_picSprite.data(), 0, 0, kPicPackedW, kPicHeight);

	_scrollSprite.resize(sizeof(ImhHeader) + kScrollBytes);
	writeImhHeader(_scrollSprite.data(), 0, 0, kScrollPackedW, kScrollHeightPx);

	_bubbleSprite.resize(sizeof(ImhHeader) + kBubbleBytes);
	writeImhHeader(_bubbleSprite.data(), 0, 0, kBubblePackedW, kBubbleHeightPx);

	_indicatorSprite.resize(sizeof(ImhHeader) + kIndicatorBytes);
	writeImhHeader(_indicatorSprite.data(), 0, 0, kIndicatorPackedW, kIndicatorHeightPx);

	_bihData.resize(64000);

	SpriteChain *chain = _engine->spriteChain();
	chain->addSprite(kLayerBackground, 0, 0, _neuroImh.data(), true);

	loadLevel();
	showLevelIndicator();
}

void RealWorldScene::deinit() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerBackground);
	chain->clearSprite(kLayerLevelBg);
	chain->clearSprite(kLayerDialogBubble);
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerDebugOverlay);
}

SceneId RealWorldScene::update() {
	if (!_textVisible && !_introPending)
		advanceVmOnce();

	_engine->render();
	return _next;
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return;

	if (_introPending) {
		clearTextWidgets();
		_textVisible = false;
		_introPending = false;
		startVmForCurrentLevel();
		return;
	}

	if (_textVisible) {
		clearTextWidgets();
		_textVisible = false;
		_engine->vm()->resume();
		return;
	}

	switch (event.kbd.keycode) {
	case Common::KEYCODE_ESCAPE:
		_next = kSceneMainMenu;
		break;
	case Common::KEYCODE_q:
		_engine->requestQuit();
		break;
	case Common::KEYCODE_RIGHT:
	case Common::KEYCODE_PAGEDOWN:
	case Common::KEYCODE_SPACE:
		gotoLevel(+1);
		break;
	case Common::KEYCODE_LEFT:
	case Common::KEYCODE_PAGEUP:
	case Common::KEYCODE_BACKSPACE:
		gotoLevel(-1);
		break;
	default:
		break;
	}
}

void RealWorldScene::gotoLevel(int delta) {
	int next = (int)_engine->currentLevel() + delta;
	for (int attempts = 0; attempts <= kMaxLevel; attempts++) {
		if (next < 0)         next = kMaxLevel;
		if (next > kMaxLevel) next = 0;

		_engine->setCurrentLevel((uint8)next);
		if (loadLevel()) {
			showLevelIndicator();
			return;
		}
		next += (delta >= 0 ? +1 : -1);
	}
	warning("RealWorldScene: no playable level found in a full scan");
}

bool RealWorldScene::loadLevel() {
	int lvl = (int)_engine->currentLevel();
	ResourceManager *res = _engine->resources();

	Common::String picName = Common::String::format("R%d.PIC", lvl + 1);
	uint32 picSize = res->load(picName, _picSprite.data() + sizeof(ImhHeader));
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", picName.c_str(), picSize);
	if (picSize == 0)
		return false;

	_engine->spriteChain()->addSprite(kLayerLevelBg, 8, 8, _picSprite.data(), true);
	clearTextWidgets();
	_textVisible = false;
	_introPending = false;

	Common::String bihName = Common::String::format("R%d.BIH", lvl + 1);
	uint32 bihSize = res->load(bihName, _bihData.data());
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", bihName.c_str(), bihSize);

	NeuroVM *vm = _engine->vm();
	vm->resetThreads();
	if (bihSize > 0) {
		vm->attach(_bihData.data(), bihSize);
		showLevelIntro();
	}
	return true;
}

void RealWorldScene::showLevelIntro() {
	uint8 level = _engine->currentLevel();
	uint16 stringNum = _engine->isLevelVisited(level) ? 1 : 0;
	_engine->markLevelVisited(level);

	const char *s = _engine->vm()->bih().textString(stringNum);
	debugC(1, kDebugScript, "RealWorldScene: intro text[%u] = \"%s\"", stringNum, s);

	if (s && *s) {
		showText(s, kWidgetScroll);
		_introPending = true;
	} else {
		startVmForCurrentLevel();
	}
}

void RealWorldScene::startVmForCurrentLevel() {
	_engine->vm()->startDefaultThread(0, 0);
}

void RealWorldScene::showLevelIndicator() {
	byte *pixels = _indicatorSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0, kIndicatorBytes);

	Common::String label = Common::String::format("Level %d", (int)_engine->currentLevel() + 1);
	drawString(label.c_str(), kIndicatorWidthPx, kIndicatorHeightPx, 0, 2, pixels);

	_engine->spriteChain()->addSprite(kLayerDebugOverlay, 12, 124, _indicatorSprite.data(), false);
}

void RealWorldScene::clearTextWidgets() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerDialogBubble);
}

// Paint `text` into the requested widget's sprite buffer and register it
// with the SpriteChain. Background is black (0x00), text is white
// (drawString's default), so no XOR inversion is needed -- both widgets
// are rendered opaque to fully cover whatever sits behind them.
void RealWorldScene::showText(const char *text, TextWidget widget) {
	clearTextWidgets();

	int widthPx, heightPx, packedW, bytes, columns, posX, posY;
	SpriteLayerIndex layer;
	byte *spriteBuf;

	if (widget == kWidgetScroll) {
		widthPx   = kScrollWidthPx;
		heightPx  = kScrollHeightPx;
		packedW   = kScrollPackedW;
		bytes     = kScrollBytes;
		columns   = kScrollColumns;
		posX      = kScrollX;
		posY      = kScrollY;
		spriteBuf = _scrollSprite.data();
		layer     = kLayerNeuroMenu; // "lower" widget slot
	} else {
		widthPx   = kBubbleWidthPx;
		heightPx  = kBubbleHeightPx;
		packedW   = kBubblePackedW;
		bytes     = kBubbleBytes;
		columns   = kBubbleColumns;
		posX      = kBubbleX;
		posY      = kBubbleY;
		spriteBuf = _bubbleSprite.data();
		layer     = kLayerDialogBubble;
	}
	(void)packedW;

	byte *pixels = spriteBuf + sizeof(ImhHeader);
	memset(pixels, 0, bytes); // black fill

	Common::String wrapped = wrapText(text, columns);
	drawString(wrapped.c_str(), widthPx, heightPx, 0, 0, pixels);

	_engine->spriteChain()->addSprite(layer, posX, posY, spriteBuf, true);
	_textVisible = true;
}

void RealWorldScene::advanceVmOnce() {
	NeuroVM *vm = _engine->vm();
	NeuroVM::TickResult r = vm->tick();

	switch (r.action) {
	case NeuroVM::Action::kIdle:
		break;

	case NeuroVM::Action::kTextOutput: {
		const char *s = vm->bih().textString(r.stringNum);
		debugC(1, kDebugScript, "RealWorldScene: scroll text[%u] = \"%s\"", r.stringNum, s);
		showText(s, kWidgetScroll);
		break;
	}

	case NeuroVM::Action::kDialogReply: {
		const char *s = vm->bih().textString(r.stringNum);
		debugC(1, kDebugScript, "RealWorldScene: bubble text[%u] = \"%s\"", r.stringNum, s);
		showText(s, kWidgetBubble);
		break;
	}

	case NeuroVM::Action::kEnterDialog:
		debugC(1, kDebugScript, "RealWorldScene: enter dialog (stub: resume)");
		vm->resume();
		break;

	case NeuroVM::Action::kChangeLevel:
		debugC(1, kDebugLevel, "RealWorldScene: VM requested level %u", r.levelN);
		_engine->setCurrentLevel(r.levelN);
		loadLevel();
		showLevelIndicator();
		break;
	}
}

} // End of namespace Neuromancer
