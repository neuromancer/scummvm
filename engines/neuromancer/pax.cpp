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

#include "neuromancer/pax.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/resource.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// PAX window geometry. Matches DOS g_open_frame_data's final frame
// (rw_state_pax.c:1079): 320x104 at (0, 4), covering the level PIC area
// and the text widgets but leaving the bottom UI panel (icons / status)
// visible.
enum {
	kWindowX        = 0,
	kWindowY        = 4,
	kWindowWidthPx  = 320,
	kWindowHeightPx = 104,
	kWindowPackedW  = kWindowWidthPx / 2,
	kWindowBytes    = kWindowPackedW * kWindowHeightPx
};

// Menu buttons are 8 px tall rows spanning most of the window width.
// Matches neuro_pax_buttons_t g_pax_buttons in data.c:141. Each entry is
// { x1, y1, x2, y2, code, label }.
struct MenuButton {
	int16 left, top, right, bottom;
	int   code;   // passed back to the dispatcher
	char  hotkey; // lowercase key alias
};

// Button rows are in DOS screen coordinates (the PAX window is at y=4, so
// these rects are already absolute for hit-testing).
static const MenuButton kMainMenuButtons[] = {
	{ 0x08, 0x0C, 0x0137, 0x13, 0x00, 'x' }, // Exit
	{ 0x08, 0x14, 0x0137, 0x1B, 0x01, '1' }, // User info
	{ 0x08, 0x1C, 0x0137, 0x23, 0x02, '2' }, // Banking
	{ 0x08, 0x24, 0x0137, 0x2B, 0x03, '3' }, // News
	{ 0x08, 0x2C, 0x0137, 0x33, 0x04, '4' }, // Board
};

// Banking sub-menu buttons. From neuro_pax_banking_buttons g_pax_banking_buttons
// in data.c:150-155. Rows 52-83, covering the lower half of the PAX window.
static const MenuButton kBankingButtons[] = {
	{ 0x08, 0x34, 0x0137, 0x3B, 0x00, 'x' }, // Exit to main
	{ 0x08, 0x3C, 0x0137, 0x43, 0x01, 'd' }, // Download credits
	{ 0x08, 0x44, 0x0137, 0x4B, 0x02, 'u' }, // Upload credits
	{ 0x08, 0x4C, 0x0137, 0x53, 0x03, 't' }, // Transaction record
};

// Info-menu buttons (exit + more) shared between News and Board view.
// From g_pax_info_menu_buttons in data.c:158. Position: lower-centre,
// y=92 (screen) -- in window-relative y=88.
static const MenuButton kInfoMenuButtons[] = {
	{ 0x78, 0x5C, 0x97, 0x63, 0x0A, 'x' },
	{ 0xA0, 0x5C, 0xBF, 0x63, 0x0B, 'm' }
};

// PAX news header table. Transcribed verbatim from DOS data.c:5-27
// (g_seg004.pax_news). Entry 20 is the 0x03 sentinel that halts scanning.
struct PaxNewsEntry {
	uint16 addr;   // DSEG byte to test (or 0 for unconditional)
	uint8  val;    // comparison value
	uint8  flag;   // low nibble = test mode, bit 7 = stamp-date-today
	char   date[9];
	char   subject[28];
};
static const PaxNewsEntry kNewsTable[] = {
	{ 0x0000, 0x00, 0x00, "11/16/58", "BAR FOOD DECLARED FATAL  " },
	{ 0x0000, 0x00, 0x00, "11/16/58", "COWBOY DISAPPEARS        " },
	{ 0x0000, 0x00, 0x00, "11/16/58", "News In Brief            " },
	{ 0x4BC9, 0x01, 0x01, "11/17/58", "NASA AND FUJI DO BUSINESS" },
	{ 0x4BC9, 0x01, 0x01, "11/17/58", "News In Brief            " },
	{ 0x4BC9, 0x02, 0x01, "11/18/58", "JUSTICE DEFENDS DEFENDERS" },
	{ 0x4BC9, 0x03, 0x01, "11/18/58", "News In Brief            " },
	{ 0x4BC9, 0x03, 0x01, "11/18/58", "FRIED COWBOY FOUND       " },
	{ 0x4BC9, 0x03, 0x01, "11/19/58", "DR. TIMOTHY LEARY AT 138 " },
	{ 0x4BC9, 0x03, 0x01, "11/19/58", "DISMEMBERED HAND FOUND   " },
	{ 0x4BC9, 0x03, 0x01, "11/19/58", "News In Brief            " },
	{ 0x4C1F, 0x00, 0x82, "11/00/58", "PERVERT NETTED IN SWEEP  " },
	{ 0x4C59, 0x00, 0x82, "11/00/58", "CRIMINAL HITS CHIBA CITY " },
	{ 0x4C5A, 0x00, 0x82, "11/00/58", "CHIBA CITY HITS CRIMINAL " },
	{ 0x4C23, 0x00, 0x82, "11/00/58", "MAAS BIOLABS BURGLARIZED " },
	{ 0x4C27, 0x00, 0x82, "11/00/58", "INDUSTRIAL SPY NABBED    " },
	{ 0x4C29, 0x00, 0x82, "11/00/58", "JUSTICE BLINDED          " },
	{ 0x4C25, 0x00, 0x82, "11/00/58", "SENSE/NET RAIDED AGAIN   " },
	{ 0x4C2B, 0x00, 0x82, "11/00/58", "VAGRANT PAYS HOTEL BILL  " },
	{ 0x4C2D, 0x00, 0x82, "11/00/58", "BANK LOSES MONEY         " },
	{ 0x0000, 0x00, 0x03, "",         ""                          },
};
static const int kNewsEntryCount = (int)(sizeof(kNewsTable) / sizeof(kNewsTable[0]));

