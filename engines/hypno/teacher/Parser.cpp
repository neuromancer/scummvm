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

#include "hypno/teacher/Parser.h"
#include "common/file.h"
#include "common/path.h"
#include "engines/util.h"
#include "common/system.h"
#include "common/archive.h"

namespace Hypno {

Parser::Parser() : _file(nullptr), _isProcessingKey(false), _lineNumber(0), _savedFilePos(0) {
}

Parser::~Parser() {
	closeFile();
}

void Parser::open(const Common::String &filename) {
	closeFile();
	_filename = filename;
	_file = SearchMan.createReadStreamForMember(Common::Path(filename));
	if (!_file) {
		error("Parser::open - Unable to open file '%s'", filename.c_str());
	}
}

void Parser::closeFile() {
	if (_file) {
		delete _file;
		_file = nullptr;
	}
}

int Parser::lblParse(const Common::String &line) {
	warning("Parser::lblParse - Unknown Label '%s' in file %s", line.c_str(), _filename.c_str());
	return 0;
}

void Parser::onProcessStart() {}
void Parser::onProcessEnd() {}

void Parser::saveFilePosition() {
	if (_file)
		_savedFilePos = _file->pos();
}

void Parser::restoreFilePosition() {
	if (_file)
		_file->seek(_savedFilePos);
}

Common::String Parser::readLine() {
	if (!_file || _file->eos())
		return "";
	return _file->readLine();
}

void Parser::findKey(const Common::String &key) {
	if (!_file) return;
	_file->seek(0);
	while (!_file->eos()) {
		Common::String line = readLine();
		line.trim();
		if (line == key)
			return;
	}
	error("Parser::findKey - Unable to find key '%s' in file '%s'", key.c_str(), _filename.c_str());
}

int Parser::getKey(const Common::String &line) {
	size_t startPos = line.find("<<");
	if (startPos != Common::String::npos) {
		size_t endPos = line.find(">>", startPos + 2);
		if (endPos != Common::String::npos) {
			Common::String keyName = line.substr(startPos + 2, endPos - (startPos + 2));
			if (!_isProcessingKey) {
				_currentKey = keyName;
				_isProcessingKey = true;
			} else if (_currentKey == keyName) {
				_isProcessingKey = false;
				_currentKey = "";
			}
		}
	}
	return _isProcessingKey;
}

int Parser::processFile(Parser *self, Parser *dst, const Common::String &key) {
	bool borrowFile = (self != dst);
	if (borrowFile) {
		self->_file = dst->_file;
		self->_filename = dst->_filename;
	}

	if (!key.empty()) {
		self->findKey(key);
	}

	self->onProcessStart();
	int result = 0;
	while (self->_file && !self->_file->eos()) {
		Common::String line = self->readLine();
		if (self->getKey(line))
			continue;
		
		if (line.empty()) continue;
		
		result = self->lblParse(line);
		if (result != 0)
			break;
	}
	self->onProcessEnd();

	if (borrowFile) {
		self->_file = nullptr;
		// self->_filename = "";  <-- DO NOT DO THIS! It clears filenames set by lblParse
	}
	return result;
}

int Parser::parseFile(Parser *parser, const Common::String &filename, const Common::String &key) {
	parser->open(filename);
	int result = Parser::processFile(parser, parser, key);
	parser->closeFile();
	return result;
}

} // End of namespace Hypno
