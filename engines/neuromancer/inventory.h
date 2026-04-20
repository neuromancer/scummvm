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

#ifndef NEUROMANCER_INVENTORY_H
#define NEUROMANCER_INVENTORY_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Neuromancer {

class NeuromancerEngine;
class RealWorldScene;

// Inventory sub-module for the real-world scene. Mirrors DOS
// rw_state_inventory.c: a sub-state within the real-world scene that pops
// up a small window at (56, 128) with the player's carried items, lets
// them Operate / Discard / Give / Erase each one, and resumes the VM on
// close. For now we cover just the "Credits" entry and the Give Credits
// flow, which is what the Ratz-payment script in R1.BIH is waiting on
// (sets active_item = 0x7F and cash_withdrawal = <amount>).
class Inventory {
public:
	explicit Inventory(NeuromancerEngine *engine, RealWorldScene *scene);
	~Inventory() = default;

	bool isActive() const { return _active; }
	void open();
	void close();
	void update();
	bool handleEvent(const Common::Event &event);

	// Returns the DOS item-operation byte for a given item code, or
	// 0xFF for out-of-range codes. The byte encodes category bits:
	//   0x80 = hardware (picks items[] array; else software[] array)
	//   0x30 = cyberspace/database mode bits
	//   0x0F = sub-index into the dispatch vtable
	// Scripts use this to know which inventory bucket holds an item
	// without duplicating the 128-byte dispatch table.
	static uint8 itemOp(uint8 itemCode);

	// Display name for an item code, or "(item N)" for out-of-range.
	// Credits (0x7F) returns "Credits" without amount -- callers that
	// want the amount should read cash() themselves.
	static const char *itemName(uint8 itemCode);

private:
	// DOS inventory_state_t subset. Later phases will add kStateDiscard,
	// kStateSoftwareList, etc.
	enum State {
		kStateItemList       = 0,  // DOS IS_ITEM_LIST
		kStateItemOptions    = 2,  // DOS IS_ITEM_OPTIONS
		kStateDiscard        = 3,  // DOS IS_DISCARD_ITEM (Y/N confirm)
		kStateSoftwareList   = 5,  // DOS IS_ERASE_SOFTWARE_LIST
		kStateSoftwareErase  = 6,  // DOS IS_ERASE_SOFTWARE (Y/N confirm)
		kStateGiveCredits    = 7,  // DOS IS_GIVE_CREDITS
		kStateGiveItem       = 8,  // DOS IS_GIVE_ITEM (Y/N confirm for NPC)
		kStateMessage        = 9   // DOS IS_WFI_AND_CONTINUE / _AND_CLOSE
	};

	// --- rendering ---
	void drawWindowFrame();
	void drawItemList();
	void drawItemOptions();
	void drawDiscardConfirm();
	void drawSoftwareList();
	void drawSoftwareEraseConfirm();
	void drawGiveCredits();
	void drawGiveItemConfirm();
	// Generic one-line message screen reached by several Operate paths
	// (e.g. "Nothing happens.", "No jack here.", "Gas mask is on."). On
	// acknowledge (_messageClosesInv == true) the inventory closes;
	// otherwise we bounce back to the item list.
	void drawMessage(const char *text, bool closesInventory);
	void pushSprite();

	// --- dispatch ---
	bool dispatchItemList(char code);
	bool dispatchItemOptions(char code);
	bool dispatchDiscardConfirm(char code);
	bool dispatchSoftwareList(char code);
	bool dispatchSoftwareEraseConfirm(char code);
	bool dispatchGiveItemConfirm(char code);

	// --- Give Credits amount entry ---
	bool handleGiveCreditsKey(const Common::Event &event);
	void commitGiveCredits();

	// --- item helpers ---
	// Count non-empty slots in the current inventory (items, not software).
	int countItems() const;
	// Rebuild the _pageEntries mapping from visible inventory slots plus
	// the virtual Credits entry. Returns the number of entries populated.
	int rebuildPage();
	// Return a printable name for the given item code. Credits (0x7F)
	// gets "Credits N"; other codes index into the engine item-name table.
	Common::String itemDisplayName(uint8 code) const;

	// Run the item's "Operate" effect. Mirrors DOS inventory_operate_item
	// (rw_state_inventory.c:306): looks up the item's op byte in
	// kItemOperations[128] and branches on hardware / skill-chip /
	// gas-mask / cyberspace / database / jackable categories.
	void operateSelectedItem();

	NeuromancerEngine *_engine;
	RealWorldScene   *_scene;

	bool  _active;
	State _state;

	uint8 _selectedItemCode;  // 0x7F = credits, else an item code 0..103
	int   _selectedSlot;       // slot index in _scene->itemSlots(), or -1

	// Amount-entry buffer for Give Credits (DOS uses 8 chars + NUL).
	char _amount[9];
	int  _amountLen;

	// Packed 4bpp sprite for the inventory window. Sized at construction
	// to DOS's (231-56+1) x (191-128+1) = 176 x 64.
	Common::Array<byte> _sprite;

	// Item list pagination. DOS shows up to 4 rows per page, with Credits
	// taking the first row of the first page. _pageStart is the slot
	// cursor from which we build the next page (0, 4, 8, ...). _pageCount
	// is the number of visible rows on the current page; _pageSlots maps
	// each visible row index to its underlying inventory slot (-1 for the
	// virtual Credits row).
	int _pageStart;
	int _pageCount;
	int _pageSlots[4];
	uint8 _pageCodes[4];

	// When true, rebuildPage + drawItemList / drawSoftwareList switch to
	// the software inventory. Reached via item-options Erase on CyberEyes.
	bool _viewingSoftware;

	// If true, dismissing the current message screen closes the inventory
	// entirely (DOS IS_WFI_AND_CLOSE); otherwise we return to the item
	// list (DOS IS_WFI_AND_CONTINUE).
	bool _messageClosesInv;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_INVENTORY_H
