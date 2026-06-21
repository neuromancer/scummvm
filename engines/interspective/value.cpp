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

#include "interspective/value.h"

#include "common/rect.h"
#include "common/textconsole.h"

#include "interspective/inter.h"
#include "interspective/util.h"

namespace Interspective {
//

Value &WordVariable::operator=(uint16 value) {
	debugC(1, kDebugLevelValues, "setting %s to %d", +*this, value);
	WRITE_LE_UINT16(_ptr, value);
	return *this;
}

CodePointer::CodePointer(uint16 off, Interpreter *i) : _offset(off), _interpreter(i) {
	init();
}

void CodePointer::init() {
	// Guard on _interpreter too — _offset is no longer indeterminate after
	// the default-ctor fix, but a moved-from / cleared CodePointer can still
	// have _offset != 0 with _interpreter == 0 (e.g. after reset()).
	if (_offset && _interpreter)
		snprintf(_inspect, 40, "code offset 0x%04x of %s", _offset, _interpreter->name());
	else
		snprintf(_inspect, 40, "null pointer");
}

void CodePointer::run() const {
	if (_offset && _interpreter)
		_interpreter->run(_offset);
}

void CodePointer::run(OpcodeMode mode) const {
	if (_offset && _interpreter)
		_interpreter->run(_offset, mode);
}

byte *CodePointer::code() const {
	return _interpreter ? _interpreter->rawCodeChecked(_offset) : 0;
}

byte *CodePointer::base() const {
	return _interpreter ? _interpreter->rawCodeChecked(0) : 0;
}

bool CodePointer::memoryReference(DosMemoryReference &ref) const {
	return _interpreter && _interpreter->memoryReference(_offset, ref);
}

static byte *checkedCodePointerField(const CodePointer &ptr, int off, uint16 size) {
	Interpreter *interpreter = ptr.interpreter();
	if (!interpreter) {
		warning("Interspective: field read through null code pointer");
		return 0;
	}

	const int32 absolute = int32(ptr.offset()) + off;
	if (absolute < 0 || absolute > 0xffff) {
		warning("Interspective: field offset %d outside %s code pointer 0x%04x",
				off, interpreter->name(), ptr.offset());
		return 0;
	}

	return interpreter->rawCodeChecked(uint16(absolute), size);
}

template<>
uint16 &CodePointer::field<uint16>(uint16 &p, int off) const {
	byte *field = checkedCodePointerField(*this, off, 2);
	p = field ? READ_LE_UINT16(field) : 0;
	return p;
}

template<>
int16 &CodePointer::field<int16>(int16 &p, int off) const {
	uint16 z;
	field(z, off);
	p = dosSignedWord(z);
	return p;
}

template<>
Common::Point &CodePointer::field<Common::Point>(Common::Point &p, int off) const {
	field(p.x, off);
	field(p.y, off + 2);
	return p;
}

template<>
byte &CodePointer::field<byte>(byte &p, int off) const {
	byte *field = checkedCodePointerField(*this, off, 1);
	p = field ? *field : 0;
	return p;
}

template<>
bool &CodePointer::field<bool>(bool &p, int off) const {
	byte b;
	field(b, off);
	return p = b;
}

} // End of namespace Interspective
