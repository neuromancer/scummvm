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

#include "neuromancer/inventory.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// Inventory window geometry from DOS neuro_window_control.c:72-77:
// rect = (56, 128) .. (231, 191), so 176 px wide x 64 px tall.
enum {
	kWindowX        = 56,
	kWindowY        = 128,
	kWindowWidthPx  = 176,
	kWindowHeightPx = 64,
	kWindowPackedW  = kWindowWidthPx / 2,
	kWindowBytes    = kWindowPackedW * kWindowHeightPx
};

// Credit "item" pseudo-code. DOS stores 0x7F for the virtual credits slot
// (rw_state_inventory.c:112). Picking credits + Give routes to the cash-
// withdrawal payment path the Ratz script (and later NPCs) detect via
// active_item and cash_withdrawal.
const uint8 kItemCodeCredits = 0x7F;

// Item-name lookup, transcribed from DOS items.c:5-92 (g_item_names[104]).
// Empty strings mark unused codes; we display "(item NN)" for those.
static const char *const kItemNames[104] = {
	"Mimic", "Jammies", "ThunderHead", "Vaccine", "Blammo", "DoorStop",
	"Decoder", "Sequencer", "ArmorAll", "KGB", "Comlink", "BlowTorch",
	"Probe", "Drill", "Hammer", "Python", "Acid", "Injector",
	"DepthCharge", "Concrete", "EasyRider", "LogicBomb", "Cyberspace",
	"Slow", "BattleChess", "BattleChess", "Scout", "Hemlock",
	"KuangEleven", "Hiki Gaeru", "Gaijin", "Bushido", "Edokko", "Katana",
	"Tofu", "Shogun", "188BJB", "350SL",
	"", "",
	"UXB", "",
	"ZXB", "Cyberspace II", "Cyberspace III", "",
	"Cyberspace VII", "Ninja 2000", "Ninja 3000", "Ninja 4000",
	"Ninja 5000", "Blue Light Spec.", "Samurai Seven",
	"", "", "", "", "", "", "",
	"", "", "", "", "", "", "",
	"Bargaining", "CopTalk", "Warez Analysis", "Debug", "Hardware Repair",
	"ICE Breaking", "Evasion", "Cryptology", "Japanese", "Logic",
	"Psychoanalysis", "Phenomenology", "Philosophy", "Sophistry", "Zen",
	"Musicianship", "CyberEyes",
	"", "",
	"guest pass",
	"", "",
	"joystick",
	"", "", "", "",
	"caviar", "pawn ticket", "Security Pass", "Zion ticket",
	"Freeside ticket", "",
	"Chiba ticket", "gas mask", "",
	"sake"
};

// DSEG addresses written by the DOS Give Credits path (data.h:217-220).
// active_item is a uint16 at 0x4BC0; cash_withdrawal is a uint32 at 0x4BC2.
// x4bbf (at 0x4BBF) is flagged to 1 to mark "credits offered this level".
const uint16 kVarX4BBF           = 0x4BBF;
const uint16 kVarActiveItem      = 0x4BC0;
const uint16 kVarCashWithdrawal  = 0x4BC2;
// x4ccb / x4ccc get written on Operate to communicate op category to
// per-level BIH scripts (DOS rw_state_inventory.c:337-338).
const uint16 kVarX4CCB           = 0x4CCB;
const uint16 kVarX4CCC           = 0x4CCC;
// gas_mask_is_on (DOS data.h:243, DSEG 0x4C19).
const uint16 kVarGasMaskIsOn     = 0x4C19;

