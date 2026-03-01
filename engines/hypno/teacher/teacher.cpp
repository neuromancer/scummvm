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
 */

#include "hypno/teacher/teacher.h"
#include "hypno/teacher/demo/Handler1.h"
#include "hypno/teacher/demo/Handler2.h"
#include "hypno/teacher/demo/Handler4.h"
#include "hypno/teacher/demo/Handler6.h"
#include "hypno/teacher/demo/Handler8.h"
#include "hypno/teacher/demo/SCI_Dialog.h"
#include "hypno/teacher/demo/Handler14.h"
#include "hypno/teacher/demo/SCI_AfterSchoolMenu.h"
#include "hypno/teacher/demo/SCI_SearchScreen.h"
#include "hypno/teacher/demo/Handler12.h"
#include "hypno/teacher/demo/Handler13.h"
#include "hypno/teacher/demo/Handler15.h"
#include "hypno/teacher/demo/Handler16.h"
#include "hypno/teacher/SC_Combat1.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/util.h"

namespace Hypno {

TeacherEngine::TeacherEngine(OSystem *syst, const ADGameDescription *gd) : HypnoEngine(syst, gd),
	_strings(nullptr), _flagManager(nullptr), _currentHandler(nullptr), _frameDelay(84), _shouldQuitLoop(false),
	_peter(nullptr), _susan(nullptr), _duncan(nullptr), _selectedCharacter(nullptr),
	_font(nullptr), _mouseControl(nullptr) {
	_screenW = 640;
	_screenH = 480;
	for (int i = 0; i < 4; ++i) _gameState[i] = nullptr;
}

TeacherEngine::~TeacherEngine() {
	delete _strings;
	delete _flagManager;
	for (int i = 0; i < 4; ++i) delete _gameState[i];

	delete _peter;
	delete _susan;
	delete _duncan;
	delete _font;
	delete _mouseControl;

	for (auto h : _eventList) delete h;
	while (!_messageQueue.empty()) delete _messageQueue.pop();
}

void TeacherEngine::initializePath(const Common::FSNode &gamePath) {
	HypnoEngine::initializePath(gamePath);

	// The Teacher game often has data in a DATA subdirectory
	SearchMan.addSubDirectoryMatching(gamePath, "DATA", 0, 5);
	SearchMan.addSubDirectoryMatching(gamePath, "mainmenu", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "demo", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "cine", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "mis", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "audio", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "elements", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "puzzle1", 0, 2);
	SearchMan.addSubDirectoryMatching(gamePath, "rat1", 0, 3);
	SearchMan.addSubDirectoryMatching(gamePath, "rat2", 0, 3);
	SearchMan.addSubDirectoryMatching(gamePath, "rat3", 0, 3);
}

void TeacherEngine::runIntros(Videos &videos) {
	for (Videos::iterator it = videos.begin(); it != videos.end(); ++it) {
		playVideo(*it);
	}

	bool skip = false;
	Common::Event event;
	while (!shouldQuit() && !skip) {
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN) {
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE || event.kbd.keycode == Common::KEYCODE_SPACE)
					skip = true;
			} else if (event.type == Common::EVENT_LBUTTONDOWN) {
				skip = true;
			}
		}

		if (skip) {
			for (Videos::iterator it = videos.begin(); it != videos.end(); ++it) {
				if (it->decoder)
					skipVideo(*it);
			}
			videos.clear();
			break;
		}

		bool playing = false;
		for (Videos::iterator it = videos.begin(); it != videos.end(); ++it) {
			if (it->decoder) {
				if (it->decoder->endOfVideo()) {
					it->decoder->close();
					delete it->decoder;
					it->decoder = nullptr;
				} else {
					playing = true;
					if (it->decoder->needsUpdate()) {
						updateScreen(*it);
						drawScreen();
					}
				}
			}
		}

		if (!playing)
			break;

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	for (Videos::iterator it = videos.begin(); it != videos.end(); ++it) {
		if (it->decoder) {
			delete it->decoder;
			it->decoder = nullptr;
		}
	}
}

