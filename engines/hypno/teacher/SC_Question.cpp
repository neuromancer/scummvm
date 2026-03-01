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

#include "hypno/teacher/SC_Question.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"

namespace Hypno {

// Original: SC_Question::SC_Question at 0x4066D0
TeacherQuestion::TeacherQuestion(uint32 id) : questionId(id), state(0) {
	// Parse the question file
	char key[32];
	Common::sprintf_s(key, "[QUESTION%05d]", questionId);
	Parser::parseFile(this, "mis/quest001.mis", key);

	// Check if question was already answered
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (engine->_flagManager && engine->_flagManager->getFlag(questionId, 1) != 0) {
		state = 2;
	}
}

TeacherQuestion::~TeacherQuestion() {
	for (auto m : messageQueue) delete m;
	for (auto s : overlaySprites) delete s;
}

// Original: SC_Question::Update at 0x406930
void TeacherQuestion::update(int x, int y) {
	switch (state) {
	case 0:
		// Original: g_ZBufferManager->ShowSubtitle(label, x+10, y+23, 10000, 8)
		// In the original, y is the visual BOTTOM of the glyph (bottom-up VBuffer),
		// and the glyph draws upward. In ScummVM's top-down surface, y is the TOP.
		// Adjust: top = original_y - glyphHeight + 1
		if (!label.empty()) {
			TeacherEngine *engine = (TeacherEngine *)g_engine;
			if (engine->_font && engine->_compositeSurface) {
				int textY = y + 23 - engine->_font->getFontHeight() + 1;
				engine->_font->drawString(engine->_compositeSurface, label,
					x + 10, textY, engine->_screenW - x - 20, 8);
			}
		}
		break;
	case 1:
		// Original: mouseControl->Draw() returns 0 when ANY sprite finishes
		// SC_Question::Finalize() is called when Draw() == 0
		if (!overlaySprites.empty()) {
			bool anyDone = false;
			for (auto s : overlaySprites) {
				if (s->Do(s->loc_x, s->loc_y, 1.0) != 0)
					anyDone = true;
			}
			if (anyDone) {
				finalize();
			}
		}
		// Original: if mouseControl == null, does nothing (stays in state 1)
		break;
	case 2:
		break;
	default:
		warning("SC_Question: illegal state %d", state);
		break;
	}
}

// Original: SC_Question::Finalize at 0x4069B0
void TeacherQuestion::finalize() {
	// Mark question as answered in flag array
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (engine->_flagManager) {
		engine->_flagManager->setFlag(questionId, 1);
	}

	state = 2;

	// Queue all messages from the message queue to the engine
	for (auto msg : messageQueue) {
		engine->queueMessage(msg);
	}
	messageQueue.clear();
}

// Original: SC_Question::LBLParse at 0x406AF0
int TeacherQuestion::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "OVERLAYS") {
		// Original: creates MMPlayer for mouseControl overlay
		// MMPlayer contains SPRITE sub-blocks, parsed via ProcessFile
		while (_file && !_file->eos()) {
			Common::String subLine = readLine();
			Common::String subKw, subRest;
			tokenize(subLine, subKw, subRest);
			if (subKw.empty()) continue;
			if (subKw == "END") break;
			if (subKw == "SPRITE") {
				Sprite *s = new Sprite();
				Parser::processFile(s, this, "");
				overlaySprites.push_back(s);
			}
		}
	} else if (keyword == "LABEL") {
		// Original: ExtractQuotedString(param_1, label, 0x80)
		// Extract quoted string from rest
		size_t firstQuote = rest.find('"');
		if (firstQuote != Common::String::npos) {
			size_t secondQuote = rest.find('"', firstQuote + 1);
			if (secondQuote != Common::String::npos) {
				label = rest.substr(firstQuote + 1, secondQuote - firstQuote - 1);
			} else {
				label = rest.substr(firstQuote + 1);
			}
		} else {
			label = rest;
		}
	} else if (keyword == "MESSAGE") {
		SC_Message *msg = new SC_Message(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		Parser::processFile(msg, this, "");
		messageQueue.push_back(msg);
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
