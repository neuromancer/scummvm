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

	// Scroll widget geometry. The DOS source stores l/w in packed-byte
	// coordinates (window_animation.c:290-297) -- pixel X = l*2, pixel
	// width = w*2. Height = b - t + 1.
	kScrollX          = 176,        // DOS l=88 (packed) * 2
	kScrollY          = 134,        // DOS t=134 (pixels)
	kScrollWidthPx    = 136,        // DOS w=68 (packed) * 2
	kScrollHeightPx   = 58,         // DOS b-t+1 = 191-134+1
	kScrollPackedW    = kScrollWidthPx / 2,
	kScrollBytes      = kScrollPackedW * kScrollHeightPx,
	kScrollColumns    = kScrollWidthPx / 8,   // 17
	kScrollRows       = 7,                    // DOS max_lines

	// Dialog bubble: full-width bordered frame at top=4. DOS formula
	// (neuro_window_control.c:96) is bottom = lines*8 + 19, so height =
	// lines*8 + 16 for the drawable region. We provision for up to 8
	// lines statically; unused rows get drawn as border-only.
	kBubbleX          = 0,
	kBubbleY          = 4,
	kBubbleWidthPx    = 320,
	kBubbleHeightPx   = 8 * 8 + 16, // 80 px (room for 8 wrapped lines + borders)
	kBubblePackedW    = kBubbleWidthPx / 2,
	kBubbleBytes      = kBubblePackedW * kBubbleHeightPx,
	kBubbleInnerLeft  = 8,          // inner text padding
	kBubbleInnerTop   = 8,
	kBubbleInnerWidth = kBubbleWidthPx  - 2 * kBubbleInnerLeft,
	kBubbleColumns    = kBubbleInnerWidth / 8,  // 38
	kBubbleRows       = 8,

	// Status widget at (96, 149): 8 chars wide, 1 row tall.
	// Matches the DOS build's ui_panel_update() which writes 8-char
	// formatted strings ("$    0", "   00:00", "11/16/58", etc.)
	// directly into NEURO.IMH at that offset.
	kStatusX        = 96,
	kStatusY        = 149,
	kStatusWidthPx  = 64,
	kStatusHeightPx = 8,
	kStatusPackedW  = kStatusWidthPx / 2,
	kStatusBytes    = kStatusPackedW * kStatusHeightPx,

	kMaxLevel = 57
};

// Clickable button footprint. Matches neuro_button_t in the DOS build.
struct UiButtonRect {
	int16 left, top, right, bottom;
	int   code;
	char  label;
};

// Transcribed verbatim from data.c:127-138. Coordinates are absolute pixel
// positions on the 320x200 screen. The icons themselves are painted into
// NEURO.IMH, so we only need click/keyboard dispatch here.
static const UiButtonRect kUiButtons[] = {
	{ 0x10, 0x93, 0x23, 0xA5, 0x00, 'i' }, // inventory
	{ 0x28, 0x93, 0x3B, 0xA5, 0x01, 'p' }, // pax
	{ 0x40, 0x93, 0x53, 0xA5, 0x02, 't' }, // dialog / talk
	{ 0x10, 0xAB, 0x23, 0xBD, 0x03, 's' }, // skills
	{ 0x28, 0xAB, 0x3B, 0xBD, 0x04, 'r' }, // ROM / chip
	{ 0x40, 0xAB, 0x53, 0xBD, 0x05, 'd' }, // disk options
	{ 0x70, 0xA8, 0x7D, 0xB2, 0x0A, '1' }, // date
	{ 0x80, 0xA8, 0x8F, 0xB2, 0x0B, '2' }, // time
	{ 0x70, 0xB3, 0x7D, 0xBB, 0x0C, '3' }, // cash
	{ 0x80, 0xB3, 0x8F, 0xBB, 0x0D, '4' }, // constitution
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

RealWorldScene::RealWorldScene(NeuromancerEngine *engine)
	: Scene(engine),
	  _next(kSceneRealWorld),
	  _textVisible(false),
	  _introPending(false),
	  _statusMode(kStatusDate),
	  _cash(0),
	  _constitution(2000),
	  _timeH(0),
	  _timeM(0),
	  _dateDay(0) {}

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

	_statusSprite.resize(sizeof(ImhHeader) + kStatusBytes);
	writeImhHeader(_statusSprite.data(), 0, 0, kStatusPackedW, kStatusHeightPx);

	_bihData.resize(64000);

	SpriteChain *chain = _engine->spriteChain();
	chain->addSprite(kLayerBackground, 0, 0, _neuroImh.data(), true);

	loadLevel();
	updateStatusWidget();
}