Common::Error TeacherEngine::run() {
	Graphics::ModeList modes;
	modes.push_back(Graphics::Mode(640, 480));
	modes.push_back(Graphics::Mode(320, 200));
	initGraphicsModes(modes);

	_screenW = 640;
	_screenH = 480;

	_pixelFormat = Graphics::PixelFormat::createFormatCLUT8();
	initGraphics(_screenW, _screenH, &_pixelFormat);

	_compositeSurface = new Graphics::Surface();
	_compositeSurface->create(_screenW, _screenH, _pixelFormat);

	loadAssets();

	// Original: g_Mouse_00436978->DrawCursor() (main.cpp line 133)
	// Mouse cursor is initialized by loadAssetsDemo() via MouseControl.
	// Fall back to default cursor if MouseControl failed to load.
	if (!_mouseControl || _mouseControl->getCursorState() < 0) {
		defaultCursor();
	}

	_shouldQuitLoop = false;

	// Original: SC_Message_Send(8, 1, 1, 1, 5, 0, 0, 0, 0, 0)
	// Sends to Handler 8 (cinematic player) with data=1 (cine0001.smk)
	// Handler 8 plays the cinematic, then transitions to Handler 1 (demo splash)
	sendMessage(8, 1, 1, 1, 5);

	while (!_shouldQuitLoop && !shouldQuit()) {
		processInput();
		updateGame();
		drawFrame();
		drawScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

void TeacherEngine::loadAssets() {
	if (isDemo()) {
		loadAssetsDemo();
	} else {
		error("Full game not supported yet");
	}
}

void TeacherEngine::loadAssetsDemo() {
	_strings = new StringTable("mis/strings.mis", 1);
	_flagManager = new FlagArray("question.sav", 1000);
	_flagManager->clearAllFlags();

	for (int i = 0; i < 4; ++i) {
		_gameState[i] = new GameState();
		Common::String key = Common::String::format("[GAMESTATE%04d]", i + 1);
		Parser::parseFile(_gameState[i], "mis/gamestat.mis", key);
		debugC(1, kHypnoDebugScene, "GameState[%d]: maxStates=%d", i, _gameState[i]->maxStates);
		for (int j = 0; j < _gameState[i]->maxStates; ++j) {
			if (!_gameState[i]->stateLabels[j].empty())
				debugC(1, kHypnoDebugScene, "  [%d] = '%s'", j, _gameState[i]->stateLabels[j].c_str());
		}
	}

	_peter = new Character("peter");
	_susan = new Character("susan");
	_duncan = new Character("duncan");

	// Original: g_Mouse_00436978 = new MouseControl();
	//           ParseFile(g_Mouse_00436978, "mis\\mouse1.mis", "[MICE]");
	_mouseControl = new MouseControl();
	Parser::parseFile(_mouseControl, "mis/mouse1.mis", "[MICE]");
	_mouseControl->loadCursorFrames();

	// Original: g_TextManager_00436990->LoadAnimatedAsset("elements\\text1.smk");
	//           g_TextManager_00436990->char_adv.advance = 2;
	// Note: InitGameSystems loads barrel06.smk, but RunGame immediately
	// reloads with text1.smk (main.cpp line 134). The demo uses text1.smk.
	_font = new TeacherFont();
	if (!_font->load("elements/text1.smk")) {
		warning("TeacherEngine: failed to load font from text1.smk");
	}
}

void TeacherEngine::sendMessage(int target, int source, int cmd, int data, int prio, int p1, int p2, int uPtr, int clickX, int clickY) {
	SC_Message *msg = new SC_Message(target, source, cmd, data, prio, p1, p2, uPtr, clickX, clickY);
	_messageQueue.push(msg);
}

void TeacherEngine::queueMessage(SC_Message *msg) {
	_messageQueue.push(msg);
}

// Original: g_Mouse_00436978->m_sprite->SetState2(cursorId)
void TeacherEngine::setCursor(int cursorId) {
	if (_mouseControl) {
		_mouseControl->setCursorState(cursorId);
	} else {
		// Fallback to built-in cursors
		if (cursorId == 0) changeCursor("default");
		else changeCursor("target");
	}
}

Common::Point TeacherEngine::getMousePos() {
	return g_system->getEventManager()->getMousePos();
}

Handler *TeacherEngine::findHandler(int id) {
	for (auto h : _eventList) {
		if (h && h->handlerId == id)
			return h;
	}
	return nullptr;
}

// Original: GameLoop::ProcessInput (0x4177B0)
// Creates a message from user input, lets current handler modify it via addMessage(),
// then queues if the handler set a valid target and priority.
void TeacherEngine::processInput() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_LBUTTONDOWN:
			if (_currentHandler) {
				// Create input message: command=3 (input), mouseX=2 (left button pressed)
				// clickPos = cursor position. This matches the original where mouseX/mouseY
				// are button states (ext1/ext2) and clickPos is the cursor position.
				SC_Message *msg = new SC_Message(0, 0, 3, 0, 0, 0, 0, 0, event.mouse.x, event.mouse.y);
				msg->mouseX = 2;  // Left button state: pressed (matches original ext1 >= 2)
				msg->mouseY = 2;  // Also set mouseY for handlers that check it (like Handler 8)
				msg->clickPos.x = event.mouse.x;
				msg->clickPos.y = event.mouse.y;

				warning("TeacherEngine: Click at %d, %d. Current handler: %d",
					event.mouse.x, event.mouse.y, _currentHandler ? _currentHandler->handlerId : 0);

				// Let the handler modify the message (set target, priority, etc.)
				_currentHandler->addMessage(msg);

				// Original: only queue if handler set valid target and priority
				if (msg->targetAddress != 0 && msg->priority != 0) {
					_messageQueue.push(msg);
				} else {
					delete msg;
				}
			}
			break;
		case Common::EVENT_KEYDOWN:
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
				_shouldQuitLoop = true;
			break;
		case Common::EVENT_QUIT:
		case Common::EVENT_RETURN_TO_LAUNCHER:
			_shouldQuitLoop = true;
			break;
		default:
			break;
		}
	}
}

