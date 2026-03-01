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

#include "hypno/teacher/demo/SCI_Dialog.h"
#include "hypno/teacher/teacher.h"
#include "common/debug.h"

namespace Hypno {

// Original: SCI_Dialog::SCI_Dialog at 0x406FC0
Handler9::Handler9() : _activeQuestion(nullptr), _buttonSprite(nullptr), _highlightSprite(nullptr), _searchScreen(nullptr) {
	handlerId = 9;
	for (int i = 0; i < 10; i++) _states[i] = 1;
}

Handler9::~Handler9() {
	for (auto q : _questions) delete q;
	delete _buttonSprite;
	delete _highlightSprite;
}

// Original: SCI_Dialog::Init at 0x407240
void Handler9::init(SC_Message *msg) {
	debugC(1, kHypnoDebugScene, "ENTER DIALOG");

	initIconBar(msg);

	for (int i = 0; i < 10; i++) _states[i] = 1;

	// Original: receives background/ambient pointers from SearchScreen via msg->userPtr
	// We look up Handler11 directly since pointers can't be passed through int userPtr
	_searchScreen = nullptr;
	if (msg->command == 11) {
		TeacherEngine *engine = (TeacherEngine *)g_engine;
		Handler *h = engine->findHandler(11);
		if (h) _searchScreen = static_cast<Handler11 *>(h);
	}

	// Disable choice and mainmenu buttons
	buttons[0].enabled = 0;
	buttons[4].enabled = 0;

	char filePath[64];
	char dialogLabel[32];
	Common::sprintf_s(filePath, "mis/dialog%02d.mis", msg->param1);
	Common::sprintf_s(dialogLabel, "[DIALOG%04d]", msg->sourceAddress);

	warning("Handler9::init: parsing '%s' label '%s' (param1=%d sourceAddress=%d command=%d data=%d)",
		filePath, dialogLabel, msg->param1, msg->sourceAddress, msg->command, msg->data);

	Parser::parseFile(this, filePath, dialogLabel);

	warning("Handler9::init: parsed %d questions, button=%p highlight=%p",
		(int)_questions.size(), (void *)_buttonSprite, (void *)_highlightSprite);
}

// Original: SCI_Dialog::ShutDown at 0x407380
int Handler9::shutDown(SC_Message *msg) {
	debugC(1, kHypnoDebugScene, "EXIT DIALOG");

	// Original: StopAnimationSound — stops audio without changing visual state
	if (_buttonSprite != nullptr) {
		_buttonSprite->stopAnimationSound();
	}
	if (_highlightSprite != nullptr) {
		_highlightSprite->stopAnimationSound();
	}

	// Delete active question
	if (_activeQuestion != nullptr) {
		delete _activeQuestion;
		_activeQuestion = nullptr;
	}

	// Delete all queued questions
	for (auto q : _questions) delete q;
	_questions.clear();

	// Re-enable buttons
	buttons[0].enabled = 1;
	buttons[4].enabled = 1;

	cleanupIconBar(msg);

	return 0;
}

// Original: SCI_Dialog::AddMessage at 0x4074E0
// Routes clicks through the message system for priority-based dispatch via deinit()
int Handler9::addMessage(SC_Message *msg) {
	// Check iconbar button click first
	if (checkButtonClick(msg))
		return 1;

	if (_activeQuestion != nullptr) {
		// Active question: ESC finalizes it
		if (msg->lastKey == 0x1b) {
			_activeQuestion->finalize();
			if (_activeQuestion->state == 2) {
				delete _activeQuestion;
				_activeQuestion = nullptr;
			}
			return 1;
		} else {
			// Original: clicking while question active sends exit message
			// SC_Message_Send(command, data, 9, sourceAddress, 5, 6, 0, 0, 0, 0)
			if (msg->mouseY >= 2) {
				SC_Message_Send(command, data, 9, sourceAddress, 5, 6, 0, 0, 0, 0);
				return 1;
			}
		}
	} else {
		// No active question
		if (msg->mouseX >= 2) {
			// Click: set up message for priority-3 dispatch through deinit()
			// Original: sets msg->targetAddress=9, sourceAddress=clicked index, priority=3
			msg->targetAddress = 9;
			TeacherEngine *engine = (TeacherEngine *)g_engine;
			Common::Point mousePos = engine->getMousePos();
			if (mousePos.y < 10) {
				msg->sourceAddress = -1;
			} else {
				msg->sourceAddress = (mousePos.y - 10) / 0x22; // 0x22 = 34
			}
			msg->priority = 3;
			return 1;
		}
		// ESC with no active question: send exit message
		if (msg->lastKey == 0x1b) {
			SC_Message_Send(command, data, 9, sourceAddress, 5, 6, 0, 0, 0, 0);
		}
	}

	return 1;
}

// Original: SCI_Dialog::Exit at 0x407650
// Priority-based dispatch for dialog management:
//   Priority 3: Select question by index from queue
//   Priority 0x13: Load/create new question and insert sorted
//   Priority 0x14: Mark question as answered via flag manager
int Handler9::deinit(SC_Message *msg) {
	if (msg->targetAddress != handlerId)
		return 0;

	TeacherEngine *engine = (TeacherEngine *)g_engine;

	switch (msg->priority) {
	case 0:
		return 0;

	case 3:
		// Select question by index
		playButtonSound(-1);
		if (_activeQuestion == nullptr) {
			_activeQuestion = getQuestionByIndex(msg->sourceAddress);
		}
		if (_activeQuestion != nullptr) {
			_activeQuestion->state = 1;
		}
		break;

	case 0x13:
		// Load new question dynamically and insert sorted by questionId
		{
			// Check if question already exists in queue
			TeacherQuestion *existing = findQuestionById(msg->sourceAddress);
			if (existing != nullptr) {
				// Already in queue, put it back (findQuestionById removes it)
				insertQuestionSorted(existing);
				break;
			}

			TeacherQuestion *dq = new TeacherQuestion(msg->sourceAddress);
			if (dq->state == 2) {
				// Already answered
				delete dq;
			} else {
				insertQuestionSorted(dq);
			}
		}
		break;

	case 0x14:
		// Mark question as answered
		{
			TeacherQuestion *dq = findQuestionById(msg->sourceAddress);
			if (engine->_flagManager) {
				engine->_flagManager->setFlag(msg->sourceAddress, 1);
			}
			if (dq != nullptr) delete dq;
		}
		break;

	default:
		break;
	}

	return 1;
}

// Original: SCI_Dialog::Update at 0x407A40
void Handler9::update(int param1, int param2) {
	if (handlerId != param2) return;

	IconBar::update(param1, param2);

	// Original: field_600->Draw() — render borrowed background from SearchScreen
	if (_searchScreen) {
		for (auto s : _searchScreen->backgroundSprites)
			s->Do(s->loc_x, s->loc_y, 1.0);
	}

	// If active question is being answered
	if (_activeQuestion != nullptr) {
		_activeQuestion->update(18, 10);
		// Original: field_604->DrawWithStates(field_618) — render ambient with states
		if (_searchScreen) {
			int idx = 0;
			for (auto s : _searchScreen->ambientSprites) {
				if (idx < 10 && _states[idx])
					s->Do(s->loc_x, s->loc_y, 1.0);
				idx++;
			}
		}
		if (_activeQuestion->state == 2) {
			delete _activeQuestion;
			_activeQuestion = nullptr;
		}
		return;
	}

	// Check if queue is empty -> send exit message
	if (_questions.empty()) {
		// Original: field_604->Draw() — render ambient before exiting
		if (_searchScreen) {
			for (auto s : _searchScreen->ambientSprites)
				s->Do(s->loc_x, s->loc_y, 1.0);
		}
		warning("Handler9::update: questions empty, sending exit (command=%d data=%d sourceAddress=%d)",
			command, data, sourceAddress);
		SC_Message_Send(command, data, 9, sourceAddress, 5, 6, 0, 0, 0, 0);
		return;
	}

	// Original: field_604->Draw() — render ambient sprites
	if (_searchScreen) {
		for (auto s : _searchScreen->ambientSprites)
			s->Do(s->loc_x, s->loc_y, 1.0);
	}

	// Render question list with button/highlight sprites
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Common::Point mousePos = engine->getMousePos();
	int mouseIndex = -1;
	if (mousePos.y >= 10) {
		mouseIndex = (mousePos.y - 10) / 34;
	}

	int y = 10;
	int counter = 0;

	for (auto q : _questions) {
		// Original uses z-buffer: sprite at default z, text at z=10000 (drawn last)
		// Without z-buffer, draw sprite first then text on top
		if (counter == mouseIndex) {
			if (_highlightSprite != nullptr) {
				_highlightSprite->Do(18, y, 1.0);
			}
		} else {
			if (_buttonSprite != nullptr) {
				_buttonSprite->Do(18, y, 1.0);
			}
		}

		q->update(18, y);

		counter++;
		y += 34;
	}
}

// Helper: get question by queue index (removes it from the list)
// Original: SCI_Dialog::GetDialogByIndex at 0x407C50
TeacherQuestion *Handler9::getQuestionByIndex(int index) {
	if (index < 0 || index >= (int)_questions.size()) return nullptr;
	auto it = _questions.begin();
	for (int i = 0; i < index; i++) ++it;
	TeacherQuestion *q = *it;
	_questions.erase(it);
	return q;
}

// Helper: find and remove question by ID from the list
// Original: SCI_Dialog::FindDialogById at 0x407D20
TeacherQuestion *Handler9::findQuestionById(int id) {
	for (auto it = _questions.begin(); it != _questions.end(); ++it) {
		if ((int)(*it)->questionId == id) {
			TeacherQuestion *q = *it;
			_questions.erase(it);
			return q;
		}
	}
	return nullptr;
}

// Helper: insert question sorted by questionId (ascending)
void Handler9::insertQuestionSorted(TeacherQuestion *q) {
	for (auto it = _questions.begin(); it != _questions.end(); ++it) {
		if ((*it)->questionId > q->questionId) {
			_questions.insert(it, q);
			return;
		}
	}
	_questions.push_back(q);
}

// Original: SCI_Dialog::LBLParse at 0x407EC6
int Handler9::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	warning("Handler9::lblParse keyword='%s' rest='%s'", keyword.c_str(), rest.c_str());

