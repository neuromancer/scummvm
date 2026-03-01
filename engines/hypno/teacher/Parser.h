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

#ifndef HYPNO_TEACHER_PARSER_H
#define HYPNO_TEACHER_PARSER_H

#include "common/stream.h"
#include "common/str.h"

namespace Hypno {

class Parser {
public:
	Parser();
	virtual ~Parser();

	virtual int lblParse(const Common::String &line);
	virtual void onProcessStart();
	virtual void onProcessEnd();
	
	static void tokenize(const Common::String &line, Common::String &keyword, Common::String &rest) {
		Common::String workLine = line;
		workLine.trim();
		if (workLine.empty()) {
			keyword = "";
			rest = "";
			return;
		}

		size_t firstWS = workLine.findFirstOf(" \t");
		if (firstWS == Common::String::npos) {
			keyword = workLine;
			rest = "";
		} else {
			keyword = workLine.substr(0, firstWS);
			rest = workLine.substr(firstWS + 1);
			rest.trim();
		}
	}

	void closeFile();
	void open(const Common::String &filename);
	void saveFilePosition();
	void restoreFilePosition();
	void findKey(const Common::String &key);
	int getKey(const Common::String &line);

	static int processFile(Parser *self, Parser *dst, const Common::String &key);
	static int parseFile(Parser *parser, const Common::String &filename, const Common::String &key);

protected:
	Common::SeekableReadStream *_file;
	Common::String _filename;
	Common::String _currentKey;
	bool _isProcessingKey;
	int _lineNumber;
	int64 _savedFilePos;

	Common::String readLine();
};

} // End of namespace Hypno

#endif