// Original: GameLoop::UpdateGame (0x4179A0)
// Double-buffered message queue: messages are generated into the pending queue
// (_messageQueue), then drained into a processing queue for handling. Messages
// generated during processing go back to pending and are drained after each
// message is processed.
void TeacherEngine::updateGame() {
	Common::Queue<SC_Message *> processingQueue;

	// Transfer all pending messages to the processing queue
	while (!_messageQueue.empty()) {
		processingQueue.push(_messageQueue.pop());
	}

	// Process messages one at a time
	while (!processingQueue.empty()) {
		SC_Message *msg = processingQueue.pop();
		processMessage(msg);
		delete msg;

		// Drain any newly generated messages (from processMessage calling sendMessage)
		while (!_messageQueue.empty()) {
			processingQueue.push(_messageQueue.pop());
		}
	}
}

void TeacherEngine::drawFrame() {
	for (auto h : _eventList) {
		h->update(0, _currentHandler ? _currentHandler->handlerId : 0);
	}
}

// Original: GameLoop::ProcessMessage (0x417CB0)
// Routes messages based on priority and target:
// - priority 5 + target != 3: handler transition (HandleSystemMessage)
// - priority 5 + target == 3: state string copy (skipped for demo)
// - target == 3: control message (quit, frame delay, handler management)
// - target == 0: no-op, treated as handled
// - otherwise: route through handlers via deinit/Exit
void TeacherEngine::processMessage(SC_Message *msg) {
	int result = 0;

	if (msg->priority == 5) {
		if (msg->targetAddress != 3) {
			// Handler transition
			handleSystemMessage(msg);
			result = 1;
		} else {
			// Original copies string from GameState2 - skip for basic demo
			result = 1;
		}
	} else {
		int target = msg->targetAddress;
		if (target != 0 && target != 3) {
			// Try current handler first
			if (_currentHandler) {
				result = _currentHandler->deinit(msg);
			}
			if (result == 0) {
				// Iterate all handlers in event list
				for (auto h : _eventList) {
					result = h->deinit(msg);
					if (result != 0) break;
				}
			}
		} else if (target == 0) {
			result = 1; // No-op, message handled
		} else { // target == 3
			result = processControlMessage(msg);
		}
	}

	if (result == 0) {
		// Fallback: create handler and try deinit
		Handler *h = getOrCreateHandler(msg->targetAddress);
		if (h) {
			result = h->deinit(msg);
		}
		if (result == 0) {
			debugC(1, kHypnoDebugScene, "Lost message: target=%d source=%d cmd=%d data=%d prio=%d",
				msg->targetAddress, msg->sourceAddress, msg->command, msg->data, msg->priority);
		}
	}
}

