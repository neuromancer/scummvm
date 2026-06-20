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

#ifndef INTERSPECTIVE_MAIN_DAT_H
#define INTERSPECTIVE_MAIN_DAT_H

#include "common/array.h"
#include "common/endian.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/str.h"

#include "interspective/actor.h"
#include "interspective/datafile.h"
#include "interspective/sprite.h"
#include "interspective/types.h"

namespace Interspective {
//

class Actor;
class Interpreter;

class MainDat : public Datafile {
public:
	MainDat(Resources *resources);
	~MainDat();

	// Resolved by the engine from the selected language: "iuc_main.dat" for the
	// single-language release, or IUC_MAIN.<ext> for the multilingual CD.
	const char *filename() const;
	void readFile(Common::SeekableReadStream &stream);

	uint16 imagesCount() const;
	uint16 tunesCount() const;

	struct GraphicFile {
		uint16 data_set;
		Common::String filename;
	};

	struct SfxFile {
		uint16 low;
		uint16 high;
		uint8 mode;
		Common::String filename;
	};

	Common::List<struct GraphicFile> graphicFiles() const;
	Common::List<Common::String> musicFiles() const;
	Common::List<SfxFile> sfxFiles() const;
	uint16 sfxSampleCount() const;
	uint16 sfxFileCount() const;

	uint16 progEntriesCount0() const;
	uint16 progEntriesCount1() const;
	// DOS footer naming is historical here: offset 0x08 is the room-code
	// program count used by LoadCodeBlock and as the base for Op_38 scene ids.
	uint16 roomProgramCount() const { return progEntriesCount1(); }
	uint16 sceneProgramCount() const { return progEntriesCount0(); }

	/**
	 * Find an image in the file contents directory.
	 * @param index of the image (start counting from 1),
	 * @returns index of graphics data file containing the image.
	 */
	uint16 fileIndexOfImage(uint16 index) const;
	uint16 imageType(uint16 index) const;
	void patchImageType(uint16 index, uint16 type);
	uint16 fileIndexOfTune(uint16 index) const;

	uint16 interfaceImageIndex() const;

	byte *getEntryPoint() const;
	uint16 getRoomLoopEntryPoint() const;
	uint16 dataSize() const { return _dataLen; }
	byte *data() { return _data.data(); }
	const byte *data() const { return _data.data(); }

	Actor *actor(uint16 index) const;
	uint16 actorsCount() const { return _actorsCount; }

	// Seed Logic::_objectRoom with the initial object→room mapping from the
	// global object-state table (DOS CS:[0x6d], iuc_main.dat footer offset
	// 0x0E). Each record is 18 bytes; first uint16 is the room. Called once
	// at engine startup before any script runs.
	void loadObjectStates();
	uint16 personsCount() const;
	uint16 globalObjectStateCount() const { return personsCount(); }
	uint16 maxGameScore() const;
	uint16 scoreEventCount() const;
	bool claimScoreEvent(uint16 eventId, uint16 &delta);

	byte *getByteVariable(uint16 index);
	byte *getWordVariable(uint16 index);
	uint16 getRoomScriptId(uint16 room) const;
	uint16 getGlyphSpriteId(byte character) const;

	SpriteInfo getSpriteInfo(uint16 index) const;
	uint16 spriteCount() const;

	uint16 getCursorSpriteId() const;
	// tableFooterOffset selects the DOS footer cursor table: 0x54 for normal
	// modes, 0x56 for modal conversation hits, and 0x58 for Op_76 drag cursor.
	bool nextCursorSprite(uint16 mode, uint16 &stepIndex, bool &stepPending, uint16 &spriteId, uint16 tableFooterOffset) const;
	bool cycleCursorOverlayAnimation(uint16 maskBit, uint16 &spriteId, uint16 &x, uint16 &y);
	uint16 getInterfaceMapMarkerSpriteId(uint16 selector) const;
	uint16 getStatusButtonSpriteId(bool statusMode) const;
	uint16 getEyeCloseUpSpriteId(bool rightHalf) const;
	uint16 getFrameId(FramePart part) const;
	uint16 getBubbleId(SpeechBubblePart part) const;

	Puppeteer getPuppeteer(uint16 actorId) const;

	friend class Resources;

private:
	void loadActors(Interpreter *);
	enum {
		kFooterLen = 0xB6
	};

	Common::Array<byte> _data;
	uint16 _dataLen;
	byte _footer[kFooterLen];
	byte *_imageDirectory;
	byte *_tunesDirectory;
	uint16 _programsCount;
	byte *_programsMap;
	Actor **_actors;
	uint16 _actorsCount;

	byte *imageDirectoryEntry(uint16 index) const;
	byte *mutableData() const { return const_cast<byte *>(_data.data()); }
	void parsePuppeteers() const;
	mutable Common::HashMap<uint16, Puppeteer> _puppeteers;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_MAIN_DAT_H