	if (keyword == "BACKGROUND") {
		// Original: shows error if background already exists
		warning("SCI_Dialog::LBLParse BACKGROUND not implemented");
	} else if (keyword == "PALETTE") {
		warning("SCI_Dialog::LBLParse PALETTE not implemented");
	} else if (keyword == "BUTTON") {
		_buttonSprite = new Sprite();
		Parser::processFile(_buttonSprite, this, "");
	} else if (keyword == "AMBIENT_SPRS") {
		warning("SCI_Dialog::LBLParse AMBIENT_SPRS not implemented");
	} else if (keyword == "HILITE") {
		_highlightSprite = new Sprite();
		Parser::processFile(_highlightSprite, this, "");
	} else if (keyword == "HANDLE") {
		// Original: sscanf(line, "%s %d", token, &sourceAddress)
		sourceAddress = atoi(rest.c_str());
	} else if (keyword == "AMBIENTSCONTROLLER") {
		// Original: decodes digit sequence and disables those ambient indices
		int ambientVal = atoi(rest.c_str());
		int temp = ambientVal;
		int count = 1;
		int av = ambientVal;
		while (av >= 10) {
			count++;
			av /= 10;
		}
		for (; count > 0; count--) {
			int digit = temp % 10;
			if (digit >= 0 && digit < 10) {
				_states[digit] = 0;
			}
			temp /= 10;
		}
	} else if (keyword == "QUESTION") {
		int id = atoi(rest.c_str());
		TeacherQuestion *dq = new TeacherQuestion(id);
		if (dq->state == 2) {
			// Already answered, discard
			delete dq;
		} else {
			insertQuestionSorted(dq);
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