// Body text wrap + pagination parameters for the article view. Article body
// occupies rows 3..11 of the PAX window (9 rows) at 38 chars per line.
enum {
	kArticleColumns = 38,
	kArticleRows    = 9,
	kArticleLinesPerPage = kArticleRows
};

// Board menu buttons. Rows 28-35 (X), 36-43 (V), 44-51 (S). From DOS
// g_pax_board_menu_buttons (data.c:164-168).
static const MenuButton kBoardMenuButtons[] = {
	{ 0x08, 0x1C, 0x0137, 0x23, 0x00, 'x' }, // Exit to main
	{ 0x08, 0x24, 0x0137, 0x2B, 0x01, 'v' }, // View
	{ 0x08, 0x2C, 0x0137, 0x33, 0x02, 's' }, // Send
};

// Board send-confirm buttons. From DOS g_pax_board_send_msg_buttons
// (data.c:171-174). Two small rects inside the prompt area.
static const MenuButton kSendConfirmButtons[] = {
	{ 0xA0, 0x1C, 0xA7, 0x23, 0x00, 'y' },
	{ 0xB0, 0x1C, 0xB7, 0x23, 0x01, 'n' }
};

// Board header table. Transcribed from DOS data.c:28-51 (g_seg004.pax_board_msg).
// The 0x01 bytes in the DOS .to column are a substitution token for "the
// player's name" -- we expand that at draw time using scene->playerName().
struct PaxBoardEntry {
	uint16 addr;
	uint8  val;
	uint8  flag;
	char   date[9];
	char   to[13];   // 0x01 in byte 0 means "<player name>"
	char   from[14];
};
static const PaxBoardEntry kBoardTable[] = {
	{ 0x0000, 0x00, 0x00, "11/14/58", "All",             "SysOp"       },
	{ 0x0000, 0x00, 0x00, "11/14/58", { 0x01, 0 },       "Matt Shaw"   },
	{ 0x0000, 0x00, 0x00, "11/14/58", { 0x01, 0 },       "FFargo"      },
	{ 0x0000, 0x00, 0x00, "11/14/58", { 0x01, 0 },       "Shin"        },
	{ 0x0000, 0x00, 0x00, "11/14/58", { 0x01, 0 },       "Crazy Edo"   },
	{ 0x0000, 0x00, 0x00, "11/15/58", { 0x01, 0 },       "Matt Shaw"   },
	{ 0x0000, 0x00, 0x00, "11/15/58", { 0x01, 0 },       "Bosch"       },
	{ 0x0000, 0x00, 0x00, "11/15/58", { 0x01, 0 },       "Emp. Norton" },
	{ 0x0000, 0x00, 0x00, "11/16/58", "Ratz",            "Red Snake"   },
	{ 0x0000, 0x00, 0x00, "11/16/58", "All",             "Interplay"   },
	{ 0x0000, 0x00, 0x00, "11/16/58", "All",             "Armitage"    },
	{ 0x0000, 0x00, 0x00, "11/16/58", "All",             "Hitachi"     },
	{ 0x4BC9, 0x01, 0x01, "11/17/58", { 0x01, 0 },       "Emp. Norton" },
	{ 0x4BC9, 0x01, 0x01, "11/17/58", "All",             "CFM"         },
	{ 0x4BC9, 0x01, 0x01, "11/17/58", "All",             "IRS"         },
	{ 0x4BC9, 0x02, 0x01, "11/18/58", "Larry",           "Modern Bob"  },
	{ 0x4BC9, 0x02, 0x01, "11/18/58", "Crazy Edo",       "Wakizashi"   },
	{ 0x4BC9, 0x03, 0x01, "11/19/58", "Wakizashi",       "Crazy Edo"   },
	{ 0x4BF1, 0x00, 0x82, "11/16/58", { 0x01, 0 },       "Bosch"       },
	{ 0x4C21, 0x00, 0x82, "11/16/58", { 0x01, 0 },       "Armitage"    },
	{ 0x4C25, 0x00, 0x82, "11/16/58", "All",             "Sense/Net"   },
	{ 0x0000, 0x00, 0x03, "",         "",                ""            },
};
static const int kBoardEntryCount = (int)(sizeof(kBoardTable) / sizeof(kBoardTable[0]));

// DSEG addresses for pax_send_mgs bookkeeping (DOS rw_state_pax.c:76-103).
// Addresses come from data.h:247 (msg_to_armitage_sent) and 275 (x4c5c).
enum {
	kVarMsgToArmitageSent = 0x4C1F,
	kVarX4C5C             = 0x4C5C
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

Pax::Pax(NeuromancerEngine *engine, RealWorldScene *scene)
	: _engine(engine),
	  _scene(scene),
	  _active(false),
	  _state(kStateMainMenu),
	  _amountLen(0),
	  _newsLoaded(false),
	  _newsPageStart(0),
	  _newsPageCount(0),
	  _scrollKind(kScrollNone),
	  _scrollArticleEntry(0),
	  _lastScrollLines(0),
	  _lastScrollState(TextScroller::kIdle),
	  _boardLoaded(false),
	  _boardPageStart(0),
	  _boardPageCount(0),
	  _sendAddrLen(0),
	  _sendBodyLen(0),
	  _userInfoLoaded(false) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
	memset(_amount, 0, sizeof(_amount));
	memset(_newsPageEntries, 0, sizeof(_newsPageEntries));
	memset(_boardPageEntries, 0, sizeof(_boardPageEntries));
	memset(_sendAddr, 0, sizeof(_sendAddr));
	memset(_sendBody, 0, sizeof(_sendBody));
}

