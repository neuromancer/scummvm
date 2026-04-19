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

#ifndef NEUROMANCER_PAX_H
#define NEUROMANCER_PAX_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "neuromancer/text_scroller.h"

namespace Neuromancer {

class NeuromancerEngine;
class RealWorldScene;

// PAX ("Public Access eXchange"): the in-game computer terminal Ratz has
// mounted on the Chatsubo wall. Covers main menu, banking, news, bulletin
// board, and user info -- mirrors DOS rw_state_pax.c (Reuromancer).
//
// The DOS implementation is a sub-state inside the real-world scene
// (returning RWS_PAX from update_pax()); we follow the same shape by making
// PAX a submodule owned by RealWorldScene. When the player hits the PAX
// icon, the scene delegates update/handleEvent to this class until close().
class Pax {
public:
	explicit Pax(NeuromancerEngine *engine, RealWorldScene *scene);
	~Pax() = default;

	// True between open() and close(). While active the owning scene should
	// route input + rendering to us.
	bool isActive() const { return _active; }

	// Paint the PAX window + main menu and take over input. Caller is
	// responsible for freezing the VM / character controller.
	void open();

	// Drop back to the real-world scene. Clears our sprite layer so the
	// scene's redraw restores the PIC underneath.
	void close();

	// Called once per frame while active.
	void update();

	// Called for every event while active. Returns true if the event was
	// consumed (so the scene shouldn't also process it).
	bool handleEvent(const Common::Event &event);

private:
	// DOS pax_state_t subset. Ordering + values match rw_state_pax.c:14 as
	// closely as practical so future save/load round-trips are easy.
	enum State {
		kStateMainMenu         = 1,   // DOS PS_MAIN_MENU
		kStateBanking          = 5,   // DOS PS_BANKING
		kStateBankDownload     = 6,   // DOS PS_BANK_DOWNLOAD
		kStateBankUpload       = 7,   // DOS PS_BANK_UPLOAD
		kStateBankTransactions = 8,   // DOS PS_BANK_TRANSACTIONS_WFI
		kStateNewsMenu         = 10,  // DOS PS_NEWS_MENU
		kStateNewsArticle      = 11,  // DOS PS_NEWS (article body displayed)
		kStateBoardMenu        = 20,  // DOS PS_BOARD_MENU
		kStateBoardViewMenu    = 21,  // DOS PS_BOARD_VIEW_MENU
		kStateBoardArticle     = 22,  // DOS PS_BOARD_MSG
		kStateBoardSendAddr    = 23,  // DOS PS_BOARD_SEND_MSG_ADDRESSEE
		kStateBoardSendBody    = 24,  // DOS PS_BOARD_SEND_MSG_TEXT
		kStateBoardSendConfirm = 25,  // DOS PS_BOARD_SEND_MSG_ACCEPT
		kStateUserInfo         = 30,  // DOS PS_USER_INFO (scrolled body)
		kStateNotImplemented   = 100  // local placeholder for unported submenus
	};

	// --- rendering ---
	void drawWindowFrame();
	void drawMainMenu();
	void drawPlaceholder(const char *body);
	void drawBanking();
	void drawBankPrompt(bool download);
	void drawBankTransactions();
	void drawAmountEditor();       // redraw the amount field only
	void drawNewsMenu();
	void drawNewsArticle(int entryIndex);
	void drawBoardMenu();
	void drawBoardViewMenu();
	void drawBoardArticle(int entryIndex);
	void drawBoardSend();           // composites current addr / body editor
	void drawBoardSendConfirm();
	void drawUserInfo();

	// Render the currently-scrolling long-form view (news article / board
	// message / user info) into the PAX sprite. Draws the header, the
	// scroller's currently-revealed lines, and a state-appropriate footer
	// (MORE / continue / press any key).
	void redrawScrollView();

	void pushSprite(); // composite _sprite into the engine's sprite chain

