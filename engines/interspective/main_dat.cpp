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

#include "interspective/main_dat.h"

#include "common/span.h"
#include "common/util.h"

#include "interspective/actor.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/util.h"
#include "interspective/value.h"

using namespace Common;

namespace Interspective {

MainDat::MainDat(Resources *res) : Datafile(res), _dataLen(0) {}

const char *MainDat::filename() const {
	return Engine::instance().mainDatFilename().c_str();
}

enum Offsets {
	kProgEntriesCount0 = 0x06,
	kProgEntriesCount1 = 0x08,
	kProgramsMap = 0x0A,
	// kPersonsCount = 0x0C → DOS CS:[0x6b]. Used by Op_7f as the bound
	// for object id; effectively also the global object-state table size.
	kPersonsCount = 0x0C,
	// kGlobalObjectStateList = 0x0E → DOS CS:[0x6d]. Pointer (within the
	// data segment) to an array of 18-byte object-state records. Each
	// record's first uint16 is the object's current room id (0 = missing,
	// 0xffff = unplaced). Op_18/Op_1b/Op_21/Op_7f read or write this field.
	kGlobalObjectStateList = 0x0E,
	kActorsCount = 0x10,
	kActors = 0x12,
	kPuppeteersCount = 0x14,
	kPuppeteers = 0x16,
	kSpriteCount = 0x18,
	kSpriteMap = 0x1A,
	kImagesCount = 0x1C,
	kImageDirectory = 0x1E,
	kGraphicFileCount = 0x20,
	kGraphicFileNames = 0x22,
	kSfxSampleCount = 0x24,
	kSfxFileNames = 0x26,
	kSfxFileCount = 0x28,
	kTunesCount = 0x2A,
	kTunesDirectory = 0x2C,
	kMusicFileCount = 0x2E,
	kMusicFileNames = 0x30,
	kMaxGameScore = 0x32,
	kScoreEventCount = 0x34,
	kScoreEventTable = 0x36,
	kWordVars = 0x3A,
	kByteVars = 0x3E,
	kRoomLoopEntryPoint = 0x40,
	kEntryPoint = 0x42,
	kCharacterMap = 0x48,
	kCursors = 0x54,
	kMenuCursors = 0x58,
	kInterfaceImgIdx = 0xB4,

	// DrawDialogChoices @ 1000:b2e8 uses these four main-footer sprites
	// as mini-map exit markers, selected by Op_e4's second argument.
	kInterfaceMapMarker1Offset = 0x62,
	kInterfaceMapMarker5Offset = 0x64,
	kInterfaceMapMarker3Offset = 0x66,
	kInterfaceMapMarker7Offset = 0x68,

	// UpdateAutoCloseTimer @ 1000:7a2b draws these footer sprites at
	// (0x40,0xbe). The loaded footer begins at DOS CS:[0x5f], so
	// CS:[0xc9]/[0xcb] are footer offsets 0x6a/0x6c.
	kStatusButtonNormalOffset = 0x6a,
	kStatusButtonStatusOffset = 0x6c,

	kFrameTopLeftOffset = 0x76,
	kFrameTopOffset = 0x7e,
	kFrameTopRightOffset = 0x84,
	kFrameLeftOffset = 0x7a,
	kFrameFillOffset = 0x80,
	kFrameRightOffset = 0x88,
	kFrameBottomLeftOffset = 0x7c,
	kFrameBottomOffset = 0x82,
	kFrameBottomRightOffset = 0x8a,

	kBubbleTopLeftOffset = 0x8e,
	kBubbleLeftOffset = 0x90,
	kBubbleBottomLeftOffset = 0x92,
	kBubbleTopOffset = 0x94,
	kBubbleFillOffset = 0x96,
	kBubbleBottomOffset = 0x98,
	kBubbleTopRightOffset = 0x9a,
	kBubbleRightOffset = 0x9c,
	kBubbleBottomRightOffset = 0x9e,

	kBubbleBottomLeftPointOffset = 0xa0,
	kBubbleBottomRightPointOffset = 0xa2,
	kBubbleTopLeftPointOffset = 0xa4,
	kBubbleTopRightPointOffset = 0xa6,
	kBubbleVerbConnectorOffset = 0xa8,
	kBubbleVerbStemOffset = 0xaa,

