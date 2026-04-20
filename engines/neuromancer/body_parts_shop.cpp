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

#include "neuromancer/body_parts_shop.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/music_player.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

enum {
	kWindowX        = 48,
	kWindowY        = 128,
	kWindowWidthPx  = 216,
	kWindowHeightPx = 64,
	kWindowPackedW  = kWindowWidthPx / 2,
	kWindowBytes    = kWindowPackedW * kWindowHeightPx
};

// Body-parts catalog, transcribed from Reuromancer's
// rw_state_body_parts_shop.c:24-68. Indexed 0..19. All four arrays are
// parallel (same index = same body part).
const char *const kPartNames[20] = {
	"Heart",             "Eyes (2)",
	"Lungs (2)",         "Stomach",
	"Liver",             "Kidneys (2)",
	"Gall Bladder",      "Pancreas",
	"Legs (2)",          "Arms (2)",
	"Tongue",            "Larynx",
	"Nose",              "Ears (2)",
	"Intestine (large)", "Intestine (small)",
	"Spleen",            "Bone Marrow",
	"Spinal Fluid",      "Appendix"
};

const uint16 kBuyPrices[20] = {
	12000, 10000, 6000, 3000,
	 2500,  2100, 2100, 1000,
	  600,   600,  300,  300, 300,
	  200,   100,  100,   90,  90, 60, 6
};

const uint16 kSellPrices[20] = {
	6000, 5000, 3000, 1500,
	1250, 1050, 1050,  500,
	 300,  300,  150,  150, 150,
	 100,   50,   50,   45,  45, 30, 3
};

const uint16 kDiscountedPrices[20] = {
	6600, 6500, 3300, 1650,
	1375, 1155, 1155,  550,
	 330,  330,  165,  165, 165,
	 110,   78,   78,   55,  55, 33, 3
};

const uint16 kConDamage[20] = {
	200, 150, 150, 100,
	 75,  75,  75,  75, 50, 50,
	 25,  25,  25,  25, 10, 10,
	 10,  10,  10,  10
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

BodyPartsShop::BodyPartsShop(NeuromancerEngine *engine, RealWorldScene *scene)
	: _engine(engine),
	  _scene(scene),
	  _active(false),
	  _state(kStateMainMenu),
	  _sellMode(true),
	  _discounted(false),
	  _firstListed(0),
	  _messageClosesShop(false),
	  _didTransaction(false) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
}

void BodyPartsShop::openSell() {
	_active = true;
	_sellMode = true;
	_discounted = false;
	_firstListed = 0;
	_didTransaction = false;
	drawMainMenu();
	debugC(1, kDebugGeneral, "BodyPartsShop: opened (sell)");
}

void BodyPartsShop::openBuy(bool discounted) {
	_active = true;
	_sellMode = false;
	_discounted = discounted;
	_firstListed = 0;
	_didTransaction = false;
	drawMainMenu();
	debugC(1, kDebugGeneral, "BodyPartsShop: opened (buy, discount=%d)", discounted ? 1 : 0);
}

void BodyPartsShop::close() {
	_active = false;
	_engine->spriteChain()->clearSprite(kLayerPaxWindow);
	// DOS writes g_4bae.x4c82 = sold_something / bought_something so
	// the calling script (Modern Bods NPC) can branch on "did the
	// player actually trade anything?" (rw_state_body_parts_shop.c:189
	// and :258). 0x4C82 is a uint16 slot but DOS only ever writes 0/1.
	NeuroVM *vm = _engine->vm();
	if (vm) {
		vm->writeVar8(0x4C82, _didTransaction ? 1 : 0);
		vm->writeVar8(0x4C83, 0);
		vm->resume();
	}
	_didTransaction = false;
	debugC(1, kDebugGeneral, "BodyPartsShop: closed");
}

void BodyPartsShop::update() {}

bool BodyPartsShop::handleEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	const char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));

	if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
		if (_state == kStateMainMenu) {
			close();
		} else {
			drawMainMenu();
		}
		return true;
	}

	if (_state == kStateMessage) {
		if (_messageClosesShop) {
			close();
		} else {
			drawMainMenu();
		}
		return true;
	}

	if (_state == kStateMainMenu)
		return dispatchMainMenu(key);
	if (_state == kStatePartList)
		return dispatchPartList(key);
	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void BodyPartsShop::drawWindowFrame() {
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
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

void BodyPartsShop::drawMainMenu() {
	_state = kStateMainMenu;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	const char *header = _sellMode ? "SELL PARTS - Modern Bods"
	                               : (_discounted ? "BUY PARTS - clearance rate"
	                                              : "BUY PARTS - full price");
	drawString(header,                  kWindowWidthPx, kWindowHeightPx, 8, 4,  pixels);
	drawString("L. Show parts list",    kWindowWidthPx, kWindowHeightPx, 8, 20, pixels);
	drawString("X. Exit shop",          kWindowWidthPx, kWindowHeightPx, 8, 32, pixels);
	drawString("(placeholder panel --", kWindowWidthPx, kWindowHeightPx, 8, 48, pixels);
	pushSprite();
}

void BodyPartsShop::drawPartList() {
	_state = kStatePartList;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	Common::String head = _sellMode ? "SELL 1..4 / M more / X exit"
	                                : "BUY  1..4 / M more / X exit";
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 4, pixels);

	for (int i = 0; i < 4; ++i) {
		int idx = (_firstListed + i) % 20;
		uint16 price = _sellMode ? kSellPrices[idx]
		                         : (_discounted ? kDiscountedPrices[idx]
		                                        : kBuyPrices[idx]);
		Common::String row = Common::String::format("%d %-18s %5u",
		                                            i + 1,
		                                            kPartNames[idx],
		                                            (unsigned)price);
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 16 + i * 10, pixels);
	}
	pushSprite();
}

