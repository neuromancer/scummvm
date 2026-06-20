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

#include "common/endian.h"
#include "common/util.h"

#include "interspective/actor.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/util.h"

using namespace Common;

namespace Interspective {

MainDat::MainDat(Resources *res)
	: Datafile(res), _dataLen(0), _imageDirectory(0), _tunesDirectory(0),
	  _programsCount(0), _programsMap(0), _actors(0), _actorsCount(0) {}

const char *MainDat::filename() const {
	return Engine::instance().mainDatFilename().c_str();
}

MainDat::~MainDat() {
	if (_actors) {
		for (int i = 0; i < _actorsCount; i++)
			delete _actors[i];
		delete[] _actors;
	}
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

void MainDat::readFile(SeekableReadStream &stream) {
	_dataLen = stream.readUint16LE();

	_data.resize(_dataLen);
	stream.seek(0);
	stream.read(data(), _dataLen);
	Resources::descramble(data() + 2, _dataLen - 2);

	stream.read(_footer, kFooterLen);

	_imageDirectory = data() + READ_LE_UINT16(_footer + kImageDirectory);
	_tunesDirectory = data() + READ_LE_UINT16(_footer + kTunesDirectory);

	_programsCount = READ_LE_UINT16(_footer + kProgEntriesCount1);

	_programsMap = data() + READ_LE_UINT16(_footer + kProgramsMap);
}

uint16 MainDat::personsCount() const {
	return READ_LE_UINT16(_footer + kPersonsCount);
}

uint16 MainDat::maxGameScore() const {
	return READ_LE_UINT16(_footer + kMaxGameScore);
}

uint16 MainDat::scoreEventCount() const {
	return READ_LE_UINT16(_footer + kScoreEventCount);
}

bool MainDat::claimScoreEvent(uint16 eventId, uint16 &delta) {
	delta = 0;
	if (eventId >= scoreEventCount())
		return false;

	byte *entry = data() + READ_LE_UINT16(_footer + kScoreEventTable) + eventId * 2;
	if (entry[1] != 0)
		return false;

	delta = READ_LE_UINT16(entry);
	entry[1] = 1;
	return true;
}

void MainDat::loadObjectStates() {
	// DOS CS:[0x6b] = persons count (footer +0x0C); CS:[0x6d] = pointer
	// to object-state table (footer +0x0E). Each record is 18 bytes;
	// uint16 at offset 0 is the room id. Op_7f writes there at runtime;
	// Op_18/Op_1b/Op_21 read.
	const uint16 count = personsCount();
	const uint16 listOff = READ_LE_UINT16(_footer + kGlobalObjectStateList);
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
		const byte *rec = data() + listOff + i * stride;
		const uint16 id = uint16(i + 1);
		const uint16 room = READ_LE_UINT16(rec);
		// Object IDs are 1-based per DOS GetObjectOffset (it does DEC AX
		// before multiplying). Room 0 = missing/destroyed (default in DOS)
		// — store anyway so isObjectMissing() returns true correctly.
		Log.setObjectRoom(id, room);
		Log.setObjectPosition(id,
							  int16(READ_LE_UINT16(rec + 2)),
							  int16(READ_LE_UINT16(rec + 4)));
		for (uint8 off = 6; off < stride; ++off)
			Log.setObjectField(id, off, rec[off]);
		if (room != 0)
			++nonZero;
	}
	debugC(1, kDebugLevelFiles,
		   "MainDat::loadObjectStates: seeded %u objects (%u with non-zero room) "
		   "from footer offset 0x%04x [DOS CS:[0x6d]]",
		   count, nonZero, listOff);
}

void MainDat::loadActors(Interpreter *in) {
	uint16 nactors = _actorsCount = READ_LE_UINT16(_footer + kActorsCount);
	uint16 actors = READ_LE_UINT16(_footer + kActors);
	assert(!_actors);
	_actors = new Actor *[nactors];
	for (int i = 0; i < nactors; ++i) {
		_actors[i] = new Actor(CodePointer(actors, in));
		_actors[i]->setId(uint16(i + 1)); // DOS uses 1-based ids
		_actors[i]->setPuppeteer(getPuppeteer(i + 1));
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
	uint16 count = READ_LE_UINT16(_footer + kPuppeteersCount);

	const byte *data = this->data() + READ_LE_UINT16(_footer + kPuppeteers);
	for (int i = 0; i < count; i++) {
		Puppeteer p(data);
		_puppeteers[p.actorId()] = p;
		data += Puppeteer::kSize;
	}
}

uint16 MainDat::imagesCount() const {
	return READ_LE_UINT16(_footer + kImagesCount);
}

uint16 MainDat::tunesCount() const {
	return READ_LE_UINT16(_footer + kTunesCount);
}

uint16 MainDat::progEntriesCount0() const {
	return READ_LE_UINT16(_footer + kProgEntriesCount0);
}

uint16 MainDat::progEntriesCount1() const {
	return READ_LE_UINT16(_footer + kProgEntriesCount1);
}

byte *MainDat::imageDirectoryEntry(uint16 index) const {
	const int32 directoryOffset = int32(_imageDirectory - mutableData());
	// DOS keeps this arithmetic in 16-bit registers:
	//   DEC AX; ADD AX,AX; ADD AX,AX; ADD DI,AX
	// so values that pass a signed bound check can wrap back before/within
	// the loaded data segment rather than becoming a large unsigned offset.
	const int16 entryDelta = int16(uint16(uint16(index - 1) * 4));
	const int32 entryOffset = directoryOffset + entryDelta;
	if (entryOffset < 0 || entryOffset + 3 >= int32(_dataLen)) {
		warning("MainDat::imageDirectoryEntry: id %u resolves outside iuc_main.dat (entryOff=%d)",
				index, entryOffset);
		return 0;
	}
	return mutableData() + entryOffset;
}

uint16 MainDat::fileIndexOfImage(uint16 index) const {
	const byte *entry = imageDirectoryEntry(index);
	if (!entry)
		return 0;
	return READ_LE_UINT16(entry + 2);
}

uint16 MainDat::imageType(uint16 index) const {
	const byte *entry = imageDirectoryEntry(index);
	if (!entry)
		return 0;
	return READ_LE_UINT16(entry);
}

void MainDat::patchImageType(uint16 index, uint16 type) {
	byte *entry = imageDirectoryEntry(index);
	if (!entry)
		return;
	WRITE_LE_UINT16(entry, type);
}

uint16 MainDat::fileIndexOfTune(uint16 index) const {
	uint32 offset = (index - 1) * 2;
	return READ_LE_UINT16(_tunesDirectory + offset);
}

Common::List<MainDat::GraphicFile> MainDat::graphicFiles() const {
	uint16 file_count = READ_LE_UINT16(_footer + kGraphicFileCount);
	uint16 names_offset = READ_LE_UINT16(_footer + kGraphicFileNames);

	const byte *data = this->data() + names_offset;
	Common::List<GraphicFile> files;
	for (; file_count > 0; file_count--) {
		GraphicFile file;
		file.data_set = READ_LE_UINT16(data);
		data += 2;
		file.filename = reinterpret_cast<const char *>(data);
		files.push_back(file);
		while (*data)
			data++;
		while (!*data)
			data++;
	}

	return files;
}

Common::List<Common::String> MainDat::musicFiles() const {
	uint16 file_count = READ_LE_UINT16(_footer + kMusicFileCount);
	uint16 names_offset = READ_LE_UINT16(_footer + kMusicFileNames);

	const byte *data = this->data() + names_offset;
	Common::List<Common::String> files;
	for (; file_count > 0; file_count--) {
		data += 2;           // data set id
		byte type = *data++; // music type (1 - adlib, 4 - roland)
		debugC(2, kDebugLevelFiles | kDebugLevelMusic, "found music file %s type %d",
			   reinterpret_cast<const char *>(data), type);
		files.push_back(Common::String(reinterpret_cast<const char *>(data)));
		while (*data)
			data++;
		data++;
	}

	return files;
}

uint16 MainDat::sfxSampleCount() const {
	return READ_LE_UINT16(_footer + kSfxSampleCount);
}

uint16 MainDat::sfxFileCount() const {
	return READ_LE_UINT16(_footer + kSfxFileCount);
}

Common::List<MainDat::SfxFile> MainDat::sfxFiles() const {
	const uint16 fileCount = sfxFileCount();
	const uint16 namesOffset = READ_LE_UINT16(_footer + kSfxFileNames);
	const byte *data = this->data() + namesOffset;
	const byte *end = this->data() + _dataLen;
	Common::List<SfxFile> files;

	// DOS OpenSfxFile @ 1000:5ff7 walks this footer list. Each entry is:
	//   uint16 strict-low sample id, uint8 driver mode, ASCIIZ filename.
	// The high bound is the next entry's low word, except for the final
	// entry, where OpenSfxFile uses CS:[0x83] (footer +0x24 sample count).
	for (uint16 i = 0; i < fileCount && data + 3 < end; ++i) {
		SfxFile file;
		file.low = READ_LE_UINT16(data);
		data += 2;
		file.mode = *data++;
		file.filename = reinterpret_cast<const char *>(data);
		while (data < end && *data)
			++data;
		if (data >= end)
			break;
		++data;
		file.high = (i + 1 == fileCount || data + 1 >= end)
						? sfxSampleCount()
						: READ_LE_UINT16(data);
		files.push_back(file);
	}

	return files;
}

byte *MainDat::getByteVariable(uint16 index) {
	uint16 offset = READ_LE_UINT16(_footer + kByteVars);
	return data() + offset + index;
}

byte *MainDat::getWordVariable(uint16 index) {
	uint16 offset = READ_LE_UINT16(_footer + kWordVars);
	return data() + offset + index * 2;
}

uint16 MainDat::interfaceImageIndex() const {
	return READ_LE_UINT16(_footer + kInterfaceImgIdx);
}

byte *MainDat::getEntryPoint() const {
	return mutableData() + READ_LE_UINT16(_footer + kEntryPoint);
}

uint16 MainDat::getRoomLoopEntryPoint() const {
	return READ_LE_UINT16(_footer + kRoomLoopEntryPoint);
}

Actor *MainDat::actor(uint16 index) const {
	return _actors[index];
}

uint16 MainDat::getRoomScriptId(uint16 room) const {

	byte *programInfo = _programsMap;
	for (int i = 1; i <= _programsCount; i++) {
		// DOS LoadRoomFromProgDat @ 1000:1ad9 reads one resource-set word
		// before scanning the room ids, and advances past every word it
		// reads, including the 0xffff terminator.
		programInfo += 2;

		for (;;) {
			const uint16 this_room = READ_LE_UINT16(programInfo);
			programInfo += 2;
			if (this_room == 0xffff)
				break;
			if (this_room == room)
				return i;
		}
	}

	return 0;
}

uint16 MainDat::getGlyphSpriteId(byte character) const {
	const byte *charmap = data() + READ_LE_UINT16(_footer + kCharacterMap);
	charmap += (character - ' ') * 2;
	uint16 id = READ_LE_UINT16(charmap);
	return id;
}

uint16 MainDat::spriteCount() const {
	return READ_LE_UINT16(_footer + kSpriteCount);
}

SpriteInfo MainDat::getSpriteInfo(uint16 index) const {
	const byte *spritemap = data() + READ_LE_UINT16(_footer + kSpriteMap);
	if (index >= spriteCount())
		error("local sprite index given (index: 0x%04x)", index);

	return SpriteInfo(spritemap, index);
}

uint16 MainDat::getCursorSpriteId() const {
	//	uint16 offset = READ_LE_UINT16(_footer + kCursors);
	uint16 sprite = 0x6c;
	debugC(1, kDebugLevelGraphics | kDebugLevelFiles, "loading cursor STUB, sprite %d", sprite);
	return sprite;
}

bool MainDat::nextCursorSprite(uint16 mode, uint16 &stepIndex, bool &stepPending, uint16 &spriteId, uint16 tableFooterOffset) const {
	const byte *table = data() + READ_LE_UINT16(_footer + tableFooterOffset);

	for (;;) {
		const int16 tableMode = int16(READ_LE_UINT16(table));
		table += 2;
		if (tableMode == -2)
			return false;

		if (uint16(tableMode) == mode)
			break;

		while (int16(READ_LE_UINT16(table)) != 0)
			table += 2;
		table += 2;
	}

	for (;;) {
		const int16 frame = int16(READ_LE_UINT16(table + stepIndex * 2));
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

bool MainDat::cycleCursorOverlayAnimation(uint16 maskBit, uint16 &spriteId, uint16 &x, uint16 &y) {
	uint16 footerOffset = 0;
	switch (maskBit) {
	case 0x01:
		footerOffset = kCursorOverlayBit01Offset;
		break;
	case 0x02:
		footerOffset = kCursorOverlayBit02Offset;
		break;
	case 0x04:
		footerOffset = kCursorOverlayBit04Offset;
		break;
	case 0x08:
		footerOffset = kCursorOverlayBit08Offset;
		break;
	case 0x10:
		footerOffset = kCursorOverlayBit10Offset;
		break;
	default:
		return false;
	}

	const uint16 recordOffset = READ_LE_UINT16(_footer + footerOffset);
	if (recordOffset == 0 || uint32(recordOffset) + 8 > _dataLen) {
		debugC(2, kDebugLevelGraphics | kDebugLevelFiles,
			   "cursor-overlay anim 0x%02x invalid record offset 0x%04x",
			   maskBit, recordOffset);
		return false;
	}

	byte *record = data() + recordOffset;
	x = READ_LE_UINT16(record);
	y = READ_LE_UINT16(record + 2);
	const uint16 frameCount = READ_LE_UINT16(record + 4);
	const uint16 frameIndex = READ_LE_UINT16(record + 6);
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
	WRITE_LE_UINT16(record + 6, nextFrame);
	spriteId = READ_LE_UINT16(data() + spriteOffset);
	return true;
}

uint16 MainDat::getInterfaceMapMarkerSpriteId(uint16 selector) const {
	uint16 footerOffset = 0;
	switch (selector) {
	case 1:
		footerOffset = kInterfaceMapMarker1Offset;
		break;
	case 5:
		footerOffset = kInterfaceMapMarker5Offset;
		break;
	case 3:
		footerOffset = kInterfaceMapMarker3Offset;
		break;
	case 7:
		footerOffset = kInterfaceMapMarker7Offset;
		break;
	default:
		return 0xffff;
	}

	return READ_LE_UINT16(_footer + footerOffset);
}

uint16 MainDat::getStatusButtonSpriteId(bool statusMode) const {
	return READ_LE_UINT16(_footer + (statusMode ? kStatusButtonStatusOffset : kStatusButtonNormalOffset));
}

uint16 MainDat::getEyeCloseUpSpriteId(bool rightHalf) const {
	return READ_LE_UINT16(_footer + (rightHalf ? kEyeCloseUpRightOffset : kEyeCloseUpLeftOffset));
}

uint16 MainDat::getFrameId(FramePart part) const {
	switch (part) {
#define PART(p) \
	case p:     \
		return READ_LE_UINT16(_footer + p##Offset)
		PART(kFrameBottom);
		PART(kFrameBottomLeft);
		PART(kFrameBottomRight);
		PART(kFrameFill);
		PART(kFrameLeft);
		PART(kFrameRight);
		PART(kFrameTop);
		PART(kFrameTopLeft);
		PART(kFrameTopRight);
#undef PART
	default:
		assert(false);
	}

	return 0;
}

uint16 MainDat::getBubbleId(SpeechBubblePart part) const {
	switch (part) {
#define PART(p) \
	case p:     \
		return READ_LE_UINT16(_footer + p##Offset)
		PART(kBubbleTopLeft);
		PART(kBubbleLeft);
		PART(kBubbleBottomLeft);
		PART(kBubbleTop);
		PART(kBubbleFill);
		PART(kBubbleBottom);
		PART(kBubbleTopRight);
		PART(kBubbleRight);
		PART(kBubbleBottomRight);

		PART(kBubbleBottomLeftPoint);
		PART(kBubbleBottomRightPoint);
		PART(kBubbleTopLeftPoint);
		PART(kBubbleTopRightPoint);
		PART(kBubbleVerbConnector);
		PART(kBubbleVerbStem);
#undef PART
	default:
		assert(false);
	}

	return 0;
}

} // End of namespace Interspective