	// DrawCursorWithBackdrop @ 1000:c800 draws the two eye close-up panel
	// halves from CS:[0x10b] and CS:[0x10d]. The loaded footer begins at
	// DOS CS:[0x5f], so these are footer offsets 0xac and 0xae.
	kEyeCloseUpLeftOffset = 0xac,
	kEyeCloseUpRightOffset = 0xae,

	// DOS CycleAllAnimationsByMask @ 1000:c8a1 reads CS:[0xa9],
	// [0xab], [0xaf], [0xad], [0xb1]. The loaded footer begins at
	// DOS CS:[0x5f], so these are footer offsets 0x4a..0x52.
	kCursorOverlayBit01Offset = 0x4a,
	kCursorOverlayBit02Offset = 0x4c,
	kCursorOverlayBit04Offset = 0x4e,
	kCursorOverlayBit10Offset = 0x50,
	kCursorOverlayBit08Offset = 0x52
};

namespace {

void writeUint16LE(Common::Span<byte> data, uint32 offset, uint16 value) {
	data[offset] = uint8(value & 0xff);
	data[offset + 1] = uint8(value >> 8);
}

class MainDatFooter {
public:
	MainDatFooter(const byte *footer) : _footer(footer, 0xB6) {}

	uint16 progEntriesCount0() const { return wordAt(kProgEntriesCount0); }
	uint16 progEntriesCount1() const { return wordAt(kProgEntriesCount1); }
	uint16 programsMapOffset() const { return wordAt(kProgramsMap); }
	uint16 personsCount() const { return wordAt(kPersonsCount); }
	uint16 globalObjectStateListOffset() const { return wordAt(kGlobalObjectStateList); }
	uint16 actorsCount() const { return wordAt(kActorsCount); }
	uint16 actorsOffset() const { return wordAt(kActors); }
	uint16 puppeteersCount() const { return wordAt(kPuppeteersCount); }
	uint16 puppeteersOffset() const { return wordAt(kPuppeteers); }
	uint16 spriteCount() const { return wordAt(kSpriteCount); }
	uint16 spriteMapOffset() const { return wordAt(kSpriteMap); }
	uint16 imagesCount() const { return wordAt(kImagesCount); }
	uint16 imageDirectoryOffset() const { return wordAt(kImageDirectory); }
	uint16 graphicFileCount() const { return wordAt(kGraphicFileCount); }
	uint16 graphicFileNamesOffset() const { return wordAt(kGraphicFileNames); }
	uint16 sfxSampleCount() const { return wordAt(kSfxSampleCount); }
	uint16 sfxFileNamesOffset() const { return wordAt(kSfxFileNames); }
	uint16 sfxFileCount() const { return wordAt(kSfxFileCount); }
	uint16 tunesCount() const { return wordAt(kTunesCount); }
	uint16 tunesDirectoryOffset() const { return wordAt(kTunesDirectory); }
	uint16 musicFileCount() const { return wordAt(kMusicFileCount); }
	uint16 musicFileNamesOffset() const { return wordAt(kMusicFileNames); }
	uint16 maxGameScore() const { return wordAt(kMaxGameScore); }
	uint16 scoreEventCount() const { return wordAt(kScoreEventCount); }
	uint16 scoreEventTableOffset() const { return wordAt(kScoreEventTable); }
	uint16 wordVarsOffset() const { return wordAt(kWordVars); }
	uint16 byteVarsOffset() const { return wordAt(kByteVars); }
	uint16 roomLoopEntryPoint() const { return wordAt(kRoomLoopEntryPoint); }
	uint16 entryPointOffset() const { return wordAt(kEntryPoint); }
	uint16 characterMapOffset() const { return wordAt(kCharacterMap); }
	uint16 interfaceImageIndex() const { return wordAt(kInterfaceImgIdx); }

	uint16 wordAt(uint16 offset) const { return _footer.getUint16LEAt(offset); }

	uint16 cursorOverlayRecordOffset(uint16 maskBit) const {
		switch (maskBit) {
		case 0x01:
			return wordAt(kCursorOverlayBit01Offset);
		case 0x02:
			return wordAt(kCursorOverlayBit02Offset);
		case 0x04:
			return wordAt(kCursorOverlayBit04Offset);
		case 0x08:
			return wordAt(kCursorOverlayBit08Offset);
		case 0x10:
			return wordAt(kCursorOverlayBit10Offset);
		default:
			return 0;
		}
	}

