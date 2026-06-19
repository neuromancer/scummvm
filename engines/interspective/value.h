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
#include "common/endian.h"
#include "common/noncopyable.h"

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

enum ValueType {
	kValueVoid,
	kValueConstant
};

class Value : public NumericInspectable<uint16> {
public:
	virtual ~Value() {}
	virtual ValueType type() const { return kValueVoid; }

	virtual operator uint16() const { return false; }
	virtual int16 signd() const {
		const uint16 v = *this;
		return *reinterpret_cast<const int16 *>(&v);
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

	virtual operator byte *() { return nullptr; }
	virtual operator const Common::String() {
		byte *b(*this);
		return Common::String(reinterpret_cast<const char *>(b));
	}
	virtual byte *rawPointer() { return nullptr; }
	virtual byte *rawBase() { return nullptr; }
	virtual uint16 rawLength() const { return 0; }

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

class ByteVariable : public Value {
public:
	ByteVariable(byte *ptr) : _ptr(ptr) {}
	virtual Value &operator=(uint16 value) {
		*_ptr = uint8(value);
		return *this;
	}
	virtual operator uint16() const { return *_ptr; }

private:
	byte *_ptr;
};

class WordVariable : public Value {
public:
	WordVariable(byte *ptr) : _ptr(ptr) {}
	virtual operator uint16() const { return READ_LE_UINT16(_ptr); }
	virtual Value &operator=(uint16 value);
	virtual Value &operator=(const Value &other) { return *this = uint16(other); }

private:
	byte *_ptr;
};

class ValueVector : private Common::NonCopyable {
public:
	~ValueVector() {
		for (Common::Array<Value *>::iterator it = _values.begin(); it != _values.end(); ++it)
			delete *it;
	}
	void push_back(Value *element) { _values.push_back(element); }
	Value &operator[](uint8 idx) { return *_values[idx]; }

private:
	Common::Array<Value *> _values;
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
	byte *base() const;
	virtual operator uint16() const { return _offset; }
	virtual bool holdsCode() const { return true; }
	virtual byte *code() const;
	Interpreter *interpreter() const { return _interpreter; }
	bool isEmpty() const { return _interpreter == 0; }
	void reset() { _interpreter = 0; }
	virtual byte *rawPointer() { return _interpreter ? code() : nullptr; }
	virtual byte *rawBase() { return _interpreter ? base() : nullptr; }

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