void Pax::open() {
	_active = true;
	_state  = kStateMainMenu;

	// Hide character / bubble / scroll widgets while the PAX panel is up.
	// The level PIC on kLayerLevelBg stays installed underneath -- our
	// opaque PAX sprite simply covers it until close().
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerCharacter);
	chain->clearSprite(kLayerDialogBubble);
	chain->clearSprite(kLayerNeuroMenu);

	drawMainMenu();
	debugC(1, kDebugGeneral, "Pax: opened");
}

void Pax::close() {
	_active = false;
	_engine->spriteChain()->clearSprite(kLayerPaxWindow);
	debugC(1, kDebugGeneral, "Pax: closed");
}

void Pax::update() {
	// Drive the article scroller when one of the long-form views is up.
	// Redraws only when visible-line count or state actually changes so
	// the sprite-chain composition isn't re-unpacking each frame.
	if (_scrollKind == kScrollNone)
		return;

	TextScroller::State st = _scroller.tick(g_system->getMillis());
	int lines = _scroller.visibleLines();
	if (st != _lastScrollState || lines != _lastScrollLines) {
		_lastScrollState = st;
		_lastScrollLines = lines;
		redrawScrollView();
	}
}

bool Pax::handleEvent(const Common::Event &event) {
	// Amount-entry screens take priority and consume keys (digits / enter /
	// backspace / escape).
	if (_state == kStateBankDownload || _state == kStateBankUpload) {
		if (handleAmountKey(event))
			return true;
	}

	// Transactions screen: any key / click returns to the banking menu.
	// Matches DOS PS_BANK_TRANSACTIONS_WFI -> handle_pax_wait_for_input.
	if (_state == kStateBankTransactions) {
		if (event.type == Common::EVENT_KEYDOWN ||
		    event.type == Common::EVENT_LBUTTONDOWN) {
			drawBanking();
			return true;
		}
	}

	// Unified handling for the three long-form scrolling views (news
	// article, board message, user info). While the scroller is running
	// we absorb input (except 'x' which skips); a paused "screen full"
	// scroller advances on any key; a completed scroller returns to the
	// parent menu. Mirrors DOS PS_*_WFI / PS_*_END_WFI transitions.
	if (_state == kStateNewsArticle || _state == kStateBoardArticle ||
	    _state == kStateUserInfo) {
		bool pressed = (event.type == Common::EVENT_KEYDOWN ||
		                event.type == Common::EVENT_LBUTTONDOWN);
		if (!pressed)
			return false;

		bool exitNow = false;
		if (event.type == Common::EVENT_KEYDOWN) {
			Common::KeyCode kc = event.kbd.keycode;
			if (kc == Common::KEYCODE_ESCAPE ||
			    event.kbd.ascii == 'x' || event.kbd.ascii == 'X')
				exitNow = true;
		}

		auto backToParent = [&]() {
			switch (_scrollKind) {
			case kScrollNews:     drawNewsMenu();      break;
			case kScrollBoard:    drawBoardViewMenu(); break;
			case kScrollUserInfo: drawMainMenu();      break;
			default:              drawMainMenu();      break;
			}
			_scrollKind = kScrollNone;
		};

		TextScroller::State st = _scroller.state();
		if (exitNow) {
			backToParent();
			return true;
		}
		if (st == TextScroller::kWaitingForInput) {
			_scroller.acknowledge();
			redrawScrollView();
			return true;
		}
		if (st == TextScroller::kComplete) {
			backToParent();
			return true;
		}
		// st == kRunning: let it finish; consume the input so stray
		// keys don't fall through into the menu dispatch below.
		return true;
	}

	// Send-message editor: text entry consumes almost all keys.
	if (_state == kStateBoardSendAddr || _state == kStateBoardSendBody) {
		if (handleBoardSendKey(event))
			return true;
	}

	if (event.type == Common::EVENT_KEYDOWN) {
		// Escape closes the PAX from top-level menus. DOS only does this
		// for explicit 'X' buttons, but the extra shortcut is harmless.
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			if (_state == kStateMainMenu)
				close();
			else
				drawMainMenu();
			return true;
		}
		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		if (_state == kStateMainMenu) {
			if (dispatchMainMenu(key))
				return true;
		} else if (_state == kStateBanking) {
			if (dispatchBanking(key))
				return true;
		} else if (_state == kStateNewsMenu) {
			if (dispatchNewsMenu(key))
				return true;
		} else if (_state == kStateBoardMenu) {
			if (dispatchBoardMenu(key))
				return true;
		} else if (_state == kStateBoardViewMenu) {
			if (dispatchBoardViewMenu(key))
				return true;
		} else if (_state == kStateBoardSendConfirm) {
			if (dispatchBoardSendConfirm(key))
				return true;
		} else if (_state == kStateNotImplemented) {
			// Any key returns to the main menu on a placeholder screen.
			drawMainMenu();
			return true;
		}
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		int x = event.mouse.x;
		int y = event.mouse.y;
		if (_state == kStateMainMenu) {
			for (uint i = 0; i < sizeof(kMainMenuButtons) / sizeof(kMainMenuButtons[0]); i++) {
				const MenuButton &b = kMainMenuButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchMainMenu(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateBanking) {
			for (uint i = 0; i < sizeof(kBankingButtons) / sizeof(kBankingButtons[0]); i++) {
				const MenuButton &b = kBankingButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchBanking(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateNewsMenu) {
			// Rows: items at y=44..44+count*8, exit + more at y=92..99.
			for (int i = 0; i < _newsPageCount; i++) {
				int top    = 44 + i * 8;
				int bottom = top + 7;
				if (x >= 8 && x <= 311 && y >= top && y <= bottom) {
					drawNewsArticle(_newsPageEntries[i]);
					return true;
				}
			}
			for (uint i = 0; i < sizeof(kInfoMenuButtons) / sizeof(kInfoMenuButtons[0]); i++) {
				const MenuButton &b = kInfoMenuButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchNewsMenu(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateBoardMenu) {
			for (uint i = 0; i < sizeof(kBoardMenuButtons) / sizeof(kBoardMenuButtons[0]); i++) {
				const MenuButton &b = kBoardMenuButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchBoardMenu(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateBoardViewMenu) {
			for (int i = 0; i < _boardPageCount; i++) {
				int top = 44 + i * 8;
				int bottom = top + 7;
				if (x >= 8 && x <= 311 && y >= top && y <= bottom) {
					drawBoardArticle(_boardPageEntries[i]);
					return true;
				}
			}
			for (uint i = 0; i < sizeof(kInfoMenuButtons) / sizeof(kInfoMenuButtons[0]); i++) {
				const MenuButton &b = kInfoMenuButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchBoardViewMenu(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateBoardSendConfirm) {
			for (uint i = 0; i < sizeof(kSendConfirmButtons) / sizeof(kSendConfirmButtons[0]); i++) {
				const MenuButton &b = kSendConfirmButtons[i];
				if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
					dispatchBoardSendConfirm(b.hotkey);
					return true;
				}
			}
		} else if (_state == kStateNotImplemented) {
			drawMainMenu();
			return true;
		}
	}

	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void Pax::drawWindowFrame() {
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	// DOS neuro_window_clear() for NWM_PAX calls LibNeuroRoutines'
	// build_text_frame (drawing.c:4): solid black top/bottom rows, white
	// interior with a thin black column on each side. drawString then
	// paints BLACK glyphs on the WHITE body (see font.cpp:166 kFontPixels
	// -- 0xFF is "background" and 0x00 is "ink"), so the final image is
	// black text on a white sheet with a thin black outline. This is also
	// what the Chatsubo PAX actually looks like in the original game.
	const int packedW = kWindowPackedW;
	for (int row = 0; row < (int)kWindowHeightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == (int)kWindowHeightPx - 1) {
			memset(line, 0x00, packedW); // edge row: fully black
		} else {
			memset(line, 0xFF, packedW);  // interior: white
			line[0]           = 0x0F;     // left column pixel = black
			line[packedW - 1] = 0xF0;     // right column pixel = black
		}
	}
}

void Pax::drawMainMenu() {
	_state = kStateMainMenu;
	drawWindowFrame();

	// Menu layout from pax_main_menu() in rw_state_pax.c:469. DOS uses
	// neuro_window_set_draw_string_offt(8, 8) which is relative to the
	// window origin (0, 4) -- equivalent to (8, 8) inside our sprite buffer.
	static const char kMenu[] =
		"X. Exit System\n"
		"1. First Time PAX User Info.\n"
		"2. Access Banking Interlink\n"
		"3. Night City News\n"
		"4. Bulletin Board\n"
		"\n\n\n"
		"        choose a function";

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	drawString(kMenu, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

void Pax::drawPlaceholder(const char *body) {
	_state = kStateNotImplemented;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	drawString(body, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

void Pax::pushSprite() {
	_engine->spriteChain()->addSprite(kLayerPaxWindow, kWindowX, kWindowY,
	                                  _sprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Button dispatch
// -------------------------------------------------------------------------

bool Pax::dispatchMainMenu(char code) {
	switch (code) {
	case 'x':
		close();
		return true;
	case '1':
		drawUserInfo();
		return true;
	case '2':
		drawBanking();
		return true;
	case '3':
		drawNewsMenu();
		return true;
	case '4':
		drawBoardMenu();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// Banking
// -------------------------------------------------------------------------

// DOS banking screen (rw_state_pax.c:413). Header + inline fields for the
// player's name (row 3, col 9), "chip" credits (row 4, col 10), and the
// bank balance (row 4, col 29). The "BAMA id" is a fixed decoration string.
void Pax::drawBanking() {
	_state = kStateBanking;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Compose the static frame + dynamic fields into one string, then hand
	// it to drawString. '%' formatting lives in the host side so we don't
	// touch the font renderer.
	Common::String body = Common::String::format(
		"  First Orbital Bank of Switzerland\n"
		"\n"
		"  name: %-8s BAMA id = 056306118\n"
		"  chip = %-8d account = %d\n"
		"\n"
		"X. Exit To Main\n"
		"D. Download credits\n"
		"U. Upload credits\n"
		"T. Transaction record",
		_scene->playerName().c_str(),
		_scene->cash(),
		_scene->bankAccount());

	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

// Enter the amount-entry screen for Download or Upload. The actual painting
// happens in drawAmountEditor() which is re-invoked on every keystroke.
void Pax::drawBankPrompt(bool download) {
	_state = download ? kStateBankDownload : kStateBankUpload;
	resetAmountEntry();
}

void Pax::drawBankTransactions() {
	_state = kStateBankTransactions;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// DOS header at row 6 (y=48 when window is at y=4 -> 44 in sprite).
	// We place all rows relative to the sprite top, so y=48 below.
	static const char kHead[] =
		"Transaction record\n"
		"\n"
		"day      type       amount\n";
	drawString(kHead, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	// Walk the ring buffer starting at index `bankTxIndex()`, oldest first.
	for (int i = 0; i < 4; i++) {
		int slot = (_scene->bankTxIndex() + i) & 3;
		uint8 op = _scene->bankTxOp(slot);
		uint32 amount = _scene->bankTxAmount(slot);
		const char *kind = nullptr;
		switch (op >> 6) {
		case 0: kind = "Upload";     break;
		case 1: kind = "Download";   break;
		case 2: kind = "TransferIn"; break;
		case 3: kind = "Fined";      break;
		default: kind = "";          break;
		}
		Common::String row;
		if (op == 0 && amount == 0) {
			row = "  (empty)";
		} else {
			// Day field is the low 6 bits of op -- DOS format is just the
			// raw day offset for now (real build_date_string is mm/dd/yy).
			row = Common::String::format("%02d       %-10s %8u",
			                             (op & 0x3F), kind, amount);
		}
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx,
		           8, 32 + i * 8, pixels);
	}

	drawString("Press any key to return.",
	           kWindowWidthPx, kWindowHeightPx, 8, 80, pixels);
	pushSprite();
}

// Re-render the prompt screen with the current amount buffer. Simpler than
// patching the sprite in place, and the keystroke rate is low enough that
// the full redraw is cheap.
void Pax::drawAmountEditor() {
	const bool download = (_state == kStateBankDownload);
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	Common::String head = Common::String::format(
		"  First Orbital Bank of Switzerland\n"
		"\n"
		"  name: %-8s BAMA id = 056306118\n"
		"  chip = %-8d account = %d\n"
		"\n"
		"%s credits from your BAMA account.\n"
		"Enter amount : %s<",
		_scene->playerName().c_str(),
		_scene->cash(),
		_scene->bankAccount(),
		download ? "Download" : "Upload",
		_amount);
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

bool Pax::dispatchBanking(char code) {
	switch (code) {
	case 'x':
		drawMainMenu();
		return true;
	case 'd':
		drawBankPrompt(/*download=*/true);
		return true;
	case 'u':
		drawBankPrompt(/*download=*/false);
		return true;
	case 't':
		drawBankTransactions();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// Amount entry
// -------------------------------------------------------------------------

void Pax::resetAmountEntry() {
	memset(_amount, 0, sizeof(_amount));
	_amountLen = 0;
	drawAmountEditor();
}

bool Pax::handleAmountKey(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	Common::KeyCode kc = event.kbd.keycode;
	if (kc == Common::KEYCODE_ESCAPE) {
		drawBanking();
		return true;
	}
	if (kc == Common::KEYCODE_RETURN || kc == Common::KEYCODE_KP_ENTER) {
		commitAmount(_state == kStateBankDownload);
		return true;
	}
	if (kc == Common::KEYCODE_BACKSPACE) {
		if (_amountLen > 0) {
			_amount[--_amountLen] = 0;
			drawAmountEditor();
		}
		return true;
	}
	// Accept only ASCII digits. DOS sfHandleTextInput is digits-only when
	// called with allowedFlags == 0,1 (see rw_state_pax.c:780).
	uint16 ch = event.kbd.ascii;
	if (ch >= '0' && ch <= '9' && _amountLen < 8) {
		_amount[_amountLen++] = (char)ch;
		_amount[_amountLen] = 0;
		drawAmountEditor();
	}
	return true;
}

void Pax::commitAmount(bool download) {
	uint32 val = 0;
	for (int i = 0; i < _amountLen; i++)
		val = val * 10 + (uint32)(_amount[i] - '0');

	if (val == 0) {
		drawBanking();
		return;
	}

	if (download) {
		if ((int32)val > _scene->bankAccount()) {
			// Overdraft: bounce back without transacting, matching DOS.
			drawBanking();
			return;
		}
		_scene->setBankAccount(_scene->bankAccount() - (int32)val);
		_scene->setCash(_scene->cash() + (int32)val);
	} else {
		if ((int32)val > _scene->cash()) {
			drawBanking();
			return;
		}
		_scene->setCash(_scene->cash() - (int32)val);
		_scene->setBankAccount(_scene->bankAccount() + (int32)val);
	}

	// Record the transaction in the ring buffer. `op` high 2 bits = kind,
	// low 6 bits = day-of-month (matches DOS on_pax_bank_account_operation_
	// text_enter at rw_state_pax.c:819).
	uint8 opKind = download ? 0x40 : 0x00;
	uint8 op     = opKind | (uint8)(_scene->dateDay() & 0x3F);
	uint8 idx    = _scene->bankTxIndex();
	_scene->setBankTxRecord(idx, op, val);
	_scene->setBankTxIndex((idx + 1) & 3);

	debugC(1, kDebugGeneral, "Pax: %s %u -> cash=%d bank=%d",
	       download ? "download" : "upload",
	       val, _scene->cash(), _scene->bankAccount());

	drawBanking();
}

// -------------------------------------------------------------------------
// News
// -------------------------------------------------------------------------

void Pax::loadNewsResource() {
	if (_newsLoaded)
		return;
	ResourceManager *res = _engine->resources();
	_newsData.resize(16384); // NEWS.BIH decompresses to ~5 KB; give us slack.
	uint32 sz = res->load("NEWS.BIH", _newsData.data());
	debugC(1, kDebugResource, "Pax: NEWS.BIH -> %u bytes", sz);
	_newsLoaded = (sz > 0);
}

// Walk the news header table and return the indices that pass the flag
// filter, honoring modes 0/1/2 from DOS pax_info_menu_prepare_list (the
// date-stamp bit 0x80 is ignored -- our headers are static, and the dates
// shown are the designed ones). Stops on mode-3 (sentinel).
int Pax::buildNewsVisibleList(int *out, int maxOut) {
	int count = 0;
	NeuroVM *vm = _engine->vm();
	for (int i = 0; i < kNewsEntryCount; i++) {
		const PaxNewsEntry &e = kNewsTable[i];
		uint8 mode = e.flag & 0x0F;
		if (mode == 0x03) break; // end sentinel
		bool visible = false;
		switch (mode) {
		case 0x00:
			visible = true;
			break;
		case 0x01: {
			uint8 v = vm->readVar8(e.addr);
			visible = (v > e.val);
			break;
		}
		case 0x02: {
			uint8 v = vm->readVar8(e.addr);
			visible = (v != e.val);
			break;
		}
		default:
			break;
		}
		if (visible && count < maxOut)
			out[count++] = i;
	}
	return count;
}

const char *Pax::newsArticleText(int entryIndex) const {
	if (!_newsLoaded || _newsData.empty())
		return nullptr;
	const byte *p = _newsData.data();
	const byte *end = p + _newsData.size();
	for (int i = 0; i < entryIndex && p < end; i++) {
		while (p < end && *p != 0) p++;
		if (p < end) p++; // skip terminator
	}
	return (p < end) ? (const char *)p : nullptr;
}

void Pax::drawNewsMenu() {
	loadNewsResource();
	_state = kStateNewsMenu;
	drawWindowFrame();

	// Collect visible entries, then slice the current page.
	int visible[32];
	int visibleCount = buildNewsVisibleList(visible, 32);

	if (_newsPageStart >= visibleCount) _newsPageStart = 0;

	_newsPageCount = visibleCount - _newsPageStart;
	if (_newsPageCount > 5) _newsPageCount = 5;
	for (int i = 0; i < _newsPageCount; i++)
		_newsPageEntries[i] = visible[_newsPageStart + i];

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header (matches DOS pax_news): title + column headers, then 5 item
	// rows starting at y=40 (sprite-relative).
	static const char kHead[] =
		"      Night City News\n"
		"\n"
		"   date     subject";
	drawString(kHead, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	for (int i = 0; i < 5; i++) {
		int rowY = 40 + i * 8;
		Common::String row;
		if (i < _newsPageCount) {
			const PaxNewsEntry &e = kNewsTable[_newsPageEntries[i]];
			row = Common::String::format("%d. %8s %s", i + 1, e.date, e.subject);
		}
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx, 8, rowY, pixels);
	}

	// exit [more] line, centered-ish at (120, 88) matching DOS.
	const char *footer = (visibleCount > _newsPageStart + _newsPageCount) ?
		"exit more" : "exit";
	drawString(footer, kWindowWidthPx, kWindowHeightPx, 120, 88, pixels);

	pushSprite();
}

// drawNewsArticle(entryIndex): entryIndex >= 0 starts a new article (resets
// paging). entryIndex < 0 redraws the current article at its current page
// (used for page advance).
void Pax::drawNewsArticle(int entryIndex) {
	if (entryIndex >= 0) {
		const char *body = newsArticleText(entryIndex);
		_scroller.start(body ? body : "(article unavailable)",
		                kArticleColumns, kArticleRows, /*frameCapMs=*/120);
		_scrollKind         = kScrollNews;
		_scrollArticleEntry = entryIndex;
		_lastScrollLines    = -1;
		_lastScrollState    = TextScroller::kIdle;
		_state              = kStateNewsArticle;
	}
	redrawScrollView();
}

bool Pax::dispatchNewsMenu(char code) {
	switch (code) {
	case 'x':
		_newsPageStart = 0;
		drawMainMenu();
		return true;
	case 'm': {
		// Advance to next page, wrapping. DOS pax_info_menu advances
		// items_listed by the displayed count and modulo-wraps.
		int visible[32];
		int visibleCount = buildNewsVisibleList(visible, 32);
		if (visibleCount == 0) return true;
		_newsPageStart = (_newsPageStart + _newsPageCount) % visibleCount;
		drawNewsMenu();
		return true;
	}
	case '1': case '2': case '3': case '4': case '5': {
		int idx = code - '1';
		if (idx < _newsPageCount) {
			drawNewsArticle(_newsPageEntries[idx]);
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// BBS board
// -------------------------------------------------------------------------

void Pax::loadBoardResource() {
	if (_boardLoaded)
		return;
	ResourceManager *res = _engine->resources();
	_boardData.resize(16384);
	uint32 sz = res->load("PAXBBS.BIH", _boardData.data());
	debugC(1, kDebugResource, "Pax: PAXBBS.BIH -> %u bytes", sz);
	_boardLoaded = (sz > 0);
}

int Pax::buildBoardVisibleList(int *out, int maxOut) {
	int count = 0;
	NeuroVM *vm = _engine->vm();
	for (int i = 0; i < kBoardEntryCount; i++) {
		const PaxBoardEntry &e = kBoardTable[i];
		uint8 mode = e.flag & 0x0F;
		if (mode == 0x03) break;
		bool visible = false;
		switch (mode) {
		case 0x00: visible = true; break;
		case 0x01: visible = (vm->readVar8(e.addr) > e.val);  break;
		case 0x02: visible = (vm->readVar8(e.addr) != e.val); break;
		default:   break;
		}
		if (visible && count < maxOut)
			out[count++] = i;
	}
	return count;
}

const char *Pax::boardArticleText(int entryIndex) const {
	if (!_boardLoaded || _boardData.empty())
		return nullptr;
	const byte *p = _boardData.data();
	const byte *end = p + _boardData.size();
	for (int i = 0; i < entryIndex && p < end; i++) {
		while (p < end && *p != 0) p++;
		if (p < end) p++;
	}
	return (p < end) ? (const char *)p : nullptr;
}

void Pax::drawBoardMenu() {
	_state = kStateBoardMenu;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	static const char kMenu[] =
		"Bulletin Board\n"
		"\n"
		"X. Exit To Main\n"
		"V. View Messages\n"
		"S. Send Message";
	drawString(kMenu, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

void Pax::drawBoardViewMenu() {
	loadBoardResource();
	_state = kStateBoardViewMenu;
	drawWindowFrame();

	int visible[32];
	int visibleCount = buildBoardVisibleList(visible, 32);
	if (_boardPageStart >= visibleCount) _boardPageStart = 0;

	_boardPageCount = visibleCount - _boardPageStart;
	if (_boardPageCount > 5) _boardPageCount = 5;
	for (int i = 0; i < _boardPageCount; i++)
		_boardPageEntries[i] = visible[_boardPageStart + i];

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	static const char kHead[] =
		"Bulletin Board\n"
		"\n"
		"      date    to           from";
	drawString(kHead, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	for (int i = 0; i < 5; i++) {
		int rowY = 40 + i * 8;
		Common::String row;
		if (i < _boardPageCount) {
			const PaxBoardEntry &e = kBoardTable[_boardPageEntries[i]];
			// Expand 0x01 -> player name (DOS convention).
			const char *to = ((byte)e.to[0] == 0x01) ? _scene->playerName().c_str() : e.to;
			row = Common::String::format("%d. %8s %-12s %-13s",
			                             i + 1, e.date, to, e.from);
		}
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx, 8, rowY, pixels);
	}

	const char *footer = (visibleCount > _boardPageStart + _boardPageCount) ?
		"exit more" : "exit";
	drawString(footer, kWindowWidthPx, kWindowHeightPx, 120, 88, pixels);

	pushSprite();
}

void Pax::drawBoardArticle(int entryIndex) {
	if (entryIndex >= 0) {
		const char *body = boardArticleText(entryIndex);
		_scroller.start(body ? body : "(message unavailable)",
		                kArticleColumns, kArticleRows, /*frameCapMs=*/120);
		_scrollKind         = kScrollBoard;
		_scrollArticleEntry = entryIndex;
		_lastScrollLines    = -1;
		_lastScrollState    = TextScroller::kIdle;
		_state              = kStateBoardArticle;
	}
	redrawScrollView();
}

void Pax::drawBoardSend() {
	// Compose a single screen showing the addressee and body editor fields.
	// The DOS version redraws only the changed line per keystroke; we redo
	// the whole window each frame for simplicity.
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String head = "     Send Message\n\nPress ESC when done\n\n";
	Common::String addrField = _sendAddr;
	while (addrField.size() < 12) addrField += ' ';
	Common::String bodyField = _sendBody;

	Common::String addrCursor = (_state == kStateBoardSendAddr) ? "<" : " ";
	Common::String bodyCursor = (_state == kStateBoardSendBody) ? "<" : " ";

	Common::String screen = head;
	screen += "to   : " + addrField + addrCursor + "\n";
	screen += "body : " + bodyField + bodyCursor;

	drawString(screen.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	drawString("[Enter] advance   [Esc] send    [X-esc back]",
	           kWindowWidthPx, kWindowHeightPx, 8, 96, pixels);
	pushSprite();
}

void Pax::drawBoardSendConfirm() {
	_state = kStateBoardSendConfirm;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String head = "     Send Message\n\n";
	head += "to   : " + Common::String(_sendAddr) + "\n";
	head += "body : " + Common::String(_sendBody) + "\n\n";
	head += "Send this message?   [Y]  [N]";
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

bool Pax::handleBoardSendKey(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	Common::KeyCode kc = event.kbd.keycode;

	if (_state == kStateBoardSendAddr) {
		if (kc == Common::KEYCODE_ESCAPE) {
			drawBoardMenu();
			return true;
		}
		if (kc == Common::KEYCODE_RETURN || kc == Common::KEYCODE_KP_ENTER) {
			_state = kStateBoardSendBody;
			drawBoardSend();
			return true;
		}
		if (kc == Common::KEYCODE_BACKSPACE) {
			if (_sendAddrLen > 0) {
				_sendAddr[--_sendAddrLen] = 0;
				drawBoardSend();
			}
			return true;
		}
		uint16 ch = event.kbd.ascii;
		if (ch >= 0x20 && ch < 0x7F && _sendAddrLen < 12) {
			_sendAddr[_sendAddrLen++] = (char)ch;
			_sendAddr[_sendAddrLen] = 0;
			drawBoardSend();
		}
		return true;
	}

	// kStateBoardSendBody
	if (kc == Common::KEYCODE_ESCAPE) {
		drawBoardSendConfirm();
		return true;
	}
	if (kc == Common::KEYCODE_RETURN || kc == Common::KEYCODE_KP_ENTER) {
		// Accept as newline inside body for multi-line entries; wrap
		// pressure handled later.
		if (_sendBodyLen < (int)sizeof(_sendBody) - 1) {
			_sendBody[_sendBodyLen++] = '\n';
			_sendBody[_sendBodyLen] = 0;
			drawBoardSend();
		}
		return true;
	}
	if (kc == Common::KEYCODE_BACKSPACE) {
		if (_sendBodyLen > 0) {
			_sendBody[--_sendBodyLen] = 0;
			drawBoardSend();
		}
		return true;
	}
	uint16 ch = event.kbd.ascii;
	if (ch >= 0x20 && ch < 0x7F && _sendBodyLen < (int)sizeof(_sendBody) - 1) {
		_sendBody[_sendBodyLen++] = (char)ch;
		_sendBody[_sendBodyLen] = 0;
		drawBoardSend();
	}
	return true;
}

// Armitage payoff. DOS rw_state_pax.c:76-103: if addressee is "armitage"
// (case-insensitive) AND body contains "056306118" (Case's BAMA id) AND
// the flag hasn't already been set, credit 10,000 to the bank account
// and mark the message as sent.
void Pax::attemptArmitagePayoff() {
	// Trim + lowercase the addressee.
	Common::String addr;
	for (int i = 0; i < _sendAddrLen; i++) {
		char c = _sendAddr[i];
		if (c == ' ' && addr.empty()) continue;
		addr += (char)tolower((byte)c);
	}
	while (!addr.empty() && addr.lastChar() == ' ')
		addr.deleteLastChar();

	if (addr != "armitage")
		return;

	if (!strstr(_sendBody, "056306118"))
		return;

	NeuroVM *vm = _engine->vm();
	if (vm->readVar16(kVarMsgToArmitageSent) != 0)
		return; // already paid

	vm->writeVar16(kVarMsgToArmitageSent, 1);
	vm->writeVar8(kVarX4C5C, 0);

	// Record the transfer-in transaction. op = date | 0x80 (TransferIn).
	uint8 idx = _scene->bankTxIndex();
	uint8 op  = (uint8)(_scene->dateDay() & 0x3F) | 0x80;
	_scene->setBankTxRecord(idx, op, 10000);
	_scene->setBankTxIndex((idx + 1) & 3);
	_scene->setBankAccount(_scene->bankAccount() + 10000);

	debugC(1, kDebugGeneral, "Pax: Armitage payoff -> +10000 to bank");
}

bool Pax::dispatchBoardMenu(char code) {
	switch (code) {
	case 'x':
		drawMainMenu();
		return true;
	case 'v':
		_boardPageStart = 0;
		drawBoardViewMenu();
		return true;
	case 's':
		memset(_sendAddr, 0, sizeof(_sendAddr));
		memset(_sendBody, 0, sizeof(_sendBody));
		_sendAddrLen = 0;
		_sendBodyLen = 0;
		_state = kStateBoardSendAddr;
		drawBoardSend();
		return true;
	default:
		return false;
	}
}

bool Pax::dispatchBoardViewMenu(char code) {
	switch (code) {
	case 'x':
		_boardPageStart = 0;
		drawBoardMenu();
		return true;
	case 'm': {
		int visible[32];
		int visibleCount = buildBoardVisibleList(visible, 32);
		if (visibleCount == 0) return true;
		_boardPageStart = (_boardPageStart + _boardPageCount) % visibleCount;
		drawBoardViewMenu();
		return true;
	}
	case '1': case '2': case '3': case '4': case '5': {
		int idx = code - '1';
		if (idx < _boardPageCount) {
			drawBoardArticle(_boardPageEntries[idx]);
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

bool Pax::dispatchBoardSendConfirm(char code) {
	switch (code) {
	case 'y':
		attemptArmitagePayoff();
		drawBoardMenu();
		return true;
	case 'n':
		drawBoardMenu();
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------
// User info (FTUSER.TXH)
// -------------------------------------------------------------------------

void Pax::drawUserInfo() {
	if (!_userInfoLoaded) {
		ResourceManager *res = _engine->resources();
		_userInfoData.resize(4096);
		uint32 sz = res->load("FTUSER.TXH", _userInfoData.data());
		debugC(1, kDebugResource, "Pax: FTUSER.TXH -> %u bytes", sz);

		if (sz > 0) {
			// DOS layout: first null-terminated string = header (drawn
			// statically above the scroll area), second = body (scrolled).
			const char *hdr = (const char *)_userInfoData.data();
			_userInfoHeader = hdr;
		} else {
			_userInfoHeader = "First-time user info";
		}
		_userInfoLoaded = true;
	}

	const char *body = "";
	if (_userInfoData.size() > 0) {
		const char *hdr = (const char *)_userInfoData.data();
		body = hdr + strlen(hdr) + 1;
	}

	_scroller.start(body, kArticleColumns, kArticleRows, /*frameCapMs=*/120);
	_scrollKind      = kScrollUserInfo;
	_lastScrollLines = -1;
	_lastScrollState = TextScroller::kIdle;
	_state           = kStateUserInfo;
	redrawScrollView();
}

// Paint the PAX window with the current scroller snapshot. Shared by all
// three long-form views -- the header text and the "back" target differ,
// but the body layout (lines starting at y=24) and footer are identical.
void Pax::redrawScrollView() {
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header line.
	Common::String head;
	switch (_scrollKind) {
	case kScrollNews: {
		const PaxNewsEntry &e = kNewsTable[_scrollArticleEntry];
		head = Common::String::format("%s %s", e.date, e.subject);
		break;
	}
	case kScrollBoard: {
		const PaxBoardEntry &e = kBoardTable[_scrollArticleEntry];
		const char *to = ((byte)e.to[0] == 0x01) ? _scene->playerName().c_str() : e.to;
		head = Common::String::format("%s  to:%-12s  from:%s", e.date, to, e.from);
		break;
	}
	case kScrollUserInfo:
		head = _userInfoHeader;
		break;
	default:
		break;
	}
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);

	// Body: paint each currently-revealed line at its fixed row. Lines
	// that haven't been revealed yet remain on the white background, so
	// the reader sees the text fill in top-to-bottom.
	for (int i = 0; i < _scroller.visibleLines(); i++) {
		const Common::String &line = _scroller.lineAt(i);
		int rowY = 24 + i * 8;
		drawString(line.c_str(), kWindowWidthPx, kWindowHeightPx, 8, rowY, pixels);
	}

	// Footer hint keyed off scroller state.
	const char *hint = "[Space] back   [X] exit";
	switch (_scroller.state()) {
	case TextScroller::kRunning:
		hint = "(revealing)           [X] skip";
		break;
	case TextScroller::kWaitingForInput:
		hint = "[Space] more    [X] exit";
		break;
	case TextScroller::kComplete:
		hint = "[Space] back    [X] exit";
		break;
	default:
		break;
	}
	drawString(hint, kWindowWidthPx, kWindowHeightPx, 8, 96, pixels);
	pushSprite();
}

} // End of namespace Neuromancer
