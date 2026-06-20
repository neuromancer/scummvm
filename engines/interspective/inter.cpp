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

#include "interspective/inter.h"

#include "common/endian.h"
#include "common/list.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "interspective/debugger.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/program.h"
#include "interspective/resources.h"

// this has to be included here or else templates
// would get instantiated to the generic handler
#include "interspective/opcode_handlers.cpp"

namespace Interspective {

class Animation;

enum {
	kOpcodeMax = 0xfd
};

template<int opcode>
Interpreter::OpResult Interpreter::opcodeHandler(ValueVector &args, CodePointer current, CodePointer next) {
	error("unhandled opcode %d [=0x%02x] at %s", opcode, opcode, +current);
	return kThxBye;
}

template<int N>
void Interpreter::init_opcodes() {
	_handlers[N] = &Interspective::Interpreter::opcodeHandler<N>;
	init_opcodes<N - 1>();
}

template<>
void Interpreter::init_opcodes<-1>() {}

Interpreter::Interpreter(Logic *l, byte *base, uint16 codeSize, const char *n) : _logic(l),
																_engine(l->engine()),
																_resources(_engine->resources()),
																_base(base),
																_codeSize(codeSize),
																_roomLoop(0) {
	init_opcodes<255>();
	Common::strlcpy(_name, n ? n : "", sizeof(_name));
	init();
}

Interpreter::~Interpreter() {
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end(); ++it) {
		_logic->removeAnimation(*it);
		delete *it;
	}
	_animations.clear();
}

void Interpreter::tick() {
	if (_roomLoop) {
		if (!containsCodePointer(_roomLoop)) {
			warning("Interspective: room-loop pointer outside %s code segment", name());
			_roomLoop = 0;
			return;
		}
		run(uint16(_roomLoop - _base), kCodeRoomLoop);
	}
}

void Interpreter::setRoomLoop(byte *code) {
	_roomLoop = code;
}

/* mode:
0 - initialization,
1 - room handler,
8 - dataset init
*/

void Interpreter::init() {
	_graphics = _engine->graphics();
}

bool Interpreter::containsCodeRange(uint16 offset, uint16 size) const {
	if (!_base)
		return false;
	return offset <= _codeSize && size <= _codeSize - offset;
}

bool Interpreter::containsCodePointer(const byte *ptr, uint16 size) const {
	if (!_base || !ptr)
		return false;

	const uintptr base = reinterpret_cast<uintptr>(_base);
	const uintptr address = reinterpret_cast<uintptr>(ptr);
	if (address < base)
		return false;

	const uintptr offset = address - base;
	return offset <= _codeSize && size <= _codeSize - offset;
}

bool Interpreter::memoryReference(uint16 offset, DosMemoryReference &ref) const {
	if (!containsCodeRange(offset)) {
		warning("Interspective: memory reference 0x%04x outside %s segment size %u",
				offset, name(), uint(_codeSize));
		ref = DosMemoryReference();
		return false;
	}
	ref = DosMemoryReference(_base, _codeSize, offset);
	return true;
}

bool Interpreter::memoryReference(const byte *ptr, DosMemoryReference &ref) const {
	if (!containsCodePointer(ptr)) {
		warning("Interspective: memory pointer outside %s segment", name());
		ref = DosMemoryReference();
		return false;
	}
	ref = DosMemoryReference(_base, _codeSize, uint16(ptr - _base));
	return true;
}

byte *Interpreter::rawCodeChecked(uint16 offset, uint16 size) const {
	if (!containsCodeRange(offset, size)) {
		warning("Interspective: code range 0x%04x+%u outside %s segment size %u",
				offset, uint(size), name(), uint(_codeSize));
		return 0;
	}
	return _base + offset;
}

