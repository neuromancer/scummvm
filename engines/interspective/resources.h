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

#ifndef INTERSPECTIVE_RESOURCES_H
#define INTERSPECTIVE_RESOURCES_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/singleton.h"
#include "common/stream.h"
#include "graphics/surface.h"

#include "interspective/main_dat.h"
#include "interspective/types.h"

namespace Interspective {
//

class Engine;

class Surface : public ::Graphics::Surface { // this surface autodestructs properly
public:
	~Surface() { free(); }
	void blit(const Surface *s, int transparent = -1, const byte (*tinted)[256] = 0) {
		blit(s, Common::Point(0, 0), transparent, tinted);
	}
	void blit(const Surface *s, Common::Point p, int transparent = -1, const byte (*tinted)[256] = 0) {
		blit(s, Common::Rect(p, s->w, s->h), transparent, tinted);
	}
	void blit(const Surface *s, Common::Rect r, int transparent = -1, const byte (*tinted)[256] = 0);
	void blit(const Surface *s, Common::Rect r, Common::Point srcOffset, int transparent = -1, const byte (*tinted)[256] = 0);

	void create(uint16 width, uint16 height) {
		::Graphics::Surface::create(width, height, ::Graphics::PixelFormat::createFormatCLUT8());
	}
};

class Sprite : public Surface {
public:
	void recolour(byte colour);
	Common::Point _hotPoint;
};

class Image : public Surface {
public:
	Sprite *cut(Common::Rect rect) const;
};

class MapFile;
class ProgDat;
class Program;

class Resources : public Common::Singleton<Resources> {
public:
	Resources();
	~Resources();
	void setEngine(Engine *e);
	void load();
	void init();

	/**
	 * Load an image. Automatically consult maps to choose the right file.
	 * @param index image index,
	 * @param target buffer,
	 * @param size of the image,
	 * @param palette optional buffer to read the palette to (size 0x300).
	 */
	void loadImage(uint16 index, byte *target, uint32 size, byte *palette = 0) const;

	/**
	 * Loads an image given index.
	 * @returns pointer to the image. Please don't delete it, Resources owns it.
	 */
	Image *loadImage(uint16 index) const;
	void loadTune(uint16 index, byte *target) const;

	Common::ReadStream *tuneStream(uint16 index) const;

	void loadInterfaceImage(byte *target, byte *palette = 0) {
		loadImage(_main.get()->interfaceImageIndex(), target, 0x3c00, palette);
	}

	uint16 blockOfRoom(uint16 room) const;

	Program *loadCodeBlock(uint16 block) const;
	Program *loadSceneCodeBlock(uint16 scene) const;

	static void descramble(byte *data, uint32 length);

	byte *getGlobalByteVariable(uint16 var) const;
	byte *getGlobalWordVariable(uint16 var) const;

	Surface *loadBackdrop(uint16 index, byte *palette);

	/* pointer to the base of the main code */
	byte *mainBase() const;
	/* initial entry point offset */
	uint16 mainEntryPoint() const;
	uint16 mainRoomLoopEntryPoint() const;

	friend class ProgDat;

	SpriteInfo getSpriteInfo(uint16 id) const;
	Sprite *getGlyph(byte character) const;
	Sprite *loadSprite(uint16 id) const;
	Sprite *getCursor() const;
	Sprite *const *frames() const { return _framePtrs; }
	Sprite *const *bubbles() const { return _bubblePtrs; }

	void loadActors();
	void loadFrames();
	void loadSpeechBubbles();

	MainDat *mainDat() const { return _main.get(); }
	MapFile *graphicsMap() const { return _graphicsMap.get(); }

	static void decodeImage(Common::ReadStream *stream, byte *target, uint32 size);
	static void readPalette(Common::ReadStream *stream, byte *palette);

private:
	MapFile *tuneMap() const { return _tuneMap.get(); }
	ProgDat *progDat() const { return _progDat.get(); }

	Common::ReadStream *imageStream(uint16 index) const;
	void loadGraphicFiles();
	void loadMusicFiles();

	Engine *_vm;

	Common::SharedPtr<MainDat> _main;
	Common::SharedPtr<MapFile> _graphicsMap;
	Common::SharedPtr<MapFile> _tuneMap;
	Common::SharedPtr<ProgDat> _progDat;

	Common::Array<Common::SharedPtr<Common::SeekableReadStream> > _graphicFiles;
	Common::Array<Common::SharedPtr<Common::SeekableReadStream> > _musicFiles;

	enum {
		kBubbleCount = kBubbleVerbStem + 1
	};

	Common::ScopedPtr<Sprite> _frames[kFrameNum];
	Common::ScopedPtr<Sprite> _bubbles[kBubbleCount];
	Sprite *_framePtrs[kFrameNum];
	Sprite *_bubblePtrs[kBubbleCount];
	mutable Common::HashMap<uint16, Common::SharedPtr<Image> > _imageCache;
};

#define Res Resources::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_RESOURCES_H