	uint16 interfaceMapMarkerSpriteId(uint16 selector) const {
		switch (selector) {
		case 1:
			return wordAt(kInterfaceMapMarker1Offset);
		case 5:
			return wordAt(kInterfaceMapMarker5Offset);
		case 3:
			return wordAt(kInterfaceMapMarker3Offset);
		case 7:
			return wordAt(kInterfaceMapMarker7Offset);
		default:
			return 0xffff;
		}
	}

	uint16 statusButtonSpriteId(bool statusMode) const {
		return wordAt(statusMode ? kStatusButtonStatusOffset : kStatusButtonNormalOffset);
	}

	uint16 eyeCloseUpSpriteId(bool rightHalf) const {
		return wordAt(rightHalf ? kEyeCloseUpRightOffset : kEyeCloseUpLeftOffset);
	}

	uint16 frameSpriteId(FramePart part) const {
		switch (part) {
		case kFrameBottom:
			return wordAt(kFrameBottomOffset);
		case kFrameBottomLeft:
			return wordAt(kFrameBottomLeftOffset);
		case kFrameBottomRight:
			return wordAt(kFrameBottomRightOffset);
		case kFrameFill:
			return wordAt(kFrameFillOffset);
		case kFrameLeft:
			return wordAt(kFrameLeftOffset);
		case kFrameRight:
			return wordAt(kFrameRightOffset);
		case kFrameTop:
			return wordAt(kFrameTopOffset);
		case kFrameTopLeft:
			return wordAt(kFrameTopLeftOffset);
		case kFrameTopRight:
			return wordAt(kFrameTopRightOffset);
		default:
			assert(false);
			return 0;
		}
	}

	uint16 bubbleSpriteId(SpeechBubblePart part) const {
		switch (part) {
		case kBubbleTopLeft:
			return wordAt(kBubbleTopLeftOffset);
		case kBubbleLeft:
			return wordAt(kBubbleLeftOffset);
		case kBubbleBottomLeft:
			return wordAt(kBubbleBottomLeftOffset);
		case kBubbleTop:
			return wordAt(kBubbleTopOffset);
		case kBubbleFill:
			return wordAt(kBubbleFillOffset);
		case kBubbleBottom:
			return wordAt(kBubbleBottomOffset);
		case kBubbleTopRight:
			return wordAt(kBubbleTopRightOffset);
		case kBubbleRight:
			return wordAt(kBubbleRightOffset);
		case kBubbleBottomRight:
			return wordAt(kBubbleBottomRightOffset);
		case kBubbleBottomLeftPoint:
			return wordAt(kBubbleBottomLeftPointOffset);
		case kBubbleBottomRightPoint:
			return wordAt(kBubbleBottomRightPointOffset);
		case kBubbleTopLeftPoint:
			return wordAt(kBubbleTopLeftPointOffset);
		case kBubbleTopRightPoint:
			return wordAt(kBubbleTopRightPointOffset);
		case kBubbleVerbConnector:
			return wordAt(kBubbleVerbConnectorOffset);
		case kBubbleVerbStem:
			return wordAt(kBubbleVerbStemOffset);
		default:
			assert(false);
			return 0;
		}
	}

private:
	Common::Span<const byte> _footer;
};

class MainDatSegment {
public:
	MainDatSegment(byte *data, uint32 size) : _readData(data), _writeData(data), _size(size) {}
	MainDatSegment(const byte *data, uint32 size) : _readData(data), _writeData(0), _size(size) {}

	bool contains(uint32 offset, uint32 size) const {
		return offset <= _size && size <= _size - offset;
	}

	bool containsSigned(int32 offset, uint32 size) const {
		return offset >= 0 && contains(uint32(offset), size);
	}

	Common::Span<const byte> span(uint32 offset) const {
		assert(offset <= _size);
		return Common::Span<const byte>(_readData + offset, _size - offset);
	}

	Common::Span<const byte> span(uint32 offset, uint32 size) const {
		assert(contains(offset, size));
		return Common::Span<const byte>(_readData + offset, size);
	}

	Common::Span<byte> mutableSpan(uint32 offset, uint32 size) const {
		assert(_writeData);
		assert(contains(offset, size));
		return Common::Span<byte>(_writeData + offset, size);
	}

	byte *mutablePtr(uint32 offset) const {
		return mutableSpan(offset, _size - offset).data();
	}