Status Interpreter::run(uint16 offset, OpcodeMode mode) {
	_mode = mode;
	// Capture the script's entry offset for diagnostics. DOS updates
	// g_block_start_di/es before every opcode dispatch; wait opcodes that call
	// RegisterSampleSlot_LoadDefaultsAndMark requeue the current opcode, not
	// this entry offset.
	_runEntry = offset;
	// Mirror DOS `g_opcode_mode = mode_value` set by the script-dispatch
	// sites (RunEntityScript, runQueued, etc.). Op_3a/Op_3d read this
	// to decide deferred-mode dispatch behaviour.
	Logic::instance().setOpcodeMode(uint16(mode));
	// InterpretBytecode @ 1000:2ca4 resets the call-stack depth and
	// switch branch state at each top-level script entry. The private
	// run(offset) path used by Op_36 stays untouched so in-script calls
	// preserve the active stack frame.
	Logic::instance().setCallDepth(0);
	Logic::instance().setBranchState(0);
	Logic::instance().setBreakInner(false);
	return run(offset);
}

Status Interpreter::run(uint16 offset) {
	return run(offset, 0);
}

Status Interpreter::run(uint16 offset, int ifDepth) {
	if (!containsCodeRange(offset)) {
		warning("Interspective: refusing to run invalid %s code offset 0x%04x",
				name(), offset);
		return kInvalidOpcode;
	}

	byte *last;
	byte *code;
	last = code = rawCodeChecked(offset);

	int if_depth = ifDepth;
	while (true) {
		if (!containsCodePointer(code)) {
			warning("Interspective: bytecode pointer escaped %s segment", name());
			return kInvalidOpcode;
		}

		byte opcode = *code;
		last = code;

		if (opcode > kOpcodeMax) {
			return kInvalidOpcode;
		}

		uint8 nargs = _argumentsCounts[opcode];

		OpcodeHandler handler = _handlers[opcode];

		ValueVector args;

		for (uint i = 0; i < nargs; i++) {
			if (!containsCodePointer(code, 2)) {
				warning("Interspective: truncated argument list in %s at offset 0x%04x",
						name(), uint16(last - _base));
				return kInvalidOpcode;
			}
			args.push_back(getArgument(code));
		}

		if (nargs == 0) {
			if (!containsCodePointer(code, 2)) {
				warning("Interspective: truncated zero-argument opcode in %s at offset 0x%04x",
						name(), uint16(last - _base));
				return kInvalidOpcode;
			}
			code += 2;
		}

		OpResult result(kThxBye);

		if (opcode == 0x2c || opcode == 0x2d || opcode == 1 || if_depth == 0) {
			Debug.opcodeStep();
			result = (this->*handler)(args, CodePointer(last - _base, this), CodePointer(code - _base, this));
		} else {
			debugC(3, kDebugLevelScript, "opcode 0x%02x skipped", opcode);
			if (opcode > 1 && opcode < 0x26)
				result = kFail;
		}

		switch (result.code) {
		case kReturn:
			return kReturned;
		case kFail:
			if_depth++;
			break;
		case kElse:
			// DOS Op_2c: `if (skip < 2) skip ^= 1` — flips the skip flag at depth
			// 0 or 1, leaves deeper nesting alone. The previous engine code only
			// covered the 1->0 direction (entering the else from a skipped if),
			// so when an if-block ran successfully we kept executing into the
			// else block too. Fix matches the binary XOR.
			if (if_depth < 2)
				if_depth ^= 1;
			break;
		case kEndIf:
			if_depth = MAX(if_depth - 1, 0);
			break;
		case kJump: {
			Interpreter *target = result.address.interpreter();
			if (target && target != this)
				return target->run(result.address.offset(), if_depth);
			if (!containsCodeRange(result.address.offset())) {
				warning("Interspective: invalid jump target 0x%04x in %s",
						result.address.offset(), name());
				return kInvalidOpcode;
			}
			code = _base + result.address.offset();
			break;
		}
		case kThxBye:
			// ok
			;
		}

		// DOS MainGameLoop @ 1000:050d calls DisplayIllError @ 1000:35cd
		// each frame: it shows a one-shot "ILL Error <code> (<mode>)"
		// overlay, clears the code, and CONTINUES (g_flag_room_loaded=1).
		// DOS errors are recoverable — they do NOT halt the program. So
		// report once per distinct code (DOS g_lastErrorCode dedup) and
		// keep running, instead of aborting the whole engine.
		Logic &log = Logic::instance();
		if (log.pendingError() != 0) {
			const uint8 pendingCode = log.pendingError();
			log.clearPendingError();
			if (pendingCode != log.lastErrorCode()) {
				log.setLastErrorCode(pendingCode);
				warning("Interspective ILL Error 0x%02x %s [opcode 0x%02x] — recovering (DOS DisplayIllError)",
						pendingCode, log.opcodeModeName(), opcode);
			}
		}
	}

	return kReturned;
}

