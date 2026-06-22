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

#ifndef INTERSPECTIVE_VARIABLES_H
#define INTERSPECTIVE_VARIABLES_H

#include "common/array.h"
#include "common/noncopyable.h"
#include "common/ptr.h"
#include "common/span.h"

#include "interspective/debug.h"

namespace Interspective {
//

enum OpcodeMode {
	kCodeInitial = 0,
	kCodeNewRoom = 1,
	kCodeRoomLoop = 2,
	kCodeGlobalRoomLoop = 3,
	kCodeItem = 4,
	kCodeStatusRefresh = 6,
	kCodeStatusLoop = 7,
	kCodeNewBlock = 8
};

class Interpreter;

class DosMemoryReference {
public:
	DosMemoryReference() : _offset(0), _valid(false) {}
	DosMemoryReference(Common::Span<byte> segment, uint16 offset)
		: _segment(segment), _offset(offset), _valid(segment.data() != nullptr && offset < segment.size()) {}

	bool valid() const { return _valid; }
	uint16 size() const { return uint16(_segment.size()); }
	uint16 offset() const { return _offset; }
	uint16 remaining() const { return valid() ? uint16(_segment.size() - _offset) : 0; }
	uint16 remainingFrom(uint16 relativeOffset) const {
		return contains(relativeOffset, 0) ? uint16(_segment.size() - uint32(_offset) - relativeOffset) : 0;
	}

	bool contains(uint16 relativeOffset, uint16 length = 1) const {
		if (!valid())
			return false;
		const uint32 absolute = uint32(_offset) + relativeOffset;
		return absolute <= _segment.size() && length <= _segment.size() - absolute;
	}

	Common::Span<const byte> span(uint16 relativeOffset = 0) const {
		const uint16 count = remainingFrom(relativeOffset);
		return count != 0 ? span(relativeOffset, count) : Common::Span<const byte>();
	}

	Common::Span<const byte> span(uint16 relativeOffset, uint16 length) const {
		return contains(relativeOffset, length)
				   ? _segment.subspan<const byte>(uint32(_offset) + relativeOffset, length)
				   : Common::Span<const byte>();
	}

	bool readByte(uint16 relativeOffset, byte &value) const {
		value = 0;
		if (!contains(relativeOffset))
			return false;
		value = _segment.getUint8At(uint32(_offset) + relativeOffset);
		return true;
	}

	bool writeByte(uint16 relativeOffset, byte value) const {
		if (!contains(relativeOffset))
			return false;
		writableSpan(relativeOffset, 1)[0] = value;
		return true;
	}

	bool fillBytes(uint16 relativeOffset, uint16 length, byte value) const {
		if (!contains(relativeOffset, length))
			return false;
		Common::Span<byte> bytes = writableSpan(relativeOffset, length);
		for (uint16 i = 0; i < length; ++i)
			bytes[i] = value;
		return true;
	}

	bool containsByte(byte value) const {
		if (!valid())
			return false;
		const uint16 count = remaining();
		for (uint16 i = 0; i < count; ++i) {
			if (_segment.getUint8At(uint32(_offset) + i) == value)
				return true;
		}
		return false;
	}

private:
	Common::Span<byte> writableSpan(uint16 relativeOffset, uint16 length) const {
		return contains(relativeOffset, length)
				   ? Common::Span<byte>(_segment.data() + uint32(_offset) + relativeOffset, length)
				   : Common::Span<byte>();
	}

	Common::Span<byte> _segment;
	uint16 _offset;
	bool _valid;
};

enum ValueType {
	kValueVoid,
	kValueConstant
};

inline int16 dosSignedWord(uint16 value) {
	return (value & 0x8000) ? int16(int32(value) - 0x10000) : int16(value);
}

inline int8 dosSignedByte(uint8 value) {
	return (value & 0x80) ? int8(int16(value) - 0x100) : int8(value);
}

class Value : public NumericInspectable<uint16> {
public:
	virtual ~Value() {}
	virtual ValueType type() const { return kValueVoid; }

