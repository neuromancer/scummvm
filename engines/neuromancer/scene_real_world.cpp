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

// PIC: 152 packed bytes × 112 rows.
enum {
	kPicPackedW = 152,
	kPicHeight  = 112,
	kPicBytes   = kPicPackedW * kPicHeight,

	// Text panel: same footprint as the PIC (304 px wide × 112 px tall).
	// 304 / 8 = 38 characters per line, which matches the DOS layout.
	kTextPanelWidthPx  = 304,
	kTextPanelHeightPx = 112,
	kTextPanelPackedW  = kTextPanelWidthPx / 2,
	kTextPanelBytes    = kTextPanelPackedW * kTextPanelHeightPx,
	kTextPanelColumns  = kTextPanelWidthPx / 8,

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

	_textPanelSprite.resize(sizeof(ImhHeader) + kTextPanelBytes);
	writeImhHeader(_textPanelSprite.data(), 0, 0, kTextPanelPackedW, kTextPanelHeightPx);

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
}

SceneId RealWorldScene::update() {
	// Three-state drive loop:
	//   1. Intro pending -> the level-intro text is on screen. Wait for a
	//      keypress to dismiss; handleEvent() starts the VM afterwards.
	//   2. Text visible (VM yielded a blocking action) -> wait for input.
	//   3. Otherwise -> tick the VM.
	if (!_textVisible && !_introPending)
		advanceVmOnce();

	_engine->render();
	return _next;
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return;

	// Pre-VM intro: first keypress dismisses it and starts the VM.
	if (_introPending) {
		clearTextPanel();
		_textVisible = false;
		_introPending = false;
		startVmForCurrentLevel();
		return;
	}

	// Mid-VM blocking text: dismiss and let the VM continue.
	if (_textVisible) {
		clearTextPanel();
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

	// Background image.
	Common::String picName = Common::String::format("R%d.PIC", lvl + 1);
	uint32 picSize = res->load(picName, _picSprite.data() + sizeof(ImhHeader));
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", picName.c_str(), picSize);
	if (picSize == 0)
		return false;

	_engine->spriteChain()->addSprite(kLayerLevelBg, 8, 8, _picSprite.data(), true);
	clearTextPanel();
	_textVisible = false;
	_introPending = false;

	// BIH script.
	Common::String bihName = Common::String::format("R%d.BIH", lvl + 1);
	uint32 bihSize = res->load(bihName, _bihData.data());
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", bihName.c_str(), bihSize);

	// Attach the BIH to the VM but do NOT start threads yet. The DOS
	// engine runs setup_intro() before the VM takes over; we mirror that
	// by showing the level intro string first and only starting threads
	// once the user dismisses it.
	NeuroVM *vm = _engine->vm();
	vm->resetThreads();
	if (bihSize > 0) {
		vm->attach(_bihData.data(), bihSize); // size = the real decompressed size
		showLevelIntro();
	}
	return true;
}

// Display the pre-VM intro text. First-visit shows text[0] (the long
// intro); subsequent visits show text[1] (short intro), matching the
// visited-levels bitstring logic in setup_intro() (scene_real_world.c:315).
void RealWorldScene::showLevelIntro() {
	uint8 level = _engine->currentLevel();
	uint16 stringNum = _engine->isLevelVisited(level) ? 1 : 0;
	_engine->markLevelVisited(level);

	const char *s = _engine->vm()->bih().textString(stringNum);
	debugC(1, kDebugScript, "RealWorldScene: intro text[%u] = \"%s\"", stringNum, s);

	if (s && *s) {
		renderTextPanel(s);
		_introPending = true;
	} else {
		// No intro for this level -- go straight to the VM.
		startVmForCurrentLevel();
	}
}

// Start the VM for the already-attached BIH. Called once the user dismisses
// the pre-VM intro text. Equivalent to the DOS engine transitioning from
// RWS_TEXT_OUTPUT to RWS_NORMAL after setup_intro() finishes.
void RealWorldScene::startVmForCurrentLevel() {
	_engine->vm()->startDefaultThread(0, 0);
}

void RealWorldScene::showLevelIndicator() {
	byte *pixels = _indicatorSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0, kIndicatorBytes);

	Common::String label = Common::String::format("Level %d", (int)_engine->currentLevel() + 1);
	drawString(label.c_str(), kIndicatorWidthPx, kIndicatorHeightPx, 0, 2, pixels);

	_engine->spriteChain()->addSprite(kLayerDialogBubble, 12, 124, _indicatorSprite.data(), false);
}

void RealWorldScene::clearTextPanel() {
	_engine->spriteChain()->clearSprite(kLayerDialogBubble);
}

void RealWorldScene::renderTextPanel(const char *rawText) {
	byte *pixels = _textPanelSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0xFF, kTextPanelBytes); // white fill -- text will XOR to black

	Common::String wrapped = wrapText(rawText, kTextPanelColumns);

	// drawString writes colour-15 (white) packed bytes; XOR against the
	// white background to produce black text on a white panel, matching
	// the DOS neuro_window style.
	Common::Array<byte> scratch;
	scratch.resize(kTextPanelBytes);
	memset(scratch.data(), 0, scratch.size());
	drawString(wrapped.c_str(), kTextPanelWidthPx, kTextPanelHeightPx, 4, 4, scratch.data());

	for (uint32 i = 0; i < kTextPanelBytes; i++)
		pixels[i] ^= scratch[i];

	// Re-use the dialog bubble layer as a general overlay for now.
	_engine->spriteChain()->addSprite(kLayerDialogBubble, 8, 8, _textPanelSprite.data(), true);
	_textVisible = true;
}

void RealWorldScene::advanceVmOnce() {
	NeuroVM *vm = _engine->vm();
	NeuroVM::TickResult r = vm->tick();

	switch (r.action) {
	case NeuroVM::Action::kIdle:
		break;

	case NeuroVM::Action::kTextOutput:
	case NeuroVM::Action::kDialogReply: {
		const char *s = vm->bih().textString(r.stringNum);
		debugC(1, kDebugScript, "RealWorldScene: text[%u] = \"%s\"", r.stringNum, s);
		renderTextPanel(s);
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