	uint8 byteAt(uint32 offset) const { return span(offset, 1).getUint8At(0); }
	void writeByteAt(uint32 offset, uint8 value) const { mutableSpan(offset, 1)[0] = value; }
	uint16 wordAt(uint32 offset) const { return span(offset, 2).getUint16LEAt(0); }
	void writeWordAt(uint32 offset, uint16 value) const { writeUint16LE(mutableSpan(offset, 2), 0, value); }

private:
	const byte *_readData;
	byte *_writeData;
	uint32 _size;
};

class MainDatImageEntry {
public:
	MainDatImageEntry(Common::Span<const byte> entry) : _readEntry(entry) {}
	MainDatImageEntry(Common::Span<byte> entry) : _readEntry(entry), _writeEntry(entry) {}

	uint16 type() const { return _readEntry.getUint16LEAt(0); }
	uint16 fileIndex() const { return _readEntry.getUint16LEAt(2); }

	void setType(uint16 type) const {
		assert(_writeEntry);
		writeUint16LE(_writeEntry, 0, type);
	}

private:
	Common::Span<const byte> _readEntry;
	Common::Span<byte> _writeEntry;
};

class ProgramMapTable {
public:
	ProgramMapTable(Common::Span<const byte> table, uint16 programCount) : _table(table), _programCount(programCount) {}

	uint16 roomScriptId(uint16 room) const {
		uint32 programInfoOffset = 0;
		for (uint16 i = 1; i <= _programCount; i++) {
			// DOS LoadRoomFromProgDat @ 1000:1ad9 reads one resource-set word
			// before scanning the room ids, and advances past every word it
			// reads, including the 0xffff terminator.
			programInfoOffset += 2;

			for (;;) {
				const uint16 thisRoom = _table.getUint16LEAt(programInfoOffset);
				programInfoOffset += 2;
				if (thisRoom == 0xffff)
					break;
				if (thisRoom == room)
					return i;
			}
		}

		return 0;
	}

private:
	Common::Span<const byte> _table;
	uint16 _programCount;
};

class ScoreEventEntry {
public:
	ScoreEventEntry(Common::Span<byte> entry) : _entry(entry) {}

	bool claimed() const { return _entry.getUint8At(1) != 0; }
	uint16 delta() const { return _entry.getUint16LEAt(0); }
	void markClaimed() { _entry[1] = 1; }

private:
	Common::Span<byte> _entry;
};

class ObjectStateRecord {
public:
	enum {
		kSize = 0x12
	};

	ObjectStateRecord(Common::Span<const byte> record) : _record(record) {}

	uint16 room() const { return _record.getUint16LEAt(0); }
	int16 x() const { return dosSignedWord(_record.getUint16LEAt(2)); }
	int16 y() const { return dosSignedWord(_record.getUint16LEAt(4)); }
	uint8 field(uint8 off) const { return _record.getUint8At(off); }

private:
	Common::Span<const byte> _record;
};

class MainDatStringTableCursor {
public:
	MainDatStringTableCursor(Common::Span<const byte> data) : _data(data), _offset(0) {}

	bool contains(uint32 size) const {
		return _offset <= _data.size() && size <= _data.size() - _offset;
	}

	uint16 readWord() {
		const uint16 value = peekWord();
		_offset += 2;
		return value;
	}

	uint16 peekWord() const {
		return _data.getUint16LEAt(_offset);
	}

	uint8 readByte() {
		const uint8 value = _data.getUint8At(_offset);
		++_offset;
		return value;
	}

	const char *string() const {
		return reinterpret_cast<const char *>(_data.getUnsafeDataAt(_offset));
	}

	void skipCString() {
		while (_offset < _data.size() && _data.getUint8At(_offset) != 0)
			++_offset;
		if (_offset < _data.size())
			++_offset;
	}

	void skipNullPadding() {
		while (_offset < _data.size() && _data.getUint8At(_offset) == 0)
			++_offset;
	}

private:
	Common::Span<const byte> _data;
	uint32 _offset;
};

class CursorSpriteTable {
public:
	CursorSpriteTable(Common::Span<const byte> table) : _table(table) {}