	// --- button dispatch ---
	bool dispatchMainMenu(char code);
	bool dispatchBanking(char code);
	bool dispatchNewsMenu(char code);
	bool dispatchBoardMenu(char code);
	bool dispatchBoardViewMenu(char code);
	bool dispatchBoardSendConfirm(char code);

	// --- amount entry ---
	void resetAmountEntry();
	bool handleAmountKey(const Common::Event &event); // true if consumed
	void commitAmount(bool download);

	// --- news helpers ---
	// Ensure NEWS.BIH is loaded into _newsData.
	void loadNewsResource();
	// Build the list of entry indices (into the news header table) that
	// are currently visible, honoring the flag-dispatch modes described
	// in DOS pax_info_menu_prepare_list.
	int  buildNewsVisibleList(int *out, int maxOut);
	// Article body pointer: walk `_newsData` past `entryIndex` null-
	// terminated strings. Returns nullptr if we ran off the end.
	const char *newsArticleText(int entryIndex) const;

	// --- board helpers ---
	void loadBoardResource();
	int  buildBoardVisibleList(int *out, int maxOut);
	const char *boardArticleText(int entryIndex) const;
	// Handle a keystroke while the send-editor is open (state is one of
	// kStateBoardSendAddr / kStateBoardSendBody). Returns true if consumed.
	bool handleBoardSendKey(const Common::Event &event);
	// Run the DOS pax_send_mgs payoff: if addressee == "armitage" (case-
	// insensitive) AND body contains "056306118" AND msg_to_armitage_sent
	// is still 0, credit 10000 into bank_account and flag the send.
	void attemptArmitagePayoff();

	NeuromancerEngine *_engine;
	RealWorldScene   *_scene;

	bool  _active;
	State _state;

	// Packed 4bpp sprite for the PAX window (320 x 104, at screen y = 4).
	// Starts with an ImhHeader so it can be installed via SpriteChain.
	Common::Array<byte> _sprite;

	// Amount-entry buffer for download / upload prompts. Up to 8 digits plus
	// null terminator; DOS pax bank uses the same 8-char width.
	char _amount[9];
	int  _amountLen;

	// Cached NEWS.BIH contents (null-terminated article bodies, packed).
	// Loaded lazily on first entry into the news menu.
	Common::Array<byte> _newsData;
	bool _newsLoaded;

	// News menu paging state. Tracks the starting offset into the visible
	// list so the "more" button cycles through pages of 5.
	int _newsPageStart; // first entry index in the current page
	// Visible entry indices for the currently-displayed page (up to 5).
	int _newsPageEntries[5];
	int _newsPageCount;

	// Article scroller state. One scroller handles all three long-form
	// views (news article / board message / user info). The active view
	// id is captured in _scrollViewKind so update() knows which header
	// to redraw on tick.
	TextScroller _scroller;
	enum ScrollViewKind { kScrollNone, kScrollNews, kScrollBoard, kScrollUserInfo };
	ScrollViewKind _scrollKind;
	int _scrollArticleEntry; // news / board entry index being displayed
	int _lastScrollLines;    // last visibleLines() snapshot, for dirty check
	TextScroller::State _lastScrollState;

	// Cached PAXBBS.BIH contents (null-terminated message bodies).
	Common::Array<byte> _boardData;
	bool _boardLoaded;

	// Board menu paging (view list -- separate from article scroller).
	int _boardPageStart;
	int _boardPageEntries[5];
	int _boardPageCount;

	// Send-message editor state. `_sendAddr` is the 12-char addressee,
	// `_sendBody` is up to ~116 chars (one line for now).
	char _sendAddr[13];
	int  _sendAddrLen;
	char _sendBody[120];
	int  _sendBodyLen;

	// FTUSER.TXH cache. Header = first null-terminated string (drawn at
	// the top of the window), body = remainder fed to the scroller.
	Common::Array<byte> _userInfoData;
	bool _userInfoLoaded;
	Common::String _userInfoHeader;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_PAX_H