enum ArgumentTypes {
	kArgumentImmediate = 1,
	kArgumentMainWord = 2,
	kArgumentMainByte = 3,
	kArgumentFieldByte = 4,
	kArgumentFieldWord = 5,
	kArgumentFieldWordAlt = 6,
	kArgumentString = 7,
	kArgumentList = 8,
	kArgumentCode = 9
};

template<>
Constant *Interpreter::readArgument<Constant>(byte *&code) {
	if (!containsCodePointer(code, 2)) {
		warning("Interspective: truncated immediate argument in %s", name());
		code = _base + _codeSize;
		return new Constant(0);
	}
	uint16 value = READ_LE_UINT16(code);
	code += 2;
	debugC(4, kDebugLevelScript, "read constant value %d as argument", value);
	return new Constant(value);
}

class GlobalByteVariable : public ByteVariable {
public:
	GlobalByteVariable(uint16 index, Resources *res) : ByteVariable(res->getGlobalByteVariable(index)), _index(index) {}
	virtual const char *operator+() const {
		snprintf(_inspect, 27, "global byte variable %d [%d]", _index, byte(*this));
		return _inspect;
	}

private:
	mutable char _inspect[27];
	const uint16 _index;
};

class GlobalWordVariable : public WordVariable {
public:
	GlobalWordVariable(uint16 index, Resources *res) : WordVariable(res->getGlobalWordVariable(index)), _index(index) {}
	virtual const char *operator+() const {
		snprintf(_inspect, 33, "global word variable %d [%d]", _index, uint16(*this));
		return _inspect;
	}

private:
	mutable char _inspect[33];
	const uint16 _index;
};

class RecordFieldVariable : public Value {
public:
	RecordFieldVariable(Logic *logic, uint8 selector, uint16 id, uint8 offset, uint8 size)
		: _logic(logic), _selector(selector), _id(id), _offset(offset), _size(size) {}
	virtual operator uint16() const {
		return _logic ? _logic->recordField(_selector, _id, _offset, _size) : 0;
	}
	virtual Value &operator=(uint16 value) {
		if (_logic)
			_logic->setRecordField(_selector, _id, _offset, _size, value);
		return *this;
	}
	virtual Value &operator=(const Value &other) { return *this = uint16(other); }
	virtual const char *operator+() const {
		snprintf(_inspect, sizeof(_inspect), "record[%u:%u]+0x%02x/%u [%u]",
				 _selector, _id, _offset, _size, uint16(*this));
		return _inspect;
	}

private:
	Logic *_logic;
	uint8 _selector;
	uint16 _id;
	uint8 _offset;
	uint8 _size;
	mutable char _inspect[48];
};

class RawPointerArgument : public Value {
public:
	RawPointerArgument(const DosMemoryReference &ref) : _ref(ref) {}
	virtual operator uint16() const {
		return _ref.valid() ? _ref.offset() : 0;
	}
	virtual operator byte *() { return _ref.ptr(); }
	virtual byte *rawPointer() { return _ref.ptr(); }
	virtual byte *rawBase() { return _ref.base(); }
	virtual bool memoryReference(DosMemoryReference &ref) const {
		ref = _ref;
		return ref.valid();
	}
	virtual const char *operator+() const {
		snprintf(_inspect, sizeof(_inspect), "raw pointer 0x%04x", uint16(*this));
		return _inspect;
	}

private:
	DosMemoryReference _ref;
	mutable char _inspect[32];
};

template<>
GlobalByteVariable *Interpreter::readArgument<GlobalByteVariable>(byte *&code) {
	if (!containsCodePointer(code, 2)) {
		warning("Interspective: truncated global byte argument in %s", name());
		code = _base + _codeSize;
		return new GlobalByteVariable(0, _resources);
	}
	uint16 index = READ_LE_UINT16(code);
	code += 2;
	debugC(4, kDebugLevelScript, "read global byte variable %d as argument", index);
	return new GlobalByteVariable(index, _resources);
}