	bool nextSprite(uint16 mode, uint16 &stepIndex, bool &stepPending, uint16 &spriteId) const {
		uint32 entryOffset = 0;
		for (;;) {
			const int16 tableMode = signedWordAt(entryOffset);
			entryOffset += 2;
			if (tableMode == -2)
				return false;

			if (uint16(tableMode) == mode)
				break;

			while (signedWordAt(entryOffset) != 0)
				entryOffset += 2;
			entryOffset += 2;
		}

		for (;;) {
			const int16 frame = signedWordAt(entryOffset + uint32(stepIndex) * 2);
			if (frame == -1) {
				if (stepPending) {
					++stepIndex;
					continue;
				}
			} else if (frame != 0) {
				spriteId = uint16(frame);
				++stepIndex;
				return true;
			}

			stepIndex = 0;
			stepPending = false;
		}
	}

private:
	int16 signedWordAt(uint32 offset) const {
		return dosSignedWord(_table.getUint16LEAt(offset));
	}

	Common::Span<const byte> _table;
};

class CursorOverlayRecord {
public:
	enum {
		kHeaderSize = 8
	};

	CursorOverlayRecord(Common::Span<byte> record) : _record(record) {}

	uint16 x() const { return _record.getUint16LEAt(0); }
	uint16 y() const { return _record.getUint16LEAt(2); }
	uint16 frameCount() const { return _record.getUint16LEAt(4); }
	uint16 frameIndex() const { return _record.getUint16LEAt(6); }
	void setFrameIndex(uint16 index) const { writeUint16LE(_record, 6, index); }

private:
	Common::Span<byte> _record;
};

} // namespace

void MainDat::readFile(SeekableReadStream &stream) {
	_dataLen = stream.readUint16LE();

	_data.resize(_dataLen);
	stream.seek(0);
	stream.read(data(), _dataLen);
	Common::Span<byte> payload = MainDatSegment(data(), _dataLen).mutableSpan(2, _dataLen - 2);
	Resources::descramble(payload.data(), payload.size());

	stream.read(_footer, kFooterLen);
}

uint16 MainDat::personsCount() const {
	return MainDatFooter(_footer).personsCount();
}

uint16 MainDat::maxGameScore() const {
	return MainDatFooter(_footer).maxGameScore();
}

uint16 MainDat::scoreEventCount() const {
	return MainDatFooter(_footer).scoreEventCount();
}

bool MainDat::claimScoreEvent(uint16 eventId, uint16 &delta) {
	delta = 0;
	if (eventId >= scoreEventCount())
		return false;

	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 entryOffset = uint32(footer.scoreEventTableOffset()) + uint32(eventId) * 2;
	if (!segment.contains(entryOffset, 2)) {
		warning("MainDat::claimScoreEvent: event %u resolves outside score table (entryOff=0x%04x)",
				eventId, entryOffset);
		return false;
	}

	ScoreEventEntry entry(segment.mutableSpan(entryOffset, 2));
	if (entry.claimed())
		return false;

	delta = entry.delta();
	entry.markClaimed();
	return true;
}

void MainDat::loadObjectStates() {
	// DOS CS:[0x6b] = persons count (footer +0x0C); CS:[0x6d] = pointer
	// to object-state table (footer +0x0E). Each record is 18 bytes;
	// uint16 at offset 0 is the room id. Op_7f writes there at runtime;
	// Op_18/Op_1b/Op_21 read.
	const uint16 count = personsCount();
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint16 listOff = footer.globalObjectStateListOffset();
	const uint16 stride = 0x12; // 18 bytes per DOS GetObjectOffset

	if (listOff == 0 || count == 0) {
		debugC(1, kDebugLevelFiles, "loadObjectStates: empty table (count=%u off=0x%04x) — skipping",
			   count, listOff);
		return;
	}

	// Bounds check: ensure the table fits inside _data.
	const uint32 endOff = uint32(listOff) + uint32(count) * stride;
	if (endOff > _dataLen) {
		warning("loadObjectStates: table overflow (off=0x%04x count=%u stride=%u, dataLen=%u) — skipping",
				listOff, count, stride, _dataLen);
		return;
	}

	uint nonZero = 0;
	for (uint16 i = 0; i < count; ++i) {
		const ObjectStateRecord rec(segment.span(uint32(listOff) + uint32(i) * stride, ObjectStateRecord::kSize));
		const uint16 id = uint16(i + 1);
		const uint16 room = rec.room();
		// Object IDs are 1-based per DOS GetObjectOffset (it does DEC AX
		// before multiplying). Room 0 = missing/destroyed (default in DOS)
		// — store anyway so isObjectMissing() returns true correctly.
		Log.setObjectRoom(id, room);
		Log.setObjectPosition(id, rec.x(), rec.y());
		for (uint8 off = 6; off < stride; ++off)
			Log.setObjectField(id, off, rec.field(off));
		if (room != 0)
			++nonZero;
	}
	debugC(1, kDebugLevelFiles,
		   "MainDat::loadObjectStates: seeded %u objects (%u with non-zero room) "
		   "from footer offset 0x%04x [DOS CS:[0x6d]]",
		   count, nonZero, listOff);
}

void MainDat::loadActors(Interpreter *in) {
	const MainDatFooter footer(_footer);
	uint16 nactors = footer.actorsCount();
	uint16 actors = footer.actorsOffset();
	assert(_actors.empty());
	for (int i = 0; i < nactors; ++i) {
		Common::ScopedPtr<Actor> actor(new Actor(CodePointer(actors, in)));
		actor->setId(uint16(i + 1)); // DOS uses 1-based ids
		actor->setPuppeteer(getPuppeteer(i + 1));
		_actors.push_back(Common::move(actor));
		actors += Actor::Size;
	}
}

Puppeteer MainDat::getPuppeteer(uint16 i) const {
	if (_puppeteers.empty())
		parsePuppeteers();

	return _puppeteers[i];
}

void MainDat::parsePuppeteers() const {
	assert(_puppeteers.empty());
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	uint16 count = footer.puppeteersCount();

	for (int i = 0; i < count; i++) {
		Common::Span<const byte> record = segment.span(uint32(footer.puppeteersOffset()) + uint32(i) * Puppeteer::kSize,
													   Puppeteer::kSize);
		Puppeteer p(record);
		_puppeteers[p.actorId()] = p;
	}
}

uint16 MainDat::imagesCount() const {
	return MainDatFooter(_footer).imagesCount();
}

uint16 MainDat::tunesCount() const {
	return MainDatFooter(_footer).tunesCount();
}

uint16 MainDat::progEntriesCount0() const {
	return MainDatFooter(_footer).progEntriesCount0();
}

uint16 MainDat::progEntriesCount1() const {
	return MainDatFooter(_footer).progEntriesCount1();
}

bool MainDat::imageDirectoryEntryOffset(uint16 index, uint32 &entryOffset) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const int32 directoryOffset = int32(footer.imageDirectoryOffset());
	// DOS keeps this arithmetic in 16-bit registers:
	//   DEC AX; ADD AX,AX; ADD AX,AX; ADD DI,AX
	// so values that pass a signed bound check can wrap back before/within
	// the loaded data segment rather than becoming a large unsigned offset.
	const int16 entryDelta = int16(uint16(uint16(index - 1) * 4));
	const int32 signedEntryOffset = directoryOffset + entryDelta;
	if (!segment.containsSigned(signedEntryOffset, 4)) {
		warning("MainDat::imageDirectoryEntry: id %u resolves outside iuc_main.dat (entryOff=%d)",
				index, signedEntryOffset);
		entryOffset = 0;
		return false;
	}
	entryOffset = uint32(signedEntryOffset);
	return true;
}