// Per-item Operate dispatch table. Transcribed from DOS data.c:361-378
// (g_inventory_item_operations[128]).
//   bit 7 (0x80): hardware item (deck / skill-chip / gas-mask / ...)
//   bits 4-5 (0x30): category
//     0x00: jackable (deck software -- needs NPC jack on level)
//     0x10: database-only
//     0x20: cyberspace-only
//   bits 0-3 (0x0F): subcategory / script callback index
//   0xFF:          no-op ("Nothing happens.")
static const uint8 kItemOperations[128] = {
	0x25, 0x27, 0x23, 0x01, 0x29, 0x22, 0x22, 0x10,
	0x24, 0x2B, 0x00, 0x22, 0x21, 0x22, 0x22, 0x23,
	0x23, 0x23, 0x22, 0x22, 0x26, 0x22, 0x01, 0x27,
	0x12, 0x20, 0x11, 0x20, 0x20, 0x80, 0x80, 0x80,
	0x80, 0xC0, 0xC0, 0xC0, 0x80, 0x80, 0xC0, 0xC0,
	0x80, 0x80, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0x80,
	0x80, 0xC0, 0xC0, 0x80, 0xC0, 0xFF, 0xFF, 0xFF,
	0x84, 0x85, 0x80, 0x80, 0x80, 0x80, 0x2A, 0x00,
	0x00, 0x00, 0x00, 0x81, 0x81, 0x81, 0x81, 0x81,
	0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
	0x81, 0x81, 0x81, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x82, 0x83, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF,
	0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
	0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

Inventory::Inventory(NeuromancerEngine *engine, RealWorldScene *scene)
	: _engine(engine),
	  _scene(scene),
	  _active(false),
	  _state(kStateItemList),
	  _selectedItemCode(0),
	  _selectedSlot(-1),
	  _amountLen(0),
	  _pageStart(0),
	  _pageCount(0),
	  _viewingSoftware(false),
	  _messageClosesInv(false) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
	memset(_amount, 0, sizeof(_amount));
	for (int i = 0; i < 4; i++) { _pageSlots[i] = -1; _pageCodes[i] = 0xFF; }
}

void Inventory::open() {
	_active          = true;
	_state           = kStateItemList;
	_pageStart       = 0;
	_viewingSoftware = false;
	drawItemList();
	debugC(1, kDebugGeneral, "Inventory: opened");
}

int Inventory::countItems() const {
	int count = 0;
	const uint8 *slots = _viewingSoftware ?
		_scene->softwareSlots() : _scene->itemSlots();
	for (int i = 0; i < 32; i++) {
		if (slots[i * 4] != 0xFF)
			count++;
	}
	return count;
}

// Fill _pageSlots / _pageCodes with up to 4 entries for the current page.
// The very first page reserves row 0 for the virtual Credits entry (slot
// = -1, code = 0x7F); subsequent pages skip it. Empty slots are skipped
// so the page always packs contiguous visible items.
int Inventory::rebuildPage() {
	_pageCount = 0;
	for (int i = 0; i < 4; i++) { _pageSlots[i] = -1; _pageCodes[i] = 0xFF; }

	int cursor = _pageStart;
	if (!_viewingSoftware && _pageStart == 0) {
		// Items view first page: Credits gets row 0.
		_pageSlots[_pageCount] = -1;
		_pageCodes[_pageCount] = kItemCodeCredits;
		_pageCount++;
		cursor = 0; // real items still start at slot 0
	}

	const uint8 *slots = _viewingSoftware ?
		_scene->softwareSlots() : _scene->itemSlots();
	int seen = 0;
	for (int i = 0; i < 32 && _pageCount < 4; i++) {
		if (slots[i * 4] == 0xFF)
			continue;
		if (seen < cursor) {
			seen++;
			continue;
		}
		_pageSlots[_pageCount] = i;
		_pageCodes[_pageCount] = slots[i * 4];
		_pageCount++;
		seen++;
	}
	return _pageCount;
}

Common::String Inventory::itemDisplayName(uint8 code) const {
	if (code == kItemCodeCredits)
		return Common::String::format("Credits %d", _scene->cash());
	if (code < 104 && kItemNames[code][0] != 0)
		return Common::String(kItemNames[code]);
	return Common::String::format("(item %u)", code);
}