void BodyPartsShop::drawMessage(const char *text, bool closesOnKey) {
	_state = kStateMessage;
	_messageClosesShop = closesOnKey;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	Common::String body = Common::String::format(
		"%s\n\nPress any key.",
		text ? text : "");
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);
	pushSprite();
}

void BodyPartsShop::pushSprite() {
	_engine->spriteChain()->addSprite(kLayerPaxWindow, kWindowX, kWindowY,
	                                  _sprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Dispatch
// -------------------------------------------------------------------------

bool BodyPartsShop::dispatchMainMenu(char key) {
	switch (key) {
	case 'x':
		close();
		return true;
	case 'l':
		_firstListed = 0;
		drawPartList();
		return true;
	default:
		return false;
	}
}

// DOS plays track 6 for transaction failures ("can't afford", "already
// sold", etc.) and track 11 for successes, called from
// rw_state_body_parts_shop.c:158-180,217-230.
static void playShopSfx(NeuromancerEngine *engine, bool success) {
	if (MusicPlayer *mp = engine->music())
		mp->setTrack(success ? 11 : 6);
}

bool BodyPartsShop::dispatchPartList(char key) {
	if (key >= '1' && key <= '4') {
		int idx = (_firstListed + (key - '1')) % 20;
		NeuroVM *vm = _engine->vm();
		// Sold-parts bitset: 3 bytes at DSEG 0x4C84 (byte[part>>3] has
		// bit (0x80 >> (part & 7)) set if that part has been sold).
		// Matches DOS g_4bae.sold_body_parts_bitstring (data.h:288).
		const uint16 kVarBitsetBase = 0x4C84;
		uint16 byteOff = kVarBitsetBase + (uint16)(idx >> 3);
		uint8  mask    = (uint8)(0x80 >> (idx & 7));
		uint8  cur     = vm ? vm->readVar8(byteOff) : 0;
		bool   sold    = (cur & mask) != 0;

		Common::String body;
		if (_sellMode) {
			if (sold) {
				playShopSfx(_engine, false);
				body = Common::String::format("%s already sold.", kPartNames[idx]);
			} else {
				if (vm) vm->writeVar8(byteOff, (uint8)(cur | mask));
				_scene->setCash(_scene->cash() + (int32)kSellPrices[idx]);
				int16 newCon = (int16)(_scene->constitution() - kConDamage[idx]);
				if (newCon < 0) newCon = 0;
				_scene->setConstitution(newCon);
				_didTransaction = true;
				playShopSfx(_engine, true);
				body = Common::String::format("Sold %s for %u.",
				                              kPartNames[idx],
				                              (unsigned)kSellPrices[idx]);
			}
		} else {
			uint16 price = _discounted ? kDiscountedPrices[idx] : kBuyPrices[idx];
			if (!sold) {
				playShopSfx(_engine, false);
				body = Common::String::format("You already own a healthy %s.",
				                              kPartNames[idx]);
			} else if (_scene->cash() < (int32)price) {
				playShopSfx(_engine, false);
				body = "Not enough credits.";
			} else {
				if (vm) vm->writeVar8(byteOff, (uint8)(cur & (uint8)~mask));
				_scene->setCash(_scene->cash() - (int32)price);
				_scene->setConstitution((int16)(_scene->constitution() + kConDamage[idx]));
				_didTransaction = true;
				playShopSfx(_engine, true);
				body = Common::String::format("Bought %s for %u.",
				                              kPartNames[idx], (unsigned)price);
			}
		}
		drawMessage(body.c_str(), /*closesOnKey=*/false);
		return true;
	}
	if (key == 'm') {
		advancePage();
		return true;
	}
	if (key == 'x') {
		close();
		return true;
	}
	return false;
}

void BodyPartsShop::advancePage() {
	_firstListed = (_firstListed + 4) % 20;
	drawPartList();
}

// -------------------------------------------------------------------------
// Static table accessors
// -------------------------------------------------------------------------

const char *BodyPartsShop::partName(int index) {
	if (index < 0 || index >= 20) return "?";
	return kPartNames[index];
}

uint16 BodyPartsShop::buyPrice(int index, bool discounted) {
	if (index < 0 || index >= 20) return 0;
	return discounted ? kDiscountedPrices[index] : kBuyPrices[index];
}

uint16 BodyPartsShop::sellPrice(int index) {
	if (index < 0 || index >= 20) return 0;
	return kSellPrices[index];
}

uint16 BodyPartsShop::constitutionDamage(int index) {
	if (index < 0 || index >= 20) return 0;
	return kConDamage[index];
}

} // End of namespace Neuromancer