uint16 MainDat::fileIndexOfImage(uint16 index) const {
	uint32 entryOffset = 0;
	if (!imageDirectoryEntryOffset(index, entryOffset))
		return 0;
	const MainDatImageEntry entry(MainDatSegment(data(), _dataLen).span(entryOffset, 4));
	return entry.fileIndex();
}

uint16 MainDat::imageType(uint16 index) const {
	uint32 entryOffset = 0;
	if (!imageDirectoryEntryOffset(index, entryOffset))
		return 0;
	const MainDatImageEntry entry(MainDatSegment(data(), _dataLen).span(entryOffset, 4));
	return entry.type();
}

void MainDat::patchImageType(uint16 index, uint16 type) {
	uint32 entryOffset = 0;
	if (!imageDirectoryEntryOffset(index, entryOffset))
		return;
	const MainDatImageEntry entry(MainDatSegment(data(), _dataLen).mutableSpan(entryOffset, 4));
	entry.setType(type);
}

uint16 MainDat::fileIndexOfTune(uint16 index) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 entryOffset = uint32(footer.tunesDirectoryOffset()) + uint32(index - 1) * 2;
	if (!segment.contains(entryOffset, 2)) {
		warning("MainDat::fileIndexOfTune: id %u resolves outside tune directory (entryOff=0x%04x)",
				index, entryOffset);
		return 0;
	}
	return segment.wordAt(entryOffset);
}