void Inventory::close() {
	_active = false;
	_engine->spriteChain()->clearSprite(kLayerPaxWindow);
	debugC(1, kDebugGeneral, "Inventory: closed");
}

void Inventory::update() {
	// No per-frame animation in Phase 1 -- the inventory is static between
	// key / click events.
}

bool Inventory::handleEvent(const Common::Event &event) {
	// Give Credits text entry absorbs almost all keys until Esc / Enter.
	if (_state == kStateGiveCredits) {
		if (handleGiveCreditsKey(event))
			return true;
	}

	// Message screen absorbs any input and dispatches based on the state
	// it was entered from.
	if (_state == kStateMessage) {
		if (event.type == Common::EVENT_KEYDOWN ||
		    event.type == Common::EVENT_LBUTTONDOWN) {
			if (_messageClosesInv)
				close();
			else
				drawItemList();
			return true;
		}
		return false;
	}

	if (event.type == Common::EVENT_KEYDOWN) {
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			// Escape walks back one level: erase-confirm -> software list,
			// software list -> items list, anything else -> items list,
			// items list -> close inventory.
			switch (_state) {
			case kStateItemList:
				close();
				break;
			case kStateSoftwareErase:
				drawSoftwareList();
				break;
			case kStateSoftwareList:
				_viewingSoftware = false;
				_pageStart = 0;
				drawItemList();
				break;
			default:
				drawItemList();
				break;
			}
			return true;
		}

		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		switch (_state) {
		case kStateItemList:      return dispatchItemList(key);
		case kStateItemOptions:   return dispatchItemOptions(key);
		case kStateDiscard:       return dispatchDiscardConfirm(key);
		case kStateSoftwareList:  return dispatchSoftwareList(key);
		case kStateSoftwareErase: return dispatchSoftwareEraseConfirm(key);
		case kStateGiveItem:      return dispatchGiveItemConfirm(key);
		default: break;
		}
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		int x = event.mouse.x;
		int y = event.mouse.y;
		switch (_state) {
		case kStateItemList: {
			// Item rows at screen y = kWindowY + 16 + (i+1)*8. Rows span
			// x 64..223 (DOS g_inv_buttons uses left=64, right=223).
			for (int i = 0; i < _pageCount; i++) {
				int rowTop    = kWindowY + 16 + (i + 1) * 8;
				int rowBottom = rowTop + 7;
				if (x >= 64 && x <= 223 && y >= rowTop && y <= rowBottom)
					return dispatchItemList((char)('1' + i));
			}
			// Footer row at y=48 (inside window) -> screen y = 176.
			int footerTop = kWindowY + 48;
			int footerBottom = footerTop + 7;
			if (x >= 96 && x <= 127 && y >= footerTop && y <= footerBottom)
				return dispatchItemList('x');
			if (x >= 136 && x <= 167 && y >= footerTop && y <= footerBottom)
				return dispatchItemList('m');
			break;
		}
		case kStateItemOptions: {
			// item_page_exit at (64, 144)..(199, 151) from g_inv_buttons.
			if (x >= 64 && x <= 199 && y >= 144 && y <= 151)
				return dispatchItemOptions('x');
			// Operate / Discard / Give rows: y = 152, 160, 168 (8 px each).
			if (x >= 64 && x <= 199 && y >= 152 && y <= 159)
				return dispatchItemOptions('o');
			if (x >= 64 && x <= 199 && y >= 160 && y <= 167)
				return dispatchItemOptions('d');
			if (x >= 64 && x <= 199 && y >= 168 && y <= 175)
				return dispatchItemOptions('g');
			break;
		}
		case kStateDiscard: {
			// DOS yes/no rects from g_inv_disc_buttons (data.c:184). The
			// rects land at screen (176,168..183,175) and (192,168..199,175).
			if (x >= 176 && x <= 183 && y >= 168 && y <= 175)
				return dispatchDiscardConfirm('y');
			if (x >= 192 && x <= 199 && y >= 168 && y <= 175)
				return dispatchDiscardConfirm('n');
			break;
		}
		case kStateSoftwareList: {
			// Same row layout as the items list (y = kWindowY + 16 + (i+1)*8).
			for (int i = 0; i < _pageCount; i++) {
				int rowTop    = kWindowY + 16 + (i + 1) * 8;
				int rowBottom = rowTop + 7;
				if (x >= 64 && x <= 223 && y >= rowTop && y <= rowBottom)
					return dispatchSoftwareList((char)('1' + i));
			}
			int footerTop = kWindowY + 48;
			int footerBottom = footerTop + 7;
			if (x >= 96 && x <= 127 && y >= footerTop && y <= footerBottom)
				return dispatchSoftwareList('x');
			if (x >= 136 && x <= 167 && y >= footerTop && y <= footerBottom)
				return dispatchSoftwareList('m');
			break;
		}
		case kStateSoftwareErase: {
			if (x >= 176 && x <= 183 && y >= 168 && y <= 175)
				return dispatchSoftwareEraseConfirm('y');
			if (x >= 192 && x <= 199 && y >= 168 && y <= 175)
				return dispatchSoftwareEraseConfirm('n');
			break;
		}
		case kStateGiveItem: {
			if (x >= 176 && x <= 183 && y >= 168 && y <= 175)
				return dispatchGiveItemConfirm('y');
			if (x >= 192 && x <= 199 && y >= 168 && y <= 175)
				return dispatchGiveItemConfirm('n');
			break;
		}
		default: break;
		}
	}

	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void Inventory::drawWindowFrame() {
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	// Same DOS build_text_frame layout used by PAX: black top/bottom rows,
	// white interior with a 1-pixel black column on each vertical edge.
	const int packedW = kWindowPackedW;
	for (int row = 0; row < (int)kWindowHeightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == (int)kWindowHeightPx - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0]           = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

void Inventory::drawItemList() {
	_state = kStateItemList;
	rebuildPage();
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header row centered-ish at (56, 8) inside the window. DOS uses
	// neuro_window_draw_string("Items", 56, 8) at rw_state_inventory.c:87.
	drawString("Items", kWindowWidthPx, kWindowHeightPx, 56, 8, pixels);

	// Each row spans 8 pixels, starting at window-relative y=16.
	// DOS pattern: "N. [flag-char]name". flag-char is ' ' for normal or
	// '-' for items marked inactive (slot byte 2 != 0).
	const uint8 *slots = _scene->itemSlots();
	for (int i = 0; i < _pageCount; i++) {
		Common::String name = itemDisplayName(_pageCodes[i]);
		char prefix = ' ';
		if (_pageSlots[i] >= 0 && slots[_pageSlots[i] * 4 + 2] != 0)
			prefix = '-';
		Common::String row = Common::String::format("%d. %c%s",
		                                             i + 1, prefix, name.c_str());
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx,
		           8, 16 + (i + 1) * 8, pixels);
	}

	// Footer. DOS draws "exit" at (40, 48) and adds "more" at (80, 48) if
	// there are more items than fit on the current page.
	drawString("exit", kWindowWidthPx, kWindowHeightPx, 40, 48, pixels);
	int totalVisible = countItems() + 1; // +1 for Credits
	if (totalVisible > _pageStart + _pageCount)
		drawString("more", kWindowWidthPx, kWindowHeightPx, 80, 48, pixels);

	pushSprite();
}