// Original: GameLoop::ProcessControlMessage (0x417E20)
// Handles messages with targetAddress==3 (system control)
int TeacherEngine::processControlMessage(SC_Message *msg) {
	if (msg->targetAddress != 3) return 0;
	switch (msg->priority) {
	case 6:
		_shouldQuitLoop = true;
		return 1;
	case 0x12:
		_frameDelay = msg->sourceAddress;
		return 1;
	case 0x13:
		getOrCreateHandler(msg->sourceAddress);
		return 1;
	case 0x14:
		removeHandler(msg->sourceAddress);
		return 1;
	default:
		return 0;
	}
}

// Original: GameLoop::HandleSystemMessage (0x417F40)
// Handles handler transitions (priority 5 messages):
// 1. Shutdown current handler
// 2. Find existing or create new target handler
// 3. Set as current and call init
void TeacherEngine::handleSystemMessage(SC_Message *msg) {
	if (!msg) return;

	// Shutdown current handler (original calls it regardless of target id)
	if (_currentHandler) {
		_currentHandler->shutDown(msg);
	}

	// Original: clears ZBufferManager draw queues (0x9c, 0xa0, 0xa4)
	// This ensures no stale graphics from the previous handler remain.
	// Our equivalent: clear the composite surface.
	if (_compositeSurface) {
		_compositeSurface->fillRect(Common::Rect(0, 0, _screenW, _screenH), 0);
	}

	// Find existing handler or create new one
	Handler *nextHandler = nullptr;
	for (auto it = _eventList.begin(); it != _eventList.end(); ++it) {
		if ((*it)->handlerId == msg->targetAddress) {
			nextHandler = *it;
			break;
		}
	}

	if (!nextHandler) {
		nextHandler = createHandler(msg->targetAddress);
		if (nextHandler) {
			_eventList.push_back(nextHandler);
		}
	}

	_currentHandler = nextHandler;

	if (_currentHandler) {
		_currentHandler->init(msg);
	} else {
		warning("Missing handler %d", msg->targetAddress);
	}
}

// Original: CreateHandler (0x40CDD0)
// Factory function that creates handlers by id
Handler *TeacherEngine::createHandler(int id) {
	Handler *h = nullptr;
	switch (id) {
	case 1:
		h = new Handler1();
		break;
	case 2:
		h = new Handler2();
		break;
	case 4:
		h = new Handler4();
		break;
	case 6:
		h = new Handler6();
		break;
	case 8:
		h = new Handler8();
		break;
	case 9:
		h = new Handler9();
		break;
	case 10:
		h = new Handler10();
		break;
	case 11:
		h = new Handler11();
		break;
	case 12:
		h = new Handler12();
		break;
	case 13:
		h = new Handler13();
		break;
	case 14:
		h = new Handler14();
		break;
	case 15:
		h = new Handler15();
		break;
	case 16:
		h = new SC_Combat1();
		break;
	default:
		warning("Creating unknown handler %d", id);
		h = new Handler();
		h->handlerId = id;
		break;
	}
	return h;
}

Handler *TeacherEngine::getOrCreateHandler(int id) {
	for (auto h : _eventList) {
		if (h->handlerId == id) return h;
	}

	Handler *h = createHandler(id);
	if (h) {
		_eventList.push_back(h);
	}
	return h;
}

// Original: GameLoop::RemoveHandler (0x418460)
void TeacherEngine::removeHandler(int id) {
	for (auto it = _eventList.begin(); it != _eventList.end(); ++it) {
		if ((*it)->handlerId == id) {
			Handler *h = *it;
			_eventList.erase(it);
			h->shutDown(nullptr);
			delete h;
			if (_currentHandler == h) {
				_currentHandler = nullptr;
			}
			return;
		}
	}
}

void SC_Message_Send(int target, int source, int command, int data, int priority, int param1, int param2, int userPtr, int clickX, int clickY) {
	((TeacherEngine *)g_engine)->sendMessage(target, source, command, data, priority, param1, param2, userPtr, clickX, clickY);
}

} // End of namespace Hypno