Common::List<MainDat::GraphicFile> MainDat::graphicFiles() const {
	const MainDatFooter footer(_footer);
	uint16 file_count = footer.graphicFileCount();
	uint16 names_offset = footer.graphicFileNamesOffset();

	const MainDatSegment segment(data(), _dataLen);
	MainDatStringTableCursor table(segment.span(names_offset));
	Common::List<GraphicFile> files;
	for (; file_count > 0; file_count--) {
		GraphicFile file;
		file.data_set = table.readWord();
		file.filename = table.string();
		files.push_back(file);
		table.skipCString();
		table.skipNullPadding();
	}

	return files;
}

Common::List<Common::String> MainDat::musicFiles() const {
	const MainDatFooter footer(_footer);
	uint16 file_count = footer.musicFileCount();
	uint16 names_offset = footer.musicFileNamesOffset();

	const MainDatSegment segment(data(), _dataLen);
	MainDatStringTableCursor table(segment.span(names_offset));
	Common::List<Common::String> files;
	for (; file_count > 0; file_count--) {
		table.readWord();           // data set id
		byte type = table.readByte(); // music type (1 - adlib, 4 - roland)
		debugC(2, kDebugLevelFiles | kDebugLevelMusic, "found music file %s type %d",
			   table.string(), type);
		files.push_back(Common::String(table.string()));
		table.skipCString();
	}

	return files;
}

uint16 MainDat::sfxSampleCount() const {
	return MainDatFooter(_footer).sfxSampleCount();
}

uint16 MainDat::sfxFileCount() const {
	return MainDatFooter(_footer).sfxFileCount();
}

Common::List<MainDat::SfxFile> MainDat::sfxFiles() const {
	const uint16 fileCount = sfxFileCount();
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint16 namesOffset = footer.sfxFileNamesOffset();
	MainDatStringTableCursor table(segment.span(namesOffset));
	Common::List<SfxFile> files;

	// DOS OpenSfxFile @ 1000:5ff7 walks this footer list. Each entry is:
	//   uint16 strict-low sample id, uint8 driver mode, ASCIIZ filename.
	// The high bound is the next entry's low word, except for the final
	// entry, where OpenSfxFile uses CS:[0x83] (footer +0x24 sample count).
	for (uint16 i = 0; i < fileCount && table.contains(4); ++i) {
		SfxFile file;
		file.low = table.readWord();
		file.mode = table.readByte();
		file.filename = table.string();
		table.skipCString();
		file.high = (i + 1 == fileCount || !table.contains(2))
						? sfxSampleCount()
						: table.peekWord();
		files.push_back(file);
	}

	return files;
}

byte MainDat::byteVariable(uint16 index) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 offset = uint32(footer.byteVarsOffset()) + index;
	if (!segment.contains(offset, 1)) {
		warning("MainDat::byteVariable: index %u outside iuc_main.dat (offset=0x%04x)",
				index, uint(offset));
		return 0;
	}
	return segment.byteAt(offset);
}

void MainDat::setByteVariable(uint16 index, byte value) {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 offset = uint32(footer.byteVarsOffset()) + index;
	if (!segment.contains(offset, 1)) {
		warning("MainDat::setByteVariable: index %u outside iuc_main.dat (offset=0x%04x)",
				index, uint(offset));
		return;
	}
	segment.writeByteAt(offset, value);
}

uint16 MainDat::wordVariable(uint16 index) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 offset = uint32(footer.wordVarsOffset()) + uint32(index) * 2;
	if (!segment.contains(offset, 2)) {
		warning("MainDat::wordVariable: index %u outside iuc_main.dat (offset=0x%04x)",
				index, uint(offset));
		return 0;
	}
	return segment.wordAt(offset);
}

void MainDat::setWordVariable(uint16 index, uint16 value) {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 offset = uint32(footer.wordVarsOffset()) + uint32(index) * 2;
	if (!segment.contains(offset, 2)) {
		warning("MainDat::setWordVariable: index %u outside iuc_main.dat (offset=0x%04x)",
				index, uint(offset));
		return;
	}
	segment.writeWordAt(offset, value);
}

uint16 MainDat::interfaceImageIndex() const {
	return MainDatFooter(_footer).interfaceImageIndex();
}