template<>
GlobalWordVariable *Interpreter::readArgument<GlobalWordVariable>(byte *&code) {
	if (!containsCodePointer(code, 2)) {
		warning("Interspective: truncated global word argument in %s", name());
		code = _base + _codeSize;
		return new GlobalWordVariable(0, _resources);
	}
	uint16 index = READ_LE_UINT16(code) / 2;
	code += 2;
	debugC(4, kDebugLevelScript, "read global word variable %d as argument", index);
	return new GlobalWordVariable(index, _resources);
}

template<>
CodePointer *Interpreter::readArgument<CodePointer>(byte *&code) {
	if (!containsCodePointer(code, 2)) {
		warning("Interspective: truncated code-pointer argument in %s", name());
		code = _base + _codeSize;
		return new CodePointer();
	}
	uint16 offset = READ_LE_UINT16(code);
	code += 2;
	debugC(4, kDebugLevelScript, "read code offset 0x%04x as argument", offset);
	return new CodePointer(offset, this);
}

class ParametrizedString : public Value {
public:
	ParametrizedString(byte *translated, uint16 len, const DosMemoryReference &rawRef, uint16 rawLength)
		: _rawRef(rawRef), _rawLength(rawLength) {
		memcpy(_translateBuf, translated, len);
		_length = len;
	}
	virtual const char *operator+() const {
		return reinterpret_cast<const char *>(_translateBuf);
	}
	virtual operator byte *() { return _translateBuf; }
	virtual operator uint16() const { return _length; }
	virtual byte *rawPointer() { return _rawRef.ptr(); }
	virtual byte *rawBase() { return _rawRef.base(); }
	virtual uint16 rawLength() const { return _rawLength; }
	virtual bool memoryReference(DosMemoryReference &ref) const {
		ref = _rawRef;
		return ref.valid();
	}

private:
	byte _translateBuf[500];
	uint16 _length;
	DosMemoryReference _rawRef;
	uint16 _rawLength;
};

static bool canReadBytes(const byte *code, const byte *end, uint count) {
	return !end || (code && code <= end && uint(end - code) >= count);
}

static bool appendDecodedByte(byte *&dst, byte *dstEnd, byte value) {
	if (dst >= dstEnd)
		return false;
	*dst++ = value;
	return true;
}