void RealWorldScene::deinit() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerBackground);
	chain->clearSprite(kLayerLevelBg);
	chain->clearSprite(kLayerDialogBubble);
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerCharacter);
}

SceneId RealWorldScene::update() {
	if (!_textVisible && !_introPending)
		advanceVmOnce();

	_engine->render();
	return _next;
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_KEYDOWN) {
		// Dismiss on any key when a blocking text widget is up.
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

		// UI keyboard shortcuts (i/p/t/s/r/d/1/2/3/4).
		int uiAction = keyToUiAction(event.kbd.ascii);
		if (uiAction >= 0) {
			onUiAction(uiAction);
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
		return;
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		// A click also dismisses pending text.
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
		int uiAction = hitTestUiButton(event.mouse.x, event.mouse.y);
		if (uiAction >= 0)
			onUiAction(uiAction);
	}
}

int RealWorldScene::keyToUiAction(uint16 ascii) const {
	char lower = (char)tolower((byte)(ascii & 0x7F));
	for (uint i = 0; i < sizeof(kUiButtons) / sizeof(kUiButtons[0]); i++) {
		char label = (char)tolower((byte)kUiButtons[i].label);
		if (label == lower)
			return kUiButtons[i].code;
	}
	return -1;
}

int RealWorldScene::hitTestUiButton(int x, int y) const {
	for (uint i = 0; i < sizeof(kUiButtons) / sizeof(kUiButtons[0]); i++) {
		const UiButtonRect &b = kUiButtons[i];
		if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom)
			return b.code;
	}
	return -1;
}

void RealWorldScene::onUiAction(int code) {
	switch (code) {
	case kUiDate: _statusMode = kStatusDate; updateStatusWidget(); break;
	case kUiTime: _statusMode = kStatusTime; updateStatusWidget(); break;
	case kUiCash: _statusMode = kStatusCash; updateStatusWidget(); break;
	case kUiConstitution: _statusMode = kStatusCon; updateStatusWidget(); break;

	// The six navigation buttons open auxiliary scenes that aren't ported
	// yet. Fall back to a scroll-widget message so the interaction is
	// still visibly acknowledged.
	case kUiInventory: showText("Inventory (not yet implemented).", kWidgetScroll); break;
	case kUiPax:       showText("PAX network (not yet implemented).", kWidgetScroll); break;
	case kUiDialog:    showText("Dialog mode (not yet implemented).", kWidgetScroll); break;
	case kUiSkills:    showText("Skills (not yet implemented).", kWidgetScroll); break;
	case kUiChip:      showText("ROM construct (not yet implemented).", kWidgetScroll); break;
	case kUiDisk:      showText("Disk options (not yet implemented).", kWidgetScroll); break;

	default:
		debugC(1, kDebugGeneral, "RealWorldScene: unknown UI code 0x%02X", code);
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
	updateStatusWidget();
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

void RealWorldScene::clearTextWidgets() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerDialogBubble);
}

// Paint a bordered frame into `pixels` matching build_text_frame from
// LibNeuroRoutines/drawing.c: black top/bottom rows; white interior with a
// single-pixel black column on each side.
static void buildBorderFrame(byte *pixels, int widthPx, int heightPx) {
	int packedW = widthPx / 2;
	for (int row = 0; row < heightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == heightPx - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0]           = 0x0F; // black-left / white-right
			line[packedW - 1] = 0xF0; // white-left / black-right
		}
	}
}

