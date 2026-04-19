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

// DSEG addresses written by the DOS Give Credits path (data.h:217-220).
// active_item is a uint16 at 0x4BC0; cash_withdrawal is a uint32 at 0x4BC2.
// x4bbf (at 0x4BBF) is flagged to 1 to mark "credits offered this level".
const uint16 kVarX4BBF           = 0x4BBF;
const uint16 kVarActiveItem      = 0x4BC0;
const uint16 kVarCashWithdrawal  = 0x4BC2;

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
	  _amountLen(0) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
	memset(_amount, 0, sizeof(_amount));
}

void Inventory::open() {
	_active = true;
	_state  = kStateItemList;
	drawItemList();
	debugC(1, kDebugGeneral, "Inventory: opened");
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

	if (event.type == Common::EVENT_KEYDOWN) {
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			// Escape from any sub-menu backs out to the item list; from the
			// list it closes the inventory entirely.
			if (_state == kStateItemList) {
				close();
			} else {
				drawItemList();
			}
			return true;
		}

		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		switch (_state) {
		case kStateItemList:    return dispatchItemList(key);
		case kStateItemOptions: return dispatchItemOptions(key);
		default: break;
		}
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		int x = event.mouse.x;
		int y = event.mouse.y;
		switch (_state) {
		case kStateItemList: {
			// DOS item rows: "1. Credits N" drawn at (8, 16) inside the
			// window -> screen (64, 144), row height 8. The button rect
			// is dynamic (rw_state_inventory.c:148-152) -- for our single
			// Credits item it covers (64, 136 + 1*8) ~= (64, 144)..(223, 151).
			int rowTop = kWindowY + 16;
			int rowBottom = rowTop + 7;
			if (x >= 64 && x <= 223 && y >= rowTop && y <= rowBottom)
				return dispatchItemList('1');
			// "exit" at (96, 176)..(127, 183) from g_inv_buttons.exit.
			if (x >= 96 && x <= 127 && y >= 176 && y <= 183)
				return dispatchItemList('x');
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
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header row centered-ish at (56, 8) inside the window. DOS uses
	// neuro_window_draw_string("Items", 56, 8) at rw_state_inventory.c:87.
	drawString("Items", kWindowWidthPx, kWindowHeightPx, 56, 8, pixels);

	// Item row 0: Credits. DOS format "1.  Credits %d" at (8, 16).
	Common::String row = Common::String::format("1.  Credits %d", _scene->cash());
	drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);

	// Footer: "exit" at window-relative (40, 48) per rw_state_inventory.c:159.
	drawString("exit", kWindowWidthPx, kWindowHeightPx, 40, 48, pixels);

	pushSprite();
}

void Inventory::drawItemOptions() {
	_state = kStateItemOptions;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header: item name (or "Credits N" for the virtual credits entry).
	Common::String header;
	if (_selectedItemCode == kItemCodeCredits)
		header = Common::String::format("Credits %d", _scene->cash());
	else
		header = "(item)";
	drawString(header.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	// DOS lays out X / Operate / Discard / (Give) rows vertically with
	// 8-px spacing starting at window-relative (8, 16).
	drawString("X. Exit",           kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);
	drawString("O. Operate Item",   kWindowWidthPx, kWindowHeightPx, 8, 24, pixels);
	drawString("D. Discard Item",   kWindowWidthPx, kWindowHeightPx, 8, 32, pixels);
	drawString("G. Give Credits",   kWindowWidthPx, kWindowHeightPx, 8, 40, pixels);

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
	case '1': // Credits item
		_selectedItemCode = kItemCodeCredits;
		drawItemOptions();
		return true;
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
		// DOS behaviour for credits: no-op message + return to list.
		// Phase 2 will dispatch item-specific op codes for real items.
		drawItemList();
		return true;
	case 'd':
		// Discard: for credits DOS just bounces back to the item list
		// (rw_state_inventory.c:565-568). Real items will go through a
		// Y/N confirmation in Phase 2.
		drawItemList();
		return true;
	case 'g':
		if (_selectedItemCode == kItemCodeCredits) {
			memset(_amount, 0, sizeof(_amount));
			_amountLen = 0;
			drawGiveCredits();
		} else {
			// Give (other items) -- Phase 4. Bounce back for now.
			drawItemList();
		}
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

} // End of namespace Neuromancer