static bool decodeParametrizedString(Resources *resources, byte *base, byte *&code, const byte *end,
									 byte *translateBuf, uint16 translateBufSize,
									 uint16 &translatedLength, byte *&raw, uint16 &rawLength) {
	byte *str = translateBuf;
	byte *const strEnd = translateBuf + translateBufSize - 1;
	raw = code;
	bool rawTerminated = false;

	while (!rawTerminated) {
		if (!canReadBytes(code, end, 1))
			return false;
		const byte ch = *(code++);
		if (ch == 0)
			break;

		uint16 offset = 0;
		uint16 value = 0;
		switch (ch) {
		case 14:
		case kStringMove:
			if (!canReadBytes(code, end, 4) ||
				!appendDecodedByte(str, strEnd, ch) ||
				!appendDecodedByte(str, strEnd, *(code++)) ||
				!appendDecodedByte(str, strEnd, *(code++)) ||
				!appendDecodedByte(str, strEnd, *(code++)) ||
				!appendDecodedByte(str, strEnd, *(code++)))
				return false;
			break;
		case kStringAdvance:
			if (!canReadBytes(code, end, 1) ||
				!appendDecodedByte(str, strEnd, ch) ||
				!appendDecodedByte(str, strEnd, *(code++)))
				return false;
			break;
		case kStringGlobalWord: {
			if (!resources || !canReadBytes(code, end, 2))
				return false;
			offset = READ_LE_UINT16(code);
			code += 2;
			value = READ_LE_UINT16(resources->getGlobalWordVariable(offset / 2));
			const uint remaining = uint(strEnd - str);
			const int written = snprintf(reinterpret_cast<char *>(str), remaining + 1, "%d", value);
			if (written < 0 || uint(written) > remaining)
				return false;
			str += written;
			break;
		}
		case kStringSetColour:
			if (!canReadBytes(code, end, 1) ||
				!appendDecodedByte(str, strEnd, ch) ||
				!appendDecodedByte(str, strEnd, *(code++)))
				return false;
			break;
		case kStringCountSpacesIf0:
		case kStringCountSpacesIf1: {
			if (!resources || !canReadBytes(code, end, 2))
				return false;
			// DOS FormatBubbleText_Inner consumes a two-byte global-byte
			// offset after these conditional markers and skips forward to
			// STX (0x02) when the condition matches. The raw string remains
			// available via rawPointer(); the translated buffer should only
			// contain text that survives the same condition.
			offset = READ_LE_UINT16(code);
			code += 2;
			const byte state = *resources->getGlobalByteVariable(offset);
			const bool skip = (ch == kStringCountSpacesIf0) ? (state == 0) : (state != 0);
			if (skip) {
				while (true) {
					if (!canReadBytes(code, end, 1))
						return false;
					const byte skipped = *(code++);
					if (skipped == 0) {
						rawTerminated = true;
						break;
					}
					if (skipped == kStringCountSpacesTerminate)
						break;
				}
			}
			break;
		}
		case kStringCountSpacesTerminate:
			break;
		case '\r':
			if (!appendDecodedByte(str, strEnd, '\n'))
				return false;
			break;
		default:
			if (ch == kStringMenuOption) {
				if (!appendDecodedByte(str, strEnd, ch))
					return false;
				while (true) {
					if (!canReadBytes(code, end, 1))
						return false;
					const byte optionCh = *(code++);
					if (!appendDecodedByte(str, strEnd, optionCh))
						return false;
					if (optionCh == 0)
						break;
				}
				if (!canReadBytes(code, end, 2) ||
					!appendDecodedByte(str, strEnd, *(code++)) ||
					!appendDecodedByte(str, strEnd, *(code++)))
					return false;
			} else if (!appendDecodedByte(str, strEnd, ch)) {
				return false;
			}
		}
	}

	if (!appendDecodedByte(str, translateBuf + translateBufSize, 0))
		return false;
	translatedLength = uint16(str - translateBuf);
	rawLength = uint16(code - raw);
	(void)base;
	return true;
}

template<>
ParametrizedString *Interpreter::readArgument<ParametrizedString>(byte *&code) {
	byte translateBuf[500];
	byte *raw = code;
	uint16 translatedLength = 0;
	uint16 rawLength = 0;
	const byte *end = _base + _codeSize;
	if (!decodeParametrizedString(_resources, _base, code, end, translateBuf, sizeof(translateBuf),
								  translatedLength, raw, rawLength))
		error("malformed parametrized string argument");

	debugC(4, kDebugLevelScript, "read parametrized string '%s' as argument", translateBuf);

	DosMemoryReference rawRef;
	memoryReference(raw, rawRef);
	return new ParametrizedString(translateBuf, translatedLength, rawRef, rawLength);
}

static bool scanBytecodeArgument(Resources *resources, byte *base, byte *&code, const byte *end, Common::String *stringOut) {
	if (!canReadBytes(code, end, 2))
		return false;

	const uint8 argumentType = code[1];
	code += 2;

	switch (argumentType) {
	case kArgumentImmediate:
	case kArgumentMainWord:
	case kArgumentMainByte:
	case kArgumentCode:
		if (!canReadBytes(code, end, 2))
			return false;
		code += 2;
		return true;
	case kArgumentFieldByte:
	case kArgumentFieldWord:
	case kArgumentFieldWordAlt:
		if (!canReadBytes(code, end, 4))
			return false;
		code += 4;
		return true;
	case kArgumentString: {
		byte translateBuf[500];
		byte *raw = 0;
		uint16 translatedLength = 0;
		uint16 rawLength = 0;
		if (!decodeParametrizedString(resources, base, code, end, translateBuf, sizeof(translateBuf),
									  translatedLength, raw, rawLength))
			return false;
		if (stringOut)
			*stringOut = Common::String(reinterpret_cast<const char *>(translateBuf));
		return true;
	}
	case kArgumentList:
		while (true) {
			if (!canReadBytes(code, end, 1))
				return false;
			if (*(code++) == 0xff)
				return true;
		}
	default:
		return false;
	}
}