void Inventory::drawItemOptions() {
	_state = kStateItemOptions;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header: currently-selected item's display name.
	Common::String header = itemDisplayName(_selectedItemCode);
	drawString(header.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	// DOS lays out X / Operate / Discard / (Give) rows vertically with
	// 8-px spacing starting at window-relative (8, 16).
	drawString("X. Exit",           kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);
	drawString("O. Operate Item",   kWindowWidthPx, kWindowHeightPx, 8, 24, pixels);
	drawString("D. Discard Item",   kWindowWidthPx, kWindowHeightPx, 8, 32, pixels);
	// DOS shows "Give Item" only when there's an NPC on the current level
	// that can receive it (g_a8e0.a8e0[] has an active thread) or level
	// 55. We don't yet track per-level NPC threads, so we unconditionally
	// offer Give. VM scripts on levels without a valid receiver will
	// simply ignore the active_item write.
	if (_selectedItemCode == kItemCodeCredits)
		drawString("G. Give Credits", kWindowWidthPx, kWindowHeightPx, 8, 40, pixels);
	else
		drawString("G. Give Item",    kWindowWidthPx, kWindowHeightPx, 8, 40, pixels);

	// "E. Erase Software" is only offered for CyberEyes (0x53) and the
	// cyberdeck range 0x1D..0x34 (DOS rw_state_inventory.c:220). Picking
	// it jumps to the software list.
	bool isEraseCapable = (_selectedItemCode == 0x53) ||
		(_selectedItemCode >= 0x1D && _selectedItemCode <= 0x34);
	if (isEraseCapable)
		drawString("E. Erase Software", kWindowWidthPx, kWindowHeightPx, 8, 48, pixels);

	pushSprite();
}

void Inventory::drawDiscardConfirm() {
	_state = kStateDiscard;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// DOS discard screen: "Discard" label at (72, 8), item name at (16, 24),
	// "Are you sure (Y/N)" at (8, 40). We compress a bit for our narrower
	// window.
	Common::String name = itemDisplayName(_selectedItemCode);
	Common::String head = Common::String::format(
		"Discard\n\n"
		"  %s\n\n"
		"Are you sure (Y/N)?",
		name.c_str());
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	pushSprite();
}

void Inventory::drawGiveCredits() {
	_state = kStateGiveCredits;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header shows current cash so the player knows the cap, followed by
	// the amount editor cursor. DOS redraws just the edited field each
	// keystroke; we redo the whole window for simplicity.
	Common::String head = Common::String::format(
		"Give Credits\n"
		"\n"
		"Cash on hand: %d\n"
		"\n"
		"Amount: %s<",
		_scene->cash(), _amount);
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	pushSprite();
}

void Inventory::pushSprite() {
	_engine->spriteChain()->addSprite(kLayerPaxWindow, kWindowX, kWindowY,
	                                  _sprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Button dispatch
// -------------------------------------------------------------------------

bool Inventory::dispatchItemList(char code) {
	switch (code) {
	case 'x':
		close();
		return true;
	case 'm': {
		// "more": advance past the items we just showed. DOS formula:
		// first page shows Credits + 3 items (advance 3); later pages
		// show 4 items (advance 4). We use _pageCount minus the virtual
		// Credits row on page 0 to compute the advance.
		int advance = _pageCount;
		if (_pageStart == 0 && _pageCount > 0) advance = _pageCount - 1;
		int totalItems = countItems();
		_pageStart += advance;
		if (_pageStart >= totalItems) _pageStart = 0;
		drawItemList();
		return true;
	}
	case '1': case '2': case '3': case '4': {
		int idx = code - '1';
		if (idx >= _pageCount)
			return false;
		_selectedItemCode = _pageCodes[idx];
		_selectedSlot     = _pageSlots[idx];
		drawItemOptions();
		return true;
	}
	default:
		return false;
	}
}

bool Inventory::dispatchItemOptions(char code) {
	switch (code) {
	case 'x':
		drawItemList();
		return true;
	case 'o':
		operateSelectedItem();
		return true;
	case 'd':
		// Discard: credits can't be discarded; real items go through a
		// Y/N confirmation. DOS behaviour from rw_state_inventory.c:565.
		if (_selectedItemCode == kItemCodeCredits || _selectedSlot < 0) {
			drawItemList();
			return true;
		}
		drawDiscardConfirm();
		return true;
	case 'g':
		if (_selectedItemCode == kItemCodeCredits) {
			memset(_amount, 0, sizeof(_amount));
			_amountLen = 0;
			drawGiveCredits();
		} else if (_selectedSlot >= 0) {
			drawGiveItemConfirm();
		} else {
			drawItemList();
		}
		return true;
	case 'e': {
		// Jump to the software list. Only valid for erase-capable items
		// (CyberEyes / cyberdeck range); bounce otherwise.
		bool isEraseCapable = (_selectedItemCode == 0x53) ||
			(_selectedItemCode >= 0x1D && _selectedItemCode <= 0x34);
		if (!isEraseCapable) {
			drawItemList();
			return true;
		}
		_viewingSoftware = true;
		_pageStart       = 0;
		drawSoftwareList();
		return true;
	}
	default:
		return false;
	}
}

bool Inventory::dispatchDiscardConfirm(char code) {
	switch (code) {
	case 'y':
		// Clear the slot so rebuildPage skips it next time.
		if (_selectedSlot >= 0) {
			_scene->itemSlots()[_selectedSlot * 4] = 0xFF;
			debugC(1, kDebugGeneral,
			       "Inventory: discarded slot %d (code 0x%02X)",
			       _selectedSlot, _selectedItemCode);
		}
		_selectedSlot = -1;
		_selectedItemCode = 0;
		drawItemList();
		return true;
	case 'n':
		drawItemList();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// Software list + Erase Software
// -------------------------------------------------------------------------

void Inventory::drawSoftwareList() {
	_state           = kStateSoftwareList;
	_viewingSoftware = true;
	rebuildPage();
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	drawString("Software", kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	const uint8 *slots = _scene->softwareSlots();
	for (int i = 0; i < _pageCount; i++) {
		int slot = _pageSlots[i];
		Common::String name = itemDisplayName(_pageCodes[i]);
		uint8 version = (slot >= 0) ? slots[slot * 4 + 1] : 0;
		char prefix = ' ';
		if (slot >= 0 && slots[slot * 4 + 2] != 0)
			prefix = '-';
		// DOS format: "N. flag-name VV.0"; version comes from slot byte 1.
		Common::String row = Common::String::format("%d. %c%-11s %2u.0",
		                                             i + 1, prefix, name.c_str(),
		                                             version);
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx,
		           8, 16 + (i + 1) * 8, pixels);
	}

	drawString("exit", kWindowWidthPx, kWindowHeightPx, 40, 48, pixels);
	int totalVisible = countItems();
	if (totalVisible > _pageStart + _pageCount)
		drawString("more", kWindowWidthPx, kWindowHeightPx, 80, 48, pixels);

	pushSprite();
}

void Inventory::drawSoftwareEraseConfirm() {
	_state = kStateSoftwareErase;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	const uint8 *slots = _scene->softwareSlots();
	uint8 version = (_selectedSlot >= 0) ? slots[_selectedSlot * 4 + 1] : 0;
	Common::String head = Common::String::format(
		"ERASE\n\n"
		"  %s %u.0\n\n"
		"Are you sure (Y/N)?",
		itemDisplayName(_selectedItemCode).c_str(), version);
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	pushSprite();
}

bool Inventory::dispatchSoftwareList(char code) {
	switch (code) {
	case 'x':
		// Back to items list.
		_viewingSoftware = false;
		_pageStart       = 0;
		drawItemList();
		return true;
	case 'm': {
		int totalItems = countItems();
		_pageStart += _pageCount;
		if (_pageStart >= totalItems) _pageStart = 0;
		drawSoftwareList();
		return true;
	}
	case '1': case '2': case '3': case '4': {
		int idx = code - '1';
		if (idx >= _pageCount || _pageSlots[idx] < 0)
			return false;
		_selectedSlot     = _pageSlots[idx];
		_selectedItemCode = _pageCodes[idx];
		drawSoftwareEraseConfirm();
		return true;
	}
	default:
		return false;
	}
}

bool Inventory::dispatchSoftwareEraseConfirm(char code) {
	switch (code) {
	case 'y':
		if (_selectedSlot >= 0) {
			_scene->softwareSlots()[_selectedSlot * 4] = 0xFF;
			debugC(1, kDebugGeneral,
			       "Inventory: erased software slot %d (code 0x%02X)",
			       _selectedSlot, _selectedItemCode);
		}
		_selectedSlot     = -1;
		_selectedItemCode = 0;
		drawSoftwareList();
		return true;
	case 'n':
		drawSoftwareList();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// Give Credits: amount entry
// -------------------------------------------------------------------------

bool Inventory::handleGiveCreditsKey(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	Common::KeyCode kc = event.kbd.keycode;
	if (kc == Common::KEYCODE_ESCAPE) {
		drawItemOptions();
		return true;
	}
	if (kc == Common::KEYCODE_RETURN || kc == Common::KEYCODE_KP_ENTER) {
		commitGiveCredits();
		return true;
	}
	if (kc == Common::KEYCODE_BACKSPACE) {
		if (_amountLen > 0) {
			_amount[--_amountLen] = 0;
			drawGiveCredits();
		}
		return true;
	}
	uint16 ch = event.kbd.ascii;
	if (ch >= '0' && ch <= '9' && _amountLen < 8) {
		_amount[_amountLen++] = (char)ch;
		_amount[_amountLen] = 0;
		drawGiveCredits();
	}
	return true;
}

// Commit the amount typed by the player. Mirrors DOS on_inventory_give_
// credits_text_input (rw_state_inventory.c:611): if the amount fits in
// cash, debit _cash, set active_item + cash_withdrawal + x4bbf in the
// VM DSEG (so the level's BIH script can detect the payment), then close
// the inventory. Over-cash requests redraw the editor empty.
void Inventory::commitGiveCredits() {
	uint32 val = 0;
	for (int i = 0; i < _amountLen; i++)
		val = val * 10 + (uint32)(_amount[i] - '0');

	if (val == 0) {
		drawItemOptions();
		return;
	}
	if ((int32)val > _scene->cash()) {
		// Not enough cash -- blank the input and stay on the prompt.
		memset(_amount, 0, sizeof(_amount));
		_amountLen = 0;
		drawGiveCredits();
		return;
	}

	_scene->setCash(_scene->cash() - (int32)val);

	NeuroVM *vm = _engine->vm();
	vm->writeVar8(kVarX4BBF, 1);
	vm->writeVar16(kVarActiveItem, _selectedItemCode);
	// 32-bit cash_withdrawal written as two LE 16-bit halves.
	vm->writeVar16(kVarCashWithdrawal,       (uint16)(val & 0xFFFF));
	vm->writeVar16(kVarCashWithdrawal + 2,   (uint16)(val >> 16));

	debugC(1, kDebugGeneral,
	       "Inventory: gave %u credits (active_item=0x%02X, cash=%d)",
	       val, _selectedItemCode, _scene->cash());

	close();
}

// -------------------------------------------------------------------------
// Give Item (non-credits): hand the selected item to whoever is talking.
// -------------------------------------------------------------------------

void Inventory::drawGiveItemConfirm() {
	_state = kStateGiveItem;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String name = itemDisplayName(_selectedItemCode);
	Common::String head = Common::String::format(
		"GIVE\n\n"
		"  %s\n\n"
		"Are you sure (Y/N)?",
		name.c_str());
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	pushSprite();
}

// Mirrors DOS on_inventory_give_item_button (rw_state_inventory.c:516):
// on Y the slot is cleared, active_item + x4bbf are written to DSEG, and
// the inventory closes so the VM resumes and per-level scripts can react
// to the gift.
bool Inventory::dispatchGiveItemConfirm(char code) {
	switch (code) {
	case 'y': {
		if (_selectedSlot >= 0)
			_scene->itemSlots()[_selectedSlot * 4] = 0xFF;

		NeuroVM *vm = _engine->vm();
		vm->writeVar16(kVarActiveItem, _selectedItemCode);
		vm->writeVar8(kVarX4BBF, 1);
		debugC(1, kDebugGeneral,
		       "Inventory: gave item 0x%02X (slot %d)",
		       _selectedItemCode, _selectedSlot);

		_selectedSlot     = -1;
		_selectedItemCode = 0;
		close();
		return true;
	}
	case 'n':
		drawItemList();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// Message screen + Operate
// -------------------------------------------------------------------------

void Inventory::drawMessage(const char *text, bool closesInventory) {
	_state = kStateMessage;
	_messageClosesInv = closesInventory;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String body = Common::String::format(
		"%s\n\n\n"
		"Press any key.", text ? text : "");
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);

	pushSprite();
}

void Inventory::operateSelectedItem() {
	// Credits can't be "operated" in the DOS sense -- there's no side
	// effect. Show the canonical no-op and continue.
	if (_selectedItemCode == kItemCodeCredits) {
		drawMessage("Nothing happens.", /*closesInv=*/false);
		return;
	}
	if (_selectedItemCode >= 128) {
		drawMessage("Nothing happens.", /*closesInv=*/false);
		return;
	}

	uint8 itemOp = kItemOperations[_selectedItemCode];
	if (itemOp == 0xFF) {
		drawMessage("Nothing happens.", /*closesInv=*/false);
		return;
	}

	// Mirror item_op category bits into DSEG so scripts / later code can
	// read them (DOS rw_state_inventory.c:337-338).
	NeuroVM *vm = _engine->vm();
	vm->writeVar8(kVarX4CCB, (uint8)(itemOp & 0x0F));
	vm->writeVar8(kVarX4CCC, (uint8)(itemOp & 0x30));

	// Hardware items branch by subcategory (DOS line 343).
	if (itemOp & 0x80) {
		uint8 sub = itemOp & 0x0F;
		switch (sub) {
		case 1: {
			// Skill chip: add to skills[] at index (code - 0x43), consume
			// the item slot. DOS rw_state_inventory.c:351.
			int skillIdx = (int)_selectedItemCode - 0x43;
			if (skillIdx >= 0 && skillIdx < 16) {
				_scene->skills()[skillIdx] = 0;
			}
			if (_selectedSlot >= 0)
				_scene->itemSlots()[_selectedSlot * 4] = 0xFF;
			drawMessage("Skill chip implanted.", /*closesInv=*/false);
			return;
		}
		case 2: {
			// Gas mask toggle (DOS rw_state_inventory.c:357).
			bool on = !_scene->gasMaskOn();
			_scene->setGasMaskOn(on);
			vm->writeVar8(kVarGasMaskIsOn, on ? 1 : 0);
			drawMessage(on ? "Gas mask is on." : "Gas mask is off.",
			            /*closesInv=*/false);
			return;
		}
		case 0:
			// Deck: DOS opens a software picker here so the player can
			// Operate one of their uploaded programs inside cyberspace
			// (rw_state_inventory.c:346-349). Cyberspace isn't ported
			// yet, so bounce with a message instead of sending the
			// player into the Erase-software list by accident.
			drawMessage("Jack into cyberspace to run programs.",
			            /*closesInv=*/true);
			return;
		default:
			drawMessage("Not implemented yet.", /*closesInv=*/true);
			return;
		}
	}

	// Non-hardware: category drives the gate.
	uint8 cat = itemOp & 0x30;
	switch (cat) {
	case 0x10:
		// Database-only items (DOS line 434).
		drawMessage("Database only.", /*closesInv=*/true);
		return;
	case 0x20:
		// Cyberspace-only items (DOS line 374).
		drawMessage("Cyberspace only.", /*closesInv=*/true);
		return;
	default:
		// cat == 0x00 -> jackable items (need an NPC on the current level).
		// We don't yet have per-level NPC tracking, so err on the safe
		// side -- DOS says "No jack here." (line 424).
		drawMessage("No jack here.", /*closesInv=*/true);
		return;
	}
}

} // End of namespace Neuromancer
