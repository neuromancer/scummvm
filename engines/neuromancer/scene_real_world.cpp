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
	  _currentPage(0),
	  _activeWidget(kWidgetScroll),
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
	chain->clearSprite(kLayerDebugOverlay);
}

SceneId RealWorldScene::update() {
	if (!_textVisible && !_introPending)
		advanceVmOnce();

	_engine->render();
	return _next;
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_KEYDOWN) {
		// Dismiss / page through blocking text. Each keypress advances
		// one page; only the final page completes the widget (resumes VM
		// or starts it for pre-VM intro).
		if (_introPending) {
			if (pageTextForward()) {
				return; // still more pages to show
			}
			clearTextWidgets();
			_textVisible = false;
			_introPending = false;
			startVmForCurrentLevel();
			return;
		}
		if (_textVisible) {
			if (pageTextForward()) {
				return;
			}
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

		// --- TEMPORARY debug backdoors while the dialog-choice scene
		// isn't ported yet. Level 1's pay-Ratz loop checks var[16]==6
		// (paid) and var[18]==127 (bypassed). Pressing F8 / F9 writes
		// those values directly so we can step past the loop and see
		// what the script does next. Remove once opcode 0x17 has a
		// real dialog UI.
		case Common::KEYCODE_F8:
			debugC(1, kDebugScript, "DEBUG: writing var[16] = 6 (paid Ratz)");
			_engine->vm()->writeVar8(16, 6);
			break;
		case Common::KEYCODE_F9:
			debugC(1, kDebugScript, "DEBUG: writing var[18] = 127 (bypass)");
			_engine->vm()->writeVar8(18, 127);
			break;

		default:
			break;
		}
		return;
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		// A click also pages / dismisses pending text.
		if (_introPending) {
			if (pageTextForward()) return;
			clearTextWidgets();
			_textVisible = false;
			_introPending = false;
			startVmForCurrentLevel();
			return;
		}
		if (_textVisible) {
			if (pageTextForward()) return;
			clearTextWidgets();
			_textVisible = false;
			_engine->vm()->resume();
			return;
		}
		int uiAction = hitTestUiButton(event.mouse.x, event.mouse.y);
		if (uiAction >= 0) {
			onUiAction(uiAction);
			return;
		}
		// PIC area click (8,8 to 312,120) -- scene-level navigation.
		if (event.mouse.x >= 8 && event.mouse.x < 8 + 304 &&
		    event.mouse.y >= 8 && event.mouse.y < 8 + 112) {
			handlePicClick(event.mouse.x, event.mouse.y);
		}
	}
}