void RealWorldScene::showText(const char *text, TextWidget widget) {
	clearTextWidgets();

	if (widget == kWidgetScroll) {
		// Scroll widget: white-on-black, no frame. Text sits inside the
		// dark notch in NEURO.IMH at (176, 134).
		byte *pixels = _scrollSprite.data() + sizeof(ImhHeader);
		memset(pixels, 0, kScrollBytes);

		Common::String wrapped = wrapText(text, kScrollColumns);
		drawString(wrapped.c_str(), kScrollWidthPx, kScrollHeightPx, 0, 0, pixels);

		_engine->spriteChain()->addSprite(kLayerNeuroMenu, kScrollX, kScrollY,
		                                  _scrollSprite.data(), true);
	} else {
		// Bubble widget: bordered white frame with black text inside.
		// Same visual style as the main-menu NeuroMenu but full-screen
		// width at top=4. Text is XOR-ed into the white interior so
		// drawString's colour-15 pixels flip to colour-0 (black).
		byte *pixels = _bubbleSprite.data() + sizeof(ImhHeader);
		buildBorderFrame(pixels, kBubbleWidthPx, kBubbleHeightPx);

		Common::Array<byte> scratch;
		scratch.resize(kBubbleBytes);
		memset(scratch.data(), 0, scratch.size());

		Common::String wrapped = wrapText(text, kBubbleColumns);
		drawString(wrapped.c_str(), kBubbleWidthPx, kBubbleHeightPx,
		           kBubbleInnerLeft, kBubbleInnerTop, scratch.data());

		for (int i = 0; i < (int)kBubbleBytes; i++)
			pixels[i] ^= scratch[i];

		_engine->spriteChain()->addSprite(kLayerDialogBubble, kBubbleX, kBubbleY,
		                                  _bubbleSprite.data(), true);
	}

	_textVisible = true;
}

// Renders the current status-panel string in a small 64x8 sprite placed at
// (96, 149). Follows the DOS formatting (scene_real_world.c:630-654):
//   UI_PM_CASH -> "$%7d"         (e.g. "$      0")
//   UI_PM_CON  -> "%8d"          (e.g. "    2000")
//   UI_PM_TIME -> "   %02d:%02d" (e.g. "   07:15")
//   UI_PM_DATE -> "mm/dd/yy"     (via build_date_string)
void RealWorldScene::updateStatusWidget() {
	byte *pixels = _statusSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0, kStatusBytes);

	char buf[9] = { 0 };
	switch (_statusMode) {
	case kStatusCash:
		snprintf(buf, sizeof(buf), "$%7d", _cash);
		break;
	case kStatusCon:
		snprintf(buf, sizeof(buf), "%8d", _constitution);
		break;
	case kStatusTime:
		snprintf(buf, sizeof(buf), "   %02d:%02d", _timeH, _timeM);
		break;
	case kStatusDate: {
		// build_date_string(dst, date_day): day 0 => 11/16/58, wraps to
		// 12/dd/58 past 14, 01/dd/59 past 45. See scene_real_world.c:567-593.
		int day = 16, month = 11, year = 58;
		if (_dateDay > 14) {
			if (day + _dateDay > 61) {
				year = 59; month = 1; day = day + _dateDay - 61;
			} else {
				month = 12; day = day + _dateDay - 30;
			}
		} else {
			day += _dateDay;
		}
		snprintf(buf, sizeof(buf), "%02d/%02d/%02d", month, day, year);
		break;
	}
	}

	drawString(buf, kStatusWidthPx, kStatusHeightPx, 0, 0, pixels);

	// Sits below the NEURO.IMH background but above most other layers;
	// kLayerCharacter has no other tenant in this stub. Transparent so
	// black background pixels show through to the UI chrome behind.
	_engine->spriteChain()->addSprite(kLayerCharacter, kStatusX, kStatusY, _statusSprite.data(), false);
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
		break;
	}
}

} // End of namespace Neuromancer