static Common::String plainFirstLineForHover(const Common::String &text) {
	Common::String line;
	const byte *p = reinterpret_cast<const byte *>(text.c_str());
	while (*p) {
		const byte ch = *p++;
		if (ch == '\r' || ch == '\n')
			break;
		switch (ch) {
		case kStringMove:
		case 14:
			p += 4;
			break;
		case kStringSetColour:
		case kStringAdvance:
		case kStringCenter:
			++p;
			break;
		case kStringDefaultColour:
		case 4:
		case kStringCountSpacesTerminate:
			break;
		default:
			if (ch >= 0x20)
				line += char(ch);
			break;
		}
	}
	line.trim();
	return line;
}

bool Interpreter::extractFirstStatusOverlayLine(uint16 offset, Common::String &text) {
	text.clear();
	if (!_base || offset >= _codeSize)
		return false;

	byte *code = _base + offset;
	const byte *end = _base + _codeSize;
	enum {
		kMaxScannedOpcodes = 512
	};

	for (uint scanned = 0; scanned < kMaxScannedOpcodes; ++scanned) {
		if (!canReadBytes(code, end, 1))
			return false;

		const byte opcode = *code;
		if (opcode > kOpcodeMax)
			return false;

		const uint8 nargs = _argumentsCounts[opcode];
		Common::String statusText;
		if (nargs == 0) {
			if (!canReadBytes(code, end, 2))
				return false;
			code += 2;
		} else {
			for (uint i = 0; i < nargs; ++i) {
				const bool captureStatusText = opcode == 0x28 && i == 1;
				if (!scanBytecodeArgument(_resources, _base, code, end,
										  captureStatusText ? &statusText : 0))
					return false;
			}
		}

		if (opcode == 0x28) {
			text = plainFirstLineForHover(statusText);
			return !text.empty();
		}
		if (opcode == 0x01)
			return false;
	}

	return false;
}

Value *Interpreter::getArgument(byte *&code) {
	if (!containsCodePointer(code, 2)) {
		warning("Interspective: argument descriptor outside %s code segment", name());
		return new Constant(0);
	}

	uint8 argument_type = code[1];
	code += 2;

	switch (argument_type) {
	case kArgumentImmediate:
		return readArgument<Constant>(code);
	case kArgumentMainWord:
		return readArgument<GlobalWordVariable>(code);
	case kArgumentMainByte:
		return readArgument<GlobalByteVariable>(code);
	case kArgumentFieldByte:
	case kArgumentFieldWord:
	case kArgumentFieldWordAlt: {
		if (!containsCodePointer(code, 4)) {
			warning("Interspective: truncated record-field argument in %s", name());
			code = _base + _codeSize;
			return new Constant(0);
		}
		const uint8 selector = code[0];
		const uint8 offset = code[1];
		const uint16 id = READ_LE_UINT16(code + 2);
		code += 4;
		const uint8 size = argument_type == kArgumentFieldByte ? 1 : 2;
		debugC(4, kDebugLevelScript,
			   "read record field selector=%u id=%u offset=0x%02x size=%u as argument",
			   selector, id, offset, size);
		return new RecordFieldVariable(_logic, selector, id, offset, size);
	}
	case kArgumentString:
		return readArgument<ParametrizedString>(code);
	case kArgumentList: {
		byte *ptr = code;
		while (containsCodePointer(code) && *code != 0xff)
			++code;
		DosMemoryReference ref;
		memoryReference(ptr, ref);
		if (!containsCodePointer(code)) {
			warning("Interspective: unterminated list argument in %s", name());
			return new RawPointerArgument(ref);
		}
		++code;
		debugC(4, kDebugLevelScript, "read raw list at offset 0x%04x as argument",
			   uint16(ptr - _base));
		return new RawPointerArgument(ref);
	}
	case kArgumentCode:
		return readArgument<CodePointer>(code);
	default:
		error("don't know how to handle argument type 0x%02x", argument_type);
	}
}

const uint8 Interpreter::_argumentsCounts[] = {
#include "opcodes_nargs.data"
};

} // End of namespace Interspective