byte *MainDat::getEntryPoint() const {
	const MainDatFooter footer(_footer);
	MainDatSegment segment(const_cast<byte *>(data()), _dataLen);
	return segment.mutablePtr(footer.entryPointOffset());
}

uint16 MainDat::getRoomLoopEntryPoint() const {
	return MainDatFooter(_footer).roomLoopEntryPoint();
}

Actor *MainDat::actor(uint16 index) const {
	return _actors[index].get();
}

uint16 MainDat::getRoomScriptId(uint16 room) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	return ProgramMapTable(segment.span(footer.programsMapOffset()), footer.progEntriesCount1()).roomScriptId(room);
}

uint16 MainDat::getGlyphSpriteId(byte character) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint32 charOffset = uint32(footer.characterMapOffset()) + uint32(character - ' ') * 2;
	return segment.wordAt(charOffset);
}

uint16 MainDat::spriteCount() const {
	return MainDatFooter(_footer).spriteCount();
}

SpriteInfo MainDat::getSpriteInfo(uint16 index) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	if (index >= spriteCount())
		error("local sprite index given (index: 0x%04x)", index);

	const uint32 recordOffset = uint32(footer.spriteMapOffset()) + uint32(index) * SpriteInfo::kSpriteMapRecordSize;
	return SpriteInfo(segment.span(recordOffset, SpriteInfo::kSpriteMapRecordSize));
}

uint16 MainDat::getCursorSpriteId() const {
	uint16 sprite = 0x6c;
	debugC(1, kDebugLevelGraphics | kDebugLevelFiles, "loading cursor STUB, sprite %d", sprite);
	return sprite;
}

bool MainDat::nextCursorSprite(uint16 mode, uint16 &stepIndex, bool &stepPending, uint16 &spriteId, uint16 tableFooterOffset) const {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	return CursorSpriteTable(segment.span(footer.wordAt(tableFooterOffset))).nextSprite(mode, stepIndex, stepPending, spriteId);
}

bool MainDat::cycleCursorOverlayAnimation(uint16 maskBit, uint16 &spriteId, uint16 &x, uint16 &y) {
	const MainDatFooter footer(_footer);
	const MainDatSegment segment(data(), _dataLen);
	const uint16 recordOffset = footer.cursorOverlayRecordOffset(maskBit);
	if (recordOffset == 0)
		return false;

	if (recordOffset == 0 || uint32(recordOffset) + 8 > _dataLen) {
		debugC(2, kDebugLevelGraphics | kDebugLevelFiles,
			   "cursor-overlay anim 0x%02x invalid record offset 0x%04x",
			   maskBit, recordOffset);
		return false;
	}

	const CursorOverlayRecord record(segment.mutableSpan(recordOffset, CursorOverlayRecord::kHeaderSize));
	x = record.x();
	y = record.y();
	const uint16 frameCount = record.frameCount();
	const uint16 frameIndex = record.frameIndex();
	const uint32 spriteOffset = uint32(recordOffset) + 8 + uint32(frameIndex) * 2;
	if (frameCount == 0 || spriteOffset + 2 > _dataLen) {
		debugC(2, kDebugLevelGraphics | kDebugLevelFiles,
			   "cursor-overlay anim 0x%02x invalid frame count/index %u/%u at 0x%04x",
			   maskBit, frameIndex, frameCount, recordOffset);
		return false;
	}

	uint16 nextFrame = uint16(frameIndex + 1);
	if (nextFrame == frameCount)
		nextFrame = 0;
	record.setFrameIndex(nextFrame);
	spriteId = segment.wordAt(spriteOffset);
	return true;
}

uint16 MainDat::getInterfaceMapMarkerSpriteId(uint16 selector) const {
	return MainDatFooter(_footer).interfaceMapMarkerSpriteId(selector);
}

uint16 MainDat::getStatusButtonSpriteId(bool statusMode) const {
	return MainDatFooter(_footer).statusButtonSpriteId(statusMode);
}

uint16 MainDat::getEyeCloseUpSpriteId(bool rightHalf) const {
	return MainDatFooter(_footer).eyeCloseUpSpriteId(rightHalf);
}

uint16 MainDat::getFrameId(FramePart part) const {
	return MainDatFooter(_footer).frameSpriteId(part);
}

uint16 MainDat::getBubbleId(SpeechBubblePart part) const {
	return MainDatFooter(_footer).bubbleSpriteId(part);
}

} // End of namespace Interspective