	virtual operator uint16() const { return false; }
	virtual int16 signd() const {
		return dosSignedWord(*this);
	}
	virtual Value &operator=(uint16 value) { return *this; }
	virtual Value &operator=(const Value &) { return *this; }
	virtual bool operator==(const Value &other) { return uint16(*this) == other; }
	virtual bool operator<(const Value &other) { return uint16(*this) < other; }
	virtual bool operator>(const Value &other) { return other < *this; }
	virtual Value &operator++() { return *this = uint16(*this) + 1; }
	virtual uint16 operator++(int) {
		uint16 old = *this;
		++*this;
		return old;
	}
	virtual Value &operator--() { return *this = uint16(*this) - 1; }
	virtual uint16 operator--(int) {
		uint16 old = *this;
		--*this;
		return old;
	}
	template<typename T>
	T operator-(T she) {
		T me = *this;
		return me - she;
	}

	virtual bool holdsCode() const { return false; }

	virtual Common::Span<const byte> translatedTextSpan() const { return Common::Span<const byte>(); }
	virtual operator const Common::String() const {
		Common::Span<const byte> text = translatedTextSpan();
		if (!text.data()) {
			DosMemoryReference ref;
			if (memoryReference(ref))
				text = ref.span();
		}
		if (!text.data())
			return Common::String();

		uint32 length = 0;
		while (length < text.size() && text.getUint8At(length) != 0)
			++length;
		return Common::String(reinterpret_cast<const char *>(text.data()), length);
	}
	virtual uint16 rawLength() const { return 0; }
	virtual bool memoryReference(DosMemoryReference &) const { return false; }

	Value() {}

private:
	explicit Value(Value &) : NumericInspectable<uint16>() { assert(false); } // no copying
};

class Constant : public Value {
public:
	Constant(uint16 value) : _value(value) {}
	virtual ~Constant() {}

	virtual operator uint16() const { return _value; }
	virtual ValueType type() const { return kValueConstant; }

private:
	uint16 _value;
};

class ValueVector : private Common::NonCopyable {
public:
	void push_back(Value *element) { _values.push_back(Common::ScopedPtr<Value>(element)); }
	Value &operator[](uint8 idx) { return *_values[idx].get(); }

private:
	Common::Array<Common::ScopedPtr<Value> > _values;
};

class CodePointer : public Value {
public:
	// Init _offset to 0 — otherwise the default-constructed instance has
	// indeterminate _offset, and a subsequent copy/init() reads the garbage
	// value, hits the `if (_offset)` branch in init(), and dereferences the
	// null _interpreter on the snprintf format. This bit a queued speech
	// callback whose default-ctor'd CodePointer was being copied through
	// Common::Queue.
	CodePointer() : _offset(0), _interpreter(0) {}
	CodePointer(const CodePointer &c) : Value(), _offset(c._offset), _interpreter(c._interpreter) { init(); }
	CodePointer(uint16 offset, Interpreter *interpreter);

	CodePointer &operator=(const CodePointer &cp) {
		_offset = cp._offset;
		_interpreter = cp._interpreter;
		init();
		return *this;
	}

	virtual const char *operator+() const { return _inspect; }
	virtual void run() const;
	virtual void run(OpcodeMode mode) const;
	uint16 offset() const { return _offset; }
	virtual operator uint16() const { return _offset; }
	virtual bool holdsCode() const { return true; }
	Interpreter *interpreter() const { return _interpreter; }
	bool isEmpty() const { return _interpreter == 0; }
	void reset() { _interpreter = 0; }
	virtual bool memoryReference(DosMemoryReference &ref) const;

	template<typename T>
	T &field(T &, int) const;

private:
	void init();
	char _inspect[40];
	uint16 _offset;
	Interpreter *_interpreter;
};

template<typename Enum, int N>
struct EnumName {
	static const char *name() {
		assert(false);
		return 0;
	}

	static const char *findName(Enum a) {
		if (N == a)
			return name();
		else
			return EnumName<Enum, N - 1>::findName(a);
	}
};

template<typename Enum>
struct EnumName<Enum, -1> {
	static const char *findName(Enum a) {
		assert(false);
		return 0;
	}
};

template<typename Enum>
class EnumPack : public Inspectable {
public:
	EnumPack() {}
	EnumPack(Enum a) : _a(a) {}
	const char *operator+() const { return EnumName<Enum, 40>::findName(Enum(_a)); }
	operator Enum() const { return _a; }

private:
	Enum _a;
};

#define ENAME(en, v, s)                               \
	template<>                                        \
	inline const char *EnumName<en, v>::name() { return s; }

} // End of namespace Interspective

#endif // INTERSPECTIVE_VARIABLES_H
