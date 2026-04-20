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

#ifndef NEUROMANCER_BODY_PARTS_SHOP_H
#define NEUROMANCER_BODY_PARTS_SHOP_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"

namespace Neuromancer {

class NeuromancerEngine;
class RealWorldScene;

// Dr. Chrome's body-parts shop. Mirrors the DOS sub-scene in
// Reuromancer/NeuromancerWin64/rw_state_body_parts_shop.c, which
// itself ports the DOS RWS_BODY_PARTS_SHOP state.
//
// Two modes, selected by BIH-script callbacks:
//   - Sell (neuro_cb cmd 8): player offloads organs for cash, paying a
//     constitution cost per part. Triggered by the Modern Bods NPC script.
//   - Buy (neuro_cb cmd 9): player buys back / buys new parts. Can carry
//     a discount multiplier (arg1 of the callback).
//
// The sub-module opens modally on top of the real-world scene, blocks the
// VM while active, and resumes it on close. Current implementation is a
// minimum-viable panel so the BIH callback no longer warns; the full
// inventory / pricing / consititution math comes in follow-up iterations.
class BodyPartsShop {
public:
	explicit BodyPartsShop(NeuromancerEngine *engine, RealWorldScene *scene);
	~BodyPartsShop() = default;

	bool isActive() const { return _active; }

	// Sell mode: g_body_shop_op = 1 in DOS. Called from BihScript
	// dispatchCallback (cmd 8) when a level script initiates the sale.
	void openSell();

	// Buy mode with optional discount multiplier (DOS passes the
	// discount word as arg1 of neuro_cb cmd 9; `discounted == true`
	// selects the premium price table).
	void openBuy(bool discounted);

	void close();
	void update();
	bool handleEvent(const Common::Event &event);

	// Static accessors for part-index metadata so callers (scripts,
	// inventory summaries) can describe a part without duplicating the
	// table. `index` is 0..19.
	static const char *partName(int index);
	static uint16 buyPrice(int index, bool discounted);
	static uint16 sellPrice(int index);
	static uint16 constitutionDamage(int index);

private:
	enum State {
		kStateMainMenu = 0, // "1 Sell / 2 Buy / X Exit"
		kStatePartList = 1, // 20 parts paged 4-at-a-time
		kStateMessage  = 2  // "<part> sold" / "not enough credits" etc.
	};

	void drawWindowFrame();
	void drawMainMenu();
	void drawPartList();
	void drawMessage(const char *text, bool closesOnKey);
	void pushSprite();

	bool dispatchMainMenu(char key);
	bool dispatchPartList(char key);

	// Advance the paging index (mod 20) when the player presses 'm'.
	void advancePage();

	NeuromancerEngine *_engine;
	RealWorldScene   *_scene;
	bool  _active;
	State _state;

	// true = sell mode, false = buy mode. Matches DOS g_body_shop_op (1
	// is sell, 0 is buy -- inverted-looking but the DOS enum is just a
	// counter).
	bool  _sellMode;
	bool  _discounted;

	int   _firstListed; // paging cursor, 0..19 in steps of 4
	bool  _messageClosesShop;

	// Accumulated across the session: did the player complete any
	// transaction? DOS stores this as x4c82 (same scratch slot that
	// CB_CMD_HAS_ITEM uses for its match index) on shop close so the
	// calling script can branch on "got any trade at all?".
	bool  _didTransaction;

	// Packed 4bpp sprite for the shop window. Sized like the ROM/Skills
	// panel so the on-screen look matches the rest of the engine.
	Common::Array<byte> _sprite;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_BODY_PARTS_SHOP_H
