/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too list here. Please refer to the COPYRIGHT
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

#include "hypno/teacher/demo/Handler6.h"

namespace Hypno {

Handler6::Handler6() : _currentHotspot(nullptr), _counter(0) {
	handlerId = 6;
}

Handler6::~Handler6() {
	for (auto h : _hotspots) delete h;
}

void Handler6::init(SC_Message *msg) {
	initIconBar(msg);
	char filePath[32];
	char staticLabel[32];
	char periodLabel[32];

	Common::sprintf_s(filePath, "mis/room%03d.mis", 5);
	Common::sprintf_s(staticLabel, "[STATIC]");
	Common::sprintf_s(periodLabel, "[PERIOD%02d]", 1);

	Parser::parseFile(this, filePath, staticLabel);
	Parser::parseFile(this, filePath, periodLabel);
}

int Handler6::shutDown(SC_Message *msg) {
	cleanupIconBar(msg);
	for (auto h : _hotspots) delete h;
	_hotspots.clear();
	return 0;
}

int Handler6::addMessage(SC_Message *msg) {
	if (checkButtonClick(msg)) return 1;

	if (msg->mouseX >= 2 && !_currentHotspot) {
		for (auto h : _hotspots) {
			if (h->rect.contains(msg->clickPos)) {
				_currentHotspot = h;
				break;
			}
		}
	}
	return 1;
}

int Handler6::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler6::update(int param1, int param2) {
	IconBar::update(param1, param2);
	if (handlerId == param2) {
		// Render room specific elements
	}
}

int Handler6::lblParse(const Common::String &line) {
	Common::String workLine = line;
	workLine.trim();
	if (workLine.empty()) return 0;

	size_t firstSpace = workLine.find(' ');
	Common::String keyword = (firstSpace == Common::String::npos) ? workLine : workLine.substr(0, firstSpace);
	Common::String rest = (firstSpace == Common::String::npos) ? "" : workLine.substr(firstSpace + 1);
	rest.trim();

	if (keyword == "HOTSPOT") {
		TeacherHotspot *h = new TeacherHotspot();
		Parser::processFile(h, this, "");
		_hotspots.push_back(h);
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