// Placeholder click-to-move: split the PIC area into three zones
// (left-edge / middle / right-edge). Edge clicks page through levels
// exactly like the arrow keys, so the player gets visible feedback.
// Once ROOMPOS.BIH parsing lands we'll dispatch to level-specific
// exits instead; for now the raw roompos bytes for this level are
// also logged so we can see them against screenshots.
void RealWorldScene::handlePicClick(int x, int y) {
	int relX = x - 8;   // 0..303 inside the PIC
	int relY = y - 8;   // 0..111

	debugC(1, kDebugGeneral,
	       "RealWorldScene: PIC click at screen (%d, %d) -> relative (%d, %d)",
	       x, y, relX, relY);

	// Dump the level's ROOMPOS entry for future use.
	if (const byte *rp = _engine->roompos()) {
		uint8 lvl = _engine->currentLevel();
		uint32 off = (uint32)lvl * 20;
		if (off + 20 <= _engine->roomposSize()) {
			debugC(1, kDebugGeneral,
			       "  roompos[level=%u]: floor=%02X %02X %02X %02X"
			       "  exits=[%02X %02X %02X %02X] [%02X %02X %02X %02X]"
			       " [%02X %02X %02X %02X] [%02X %02X %02X %02X]",
			       lvl,
			       rp[off+0],  rp[off+1],  rp[off+2],  rp[off+3],
			       rp[off+4],  rp[off+5],  rp[off+6],  rp[off+7],
			       rp[off+8],  rp[off+9],  rp[off+10], rp[off+11],
			       rp[off+12], rp[off+13], rp[off+14], rp[off+15],
			       rp[off+16], rp[off+17], rp[off+18], rp[off+19]);
		}
	}

	// Edge-zone navigation placeholder.
	const int kEdgeWidth = 60;
	if (relX < kEdgeWidth) {
		gotoLevel(-1);
	} else if (relX >= 304 - kEdgeWidth) {
		gotoLevel(+1);
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

	// Player character: initial pose is UP-facing (DOS roompos_init picks
	// dir = (transition+2)&3 = CD_UP for first entry, see
	// Reuromancer/NeuromancerWin64/scene_real_world.c:696). Frame 0 of
	// g_up_frames[] is at SPRITES.IMH offset 0x0000. Rendered on
	// kLayerCharacter with transparent key 0 so the sprite's background
	// pixels show the PIC through.
	if (const byte *sheet = _engine->spritesheet()) {
		const uint32 kCharUpFrame0 = 0x0000;
		int posX = 8 + 304 / 2;
		int posY = 8 + 112 - 40;
		_engine->spriteChain()->addSprite(kLayerCharacter, posX, posY,
		                                  sheet + kCharUpFrame0,
		                                  /*opaque=*/false, /*transKey=*/0);
	}

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
			line[0]           = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

// Split a word-wrapped (newline-separated) body into pages of `linesPerPage`
// lines each. Trailing empty lines are preserved only in the last page.
static Common::Array<Common::String> paginate(const Common::String &wrapped, int linesPerPage) {
	Common::Array<Common::String> pages;
	if (linesPerPage <= 0 || wrapped.empty()) {
		pages.push_back(wrapped);
		return pages;
	}

	Common::Array<Common::String> lines;
	Common::String cur;
	for (uint i = 0; i < wrapped.size(); i++) {
		char c = wrapped[i];
		if (c == '\n') {
			lines.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	if (!cur.empty())
		lines.push_back(cur);

	for (uint i = 0; i < lines.size(); i += linesPerPage) {
		Common::String page;
		for (uint j = i; j < i + linesPerPage && j < lines.size(); j++) {
			if (j > i) page += '\n';
			page += lines[j];
		}
		pages.push_back(page);
	}
	if (pages.empty())
		pages.push_back(Common::String());
	return pages;
}

void RealWorldScene::showText(const char *text, TextWidget widget) {
	clearTextWidgets();

	// Expand DOS control codes (0x01 name, 0x02 date, \r -> \n).
	Common::String expanded = expandText(text, /*playerName*/ "", /*dateString*/ "");

	int columns = (widget == kWidgetScroll) ? kScrollColumns : kBubbleColumns;
	int rows    = (widget == kWidgetScroll) ? kScrollRows    : kBubbleRows;

	Common::String wrapped = wrapText(expanded.c_str(), columns);
	_pages        = paginate(wrapped, rows);
	_currentPage  = 0;
	_activeWidget = widget;
	_textVisible  = true;

	debugC(1, kDebugScript, "RealWorldScene: text -> %u page(s) in widget %d",
	       (uint)_pages.size(), (int)widget);

	renderCurrentPage();
}

void RealWorldScene::renderCurrentPage() {
	if (_pages.empty() || _currentPage >= (int)_pages.size())
		return;
	const Common::String &page = _pages[_currentPage];

	// Count lines in the page (1 + number of '\n'). Used to size the
	// bubble widget so its frame fits the content exactly, matching the
	// DOS formula bottom = lines*8 + 19 (neuro_window_control.c:96).
	int lines = 1;
	for (uint i = 0; i < page.size(); i++)
		if (page[i] == '\n')
			lines++;

	if (_activeWidget == kWidgetScroll) {
		// Scroll widget must match DOS build_string behaviour: inside
		// each character cell, write black (OFF pixels) and white (ON
		// pixels) verbatim; leave everything OUTSIDE character cells
		// untouched so the NEURO.IMH chrome at (176, 134) keeps showing.
		//
		// We can't just blit with colour 0 as the transparent key, since
		// black IS used for OFF pixels inside glyphs. Instead we fill
		// the sprite with sentinel colour 14 (packed byte 0xEE), let
		// drawString overwrite each drawn character's 4x8 packed region
		// with the font pattern (0x00/0x0F/0xF0/0xFF), and transBlit
		// with 14 as the key -- so drawn cells composite as black+white
		// and undrawn cells stay transparent.
		byte *pixels = _scrollSprite.data() + sizeof(ImhHeader);
		memset(pixels, 0xEE, kScrollBytes);
		drawString(page.c_str(), kScrollWidthPx, kScrollHeightPx, 0, 0, pixels);
		_engine->spriteChain()->addSprite(kLayerNeuroMenu, kScrollX, kScrollY,
		                                  _scrollSprite.data(),
		                                  /*opaque=*/false, /*transKey=*/14);
		return;
	}

	// Bubble widget: size dynamically to the current page's line count.
	// height = lines*8 + 16 (inner text) + 2 border rows; clamp to the
	// pre-allocated buffer so we never overflow.
	int desiredH = lines * 8 + 16;
	if (desiredH > (int)kBubbleHeightPx) desiredH = (int)kBubbleHeightPx;
	int packedW = kBubbleWidthPx / 2;
	int usedBytes = packedW * desiredH;

	// Update the IMH header in the sprite buffer to reflect the tighter
	// size. width stays at packedW; height is the per-page height.
	WRITE_LE_UINT16(_bubbleSprite.data() + 6, (uint16)desiredH);

	byte *pixels = _bubbleSprite.data() + sizeof(ImhHeader);
	buildBorderFrame(pixels, kBubbleWidthPx, desiredH);

	Common::Array<byte> scratch;
	scratch.resize(usedBytes);
	memset(scratch.data(), 0, scratch.size());
	drawString(page.c_str(), kBubbleWidthPx, desiredH,
	           kBubbleInnerLeft, kBubbleInnerTop, scratch.data());
	for (int i = 0; i < usedBytes; i++)
		pixels[i] ^= scratch[i];

	_engine->spriteChain()->addSprite(kLayerDialogBubble, kBubbleX, kBubbleY,
	                                  _bubbleSprite.data(), /*opaque=*/true);
}

// Advance to the next page. Returns true if there was more to show (caller
// should not dismiss the widget), false if the final page was already
// visible (caller should clear + resume VM).
bool RealWorldScene::pageTextForward() {
	if (_currentPage + 1 >= (int)_pages.size())
		return false;
	_currentPage++;
	renderCurrentPage();
	return true;
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

	// Lives on kLayerDebugOverlay (= 1) so it doesn't collide with the
	// player character sprite on kLayerCharacter. Opaque fill so both
	// white text pixels AND black glyph-interior pixels render cleanly,
	// matching DOS build_string's behaviour of overwriting the underlying
	// g_seg010.background bytes in place.
	_engine->spriteChain()->addSprite(kLayerDebugOverlay, kStatusX, kStatusY, _statusSprite.data(), true);
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
		debugC(1, kDebugScript,
		       "RealWorldScene: bubble text[%u] at (%u, %u) = \"%s\"",
		       r.stringNum, r.var1, r.var2, s);
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
