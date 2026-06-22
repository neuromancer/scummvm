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

#include "interspective/graphics.h"

#include "graphics/palette.h"
#include "graphics/paletteman.h"

#include "common/algorithm.h"
#include "common/array.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/system.h"
#include "common/util.h"
#include "graphics/cursorman.h"
#include "graphics/font.h"

#include "interspective/actor.h"
#include "interspective/animation.h"
#include "interspective/debug.h"
#include "interspective/debugger.h"
#include "interspective/eventmanager.h"
#include "interspective/exit.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Common {
DECLARE_SINGLETON(Interspective::Graphics);
}

namespace Interspective {

enum {
	kFontChangeableColour = 235
};

static byte *surfacePixelAt(::Graphics::Surface *surface, int x, int y) {
	assert(surface && surface->format.bytesPerPixel == 1);
	return reinterpret_cast<byte *>(surface->getBasePtr(x, y));
}

class GraphicsTextCursor {
public:
	GraphicsTextCursor(Common::Span<const byte> bytes, const char *context)
		: _bytes(bytes), _pos(0), _context(context), _warned(false) {}

	Common::Span<const byte> remaining() const {
		if (!_bytes.data() || _pos > _bytes.size())
			return Common::Span<const byte>();
		return _bytes.subspan(_pos);
	}

	bool readByte(byte &value) {
		if (!canRead(1))
			return fail(1);
		value = _bytes.getUint8At(_pos++);
		return true;
	}

	bool readUint16LE(uint16 &value) {
		if (!canRead(2))
			return fail(2);
		value = _bytes.getUint16LEAt(_pos);
		_pos += 2;
		return true;
	}

	void skipCString() {
		byte ch = 0;
		while (readByte(ch) && ch != 0) {
		}
	}

private:
	bool canRead(uint32 count) const {
		return _bytes.data() && _pos <= _bytes.size() && count <= _bytes.size() - _pos;
	}

	bool fail(uint32 count) {
		if (!_warned) {
			warning("Interspective: truncated text stream in %s at byte %u (wanted %u, size %u)",
					_context ? _context : "graphics text", uint(_pos), uint(count), uint(_bytes.size()));
			_warned = true;
		}
		return false;
	}

	Common::Span<const byte> _bytes;
	uint32 _pos;
	const char *_context;
	bool _warned;
};

static const byte kLayerScaleRows[9][16] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0 },
	{ 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1 },
	{ 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0 },
	{ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
	{ 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0 },
	{ 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 }
};

static const byte kLayerScaleCols[9][16] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
	{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0 },
	{ 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0 },
	{ 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1 },
	{ 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1 }
};

static uint16 scaledAxisLength(uint16 sourceLength, const byte *pattern) {
	uint16 scaled = 0;
	for (uint16 i = 0; i < sourceLength; ++i)
		if (pattern[i & 0x0f])
			++scaled;
	return scaled;
}

static uint layerScalePattern(uint16 drawMode) {
	const uint8 bl = uint8(drawMode & 0xff);
	return bl < 4 ? 0 : MIN<uint>(bl - 3, 8);
}

class InterspectiveFont : public ::Graphics::Font {
public:
	InterspectiveFont(const Graphics *graphics, Resources *resources)
		: _graphics(graphics), _resources(resources) {}

	Common::String getFontName() const override { return "Interspective DOS font"; }
	int getFontHeight() const override { return Graphics::kLineHeight; }

	int getMaxCharWidth() const override {
		int maxWidth = 4;
		for (uint32 ch = '!'; ch <= 0x9e; ++ch)
			maxWidth = MAX(maxWidth, getCharWidth(ch));
		return maxWidth;
	}

	int getCharWidth(uint32 chr) const override {
		Common::ScopedPtr<Sprite> glyph(loadGlyph(chr));
		return glyph ? MAX<int>(0, glyph->w - 1) : 4;
	}

	Common::Rect getBoundingBox(uint32 chr) const override {
		Common::ScopedPtr<Sprite> glyph(loadGlyph(chr));
		if (!glyph)
			return Common::Rect();
		return Common::Rect(-glyph->_hotPoint.x, glyph->_hotPoint.y,
							-glyph->_hotPoint.x + glyph->w,
							glyph->_hotPoint.y + glyph->h);
	}

	void drawChar(::Graphics::Surface *dst, uint32 chr, int x, int y, uint32 color) const override {
		if (!dst)
			return;

		Common::ScopedPtr<Sprite> glyph(loadGlyph(chr));
		if (!glyph)
			return;

		Common::Rect destRect(glyph->w, glyph->h);
		destRect.moveTo(Common::Point(x - glyph->_hotPoint.x, y + glyph->_hotPoint.y));
		Common::Rect clipped = destRect;
		clipped.clip(dst->w, dst->h);
		if (clipped.isEmpty())
			return;

		const int srcX = clipped.left - destRect.left;
		const int srcY = clipped.top - destRect.top;
		const byte textColor = byte(color & 0xff);

		for (int row = 0; row < clipped.height(); ++row) {
			const byte *src = glyph->pixelAt(srcX, srcY + row);
			byte *dest = surfacePixelAt(dst, clipped.left, clipped.top + row);
			for (int col = 0; col < clipped.width(); ++col) {
				if (src[col] == 0)
					continue;
				dest[col] = src[col] == kFontChangeableColour ? textColor : src[col];
			}
		}
	}

private:
	Sprite *loadGlyph(uint32 chr) const {
		if (!_graphics || !_resources || chr > 0xff)
			return nullptr;
		const byte ch = _graphics->clampChar(byte(chr));
		return _resources->getGlyph(ch);
	}

	const Graphics *_graphics;
	Resources *_resources;
};

enum {
	kInterfaceTop = 152,
	kPanelLeft = 3,
	kPanelTop = 155,
	kPanelWidth = 56,
	kPanelHeight = 25,
	kCloseUpFrameLeft = 3,
	kCloseUpFrameTop = 155,
	kCloseUpFrameRightLeft = 31,
	kCloseUpContentLeft = 4,
	kCloseUpContentTop = 156,
	kCloseUpContentWidth = 54,
	kCloseUpContentHeight = 22
};

static uint16 inventoryObjectAtPoint(Logic *logic, Resources *resources,
									 const Common::Point &screen) {
	if (!logic || !resources)
		return 0;
	if (screen.x < 128 || screen.x >= 310 || screen.y < 160 || screen.y >= 191)
		return 0;

	const Common::Array<uint16> &objectExits = logic->objectExitList();
	for (int i = int(objectExits.size()) - 1; i >= 0; --i) {
		const uint16 id = objectExits[i];
		if (id == 0 || logic->getObjectRoom(id) != 0xffff)
			continue;

		const uint16 spriteId = uint16(logic->objectField(id, 8)) | (uint16(logic->objectField(id, 9)) << 8);
		if (spriteId == 0xffff)
			continue;

		const SpriteInfo info = resources->getSpriteInfo(spriteId);
		if (info.empty())
			continue;

		const Common::Point hotPoint = info.hotPoint();
		const Common::Point topLeft(
			int16(0x80 + logic->getObjectPosX(id) - hotPoint.x),
			int16(0xa0 + logic->getObjectPosY(id) - hotPoint.y));
		Common::Rect rect = info.topLeftRect(topLeft);
		if (!rect.contains(screen))
			continue;

		Common::ScopedPtr<Sprite> sprite(resources->loadSprite(spriteId));
		const int16 sx = int16(screen.x - topLeft.x);
		const int16 sy = int16(screen.y - topLeft.y);
		const byte *pixel = sprite->pixelAt(sx, sy);
		if (pixel && *pixel != 0)
			return id;
	}

	return 0;
}

void Graphics::setEngine(Engine *engine) {
	_engine = engine;
	_framebuffer = Common::SharedPtr<Surface>(new Surface);
	_framebuffer.get()->create(320, 200);
	_willFadein = false;
	_inFade = false;
	_fadeFlags = kFullFade;
	_fadeStart = 0;
	_fadeCount = 256;
	Common::fill(_roomPalette, _roomPalette + sizeof(_roomPalette), 0);
	Common::fill(_interfacePalette, _interfacePalette + sizeof(_interfacePalette), 0);
	Common::fill(_tintedPalette, _tintedPalette + sizeof(_tintedPalette), 0);
	Common::fill(_conversationSavedPalette, _conversationSavedPalette + sizeof(_conversationSavedPalette), 0);
	_conversationPaletteRestorePending = false;

	_speechText.clear();
	_speechActive = false;
	_speechFramesLeft = 0;
	_speechX = 0;
	_speechY = 0;
	_speechColor = 235;
	_speechMaxLines = 0;
	_speechBubble = false;
	_speechBubbleMode = kSpeechBubbleType1;
	_speechDoneCallbackMode = 0;
	_speechDoneCallbackHasMode = false;
	_fullscreen = false;
	_fullRedrawPending = true;
	_extendedLatinFont = false;
	_font.reset();
}

void Graphics::init() {
	_resources = _engine->resources();
	_system = _engine->_system;
	// The multilingual accented releases (Spanish/French/German/Italian) ship an
	// extended font (glyph codes 0x7c..0x9e = accents) and CP437-encoded text;
	// their EXEs translate it via LookupCharSprite's helper @ 1000:c77a. The
	// English/single-language build has no such glyphs, so keep its plain
	// 0x20..0x7e font there.
	const Common::Language lang = _engine ? _engine->language() : Common::UNK_LANG;
	_extendedLatinFont = (lang == Common::DE_DEU || lang == Common::FR_FRA || lang == Common::ES_ESP || lang == Common::IT_ITA);
	_font.reset(new InterspectiveFont(this, _resources));
	loadInterface();
}

void Graphics::paint() {
	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, ">>>start paint procedure");
	restoreConversationPalette();
	beginFrame();
	Logic *logic = _engine->logic();

	if (logic->roomChangePending()) {
		debugC(2, kDebugLevelFlow | kDebugLevelGraphics, "skipping paint while room restart is pending");
		return;
	}

	paintBackdrop();
	paintAnimations();
	paintStatusScreenText();
	paintInterface();
	paintSpeech();
	_engine->logic()->paintMotionText();

	debugC(3, kDebugLevelGraphics, "painting paintables");
	foreach (Paintable *, _paintables)
		(*it)->paint(this);

	unless(_afterRepaintHooks.empty()) {
		debugC(3, kDebugLevelGraphics | kDebugLevelScript, "running hooks");
		foreach (CodePointer, _afterRepaintHooks)
			it->run();
		_afterRepaintHooks.clear();
	}

	if (!logic->paused()) {
		paintCursorSprite();
		paintCursorObjectName();
	} else {
		debugC(3, kDebugLevelGraphics, "skipping cursor paint while paused");
	}

	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, "<<<end paint procedure");
}

void Graphics::paintExits() {
	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, "painting exits");
	foreach_const(Exit *, _engine->logic()->room()->exits())(*it)->paint(this);
}

static bool containsDosPoint(uint16 a, uint16 b, uint16 c, uint16 d, int16 x, int16 y) {
	return int16(a) <= x && x <= int16(c) && int16(b) <= y && y <= int16(d);
}

static uint16 recordWord(const Logic *logic, uint8 selector, uint16 id, uint8 off) {
	return logic->recordField(selector, id, off, 2);
}

static bool exitIsNoSprite(const Logic *logic, uint16 id) {
	return logic->recordField(1, id, 0x0a, 1) != 0;
}

static int16 normalizeLayer(int16 layer) {
	return uint8(layer) == 0xff ? -1 : layer;
}

static int16 findObjectLayer(Logic *logic, uint16 id, uint16 spriteId) {
	SpriteInfo info = logic->engine()->resources()->getSpriteInfo(spriteId);
	int16 x = logic->getObjectPosX(id);
	int16 y = logic->getObjectPosY(id);

	// CalcSpriteAndExitInfo @ 1000:bfbe calls CalcSpriteOffsetInGraphic,
	// then FindZoneAtPoint with x + width/2 (clamped at 0) and y + height.
	int32 zoneX = int32(x) + (int32(info.width) >> 1);
	if (zoneX < 0)
		zoneX = 0;
	const int16 cx = int16(zoneX);
	const int16 dx = int16(int32(y) + int32(info.height));

	uint8 low = 0;
	const Common::Array<Logic::CollisionZone> &collisionZones = logic->collisionZones();
	for (uint i = 0; i < collisionZones.size(); ++i) {
		const Logic::CollisionZone &z = collisionZones[i];
		if (containsDosPoint(z.a, z.b, z.c, z.d, cx, dx)) {
			low = uint8(z.slot);
			break;
		}
	}

	uint8 high = 0;
	const Common::Array<Logic::ZoneB> &zonesB = logic->zonesB();
	for (uint i = 0; i < zonesB.size(); ++i) {
		const Logic::ZoneB &z = zonesB[i];
		if (containsDosPoint(z.a, z.b, z.c, z.d, cx, dx)) {
			high = uint8(z.var);
			break;
		}
	}

	logic->setObjectField(id, 0x0e, low);
	logic->setObjectField(id, 0x0f, high);
	return dosSignedByte(low);
}

static void rebuildDrawCommands(Graphics *graphics, Logic *logic) {
	logic->clearDrawCommands();
	if (!logic->room() || !logic->blockProgram())
		return;

	Program *program = logic->blockProgram();
	for (uint16 id = 1; id <= program->exitsCount(); ++id) {
		if (logic->recordField(1, id, 0, 2) != logic->currentRoom())
			continue;
		if (!logic->cellBit(id, 0))
			continue;
		if (exitIsNoSprite(logic, id)) {
			if (!logic->addVisibleNoSpriteExit(id))
				return;
			continue;
		}
		if (!logic->addDrawCommand(1, id, dosSignedByte(uint8(logic->recordField(1, id, 0x0b, 1)))))
			return;
	}

	Resources *resources = logic->engine()->resources();
	const uint16 objectCount = resources->mainDat()->personsCount();
	for (uint16 id = 1; id <= objectCount; ++id) {
		if (logic->getObjectRoom(id) != logic->currentRoom())
			continue;
		const int16 x = logic->getObjectPosX(id);
		const int16 y = logic->getObjectPosY(id);
		if (x == -1 || y == -1)
			continue;
		const uint16 spriteId = uint16(logic->objectField(id, 6)) | (uint16(logic->objectField(id, 7)) << 8);
		if (spriteId == 0xffff)
			continue;
		const int16 layer = findObjectLayer(logic, id, spriteId);
		if (!logic->addDrawCommand(2, id, layer))
			return;
	}

	debugC(3, kDebugLevelGraphics, "rebuilt %u draw commands [DOS AddDrawCommand]",
		   logic->drawCommandCount());
}

static bool objectDrawShouldDefer(Logic *logic, const Logic::DrawCommand &cmd) {
	if (cmd.type != 2)
		return false;

	Actor *protagonist = logic->protagonist();
	if (!protagonist)
		return false;

	const uint16 spriteId = protagonist->mainSpriteId();
	if (spriteId == 0xffff)
		return false;

	SpriteInfo info = logic->engine()->resources()->getSpriteInfo(spriteId);
	const int16 minX = int16(protagonist->position().x - int16(protagonist->visibleSpriteWidth()));
	const int16 maxX = int16(protagonist->position().x + int16(protagonist->visibleSpriteWidth()));
	const int16 minY = int16(protagonist->position().y + info.hotPoint().y);
	const int16 maxY = int16(minY + 6);
	const int16 x = logic->getObjectPosX(cmd.id);
	const int16 y = logic->getObjectPosY(cmd.id);

	return minX <= x && x < maxX && minY <= y && y < maxY;
}

static void paintDrawCommand(Graphics *graphics, Logic *logic, const Logic::DrawCommand &cmd) {
	if (cmd.type == 1) {
		if (logic->recordField(1, cmd.id, 0, 2) == logic->currentRoom() && logic->cellBit(cmd.id, 0)) {
			const uint16 spriteId = recordWord(logic, 1, cmd.id, 6);
			Common::ScopedPtr<Sprite> sprite(logic->engine()->resources()->loadSprite(spriteId));
			debugC(5, kDebugLevelGraphics,
				   "DOS DrawObjectsAtActorY: draw exit %u sprite=%u layer=%d pos=(%d,%d)",
				   cmd.id, spriteId, normalizeLayer(cmd.layer),
				   int16(recordWord(logic, 1, cmd.id, 2)), int16(recordWord(logic, 1, cmd.id, 4)));
			graphics->paint(sprite.get(), Common::Point(int16(recordWord(logic, 1, cmd.id, 2)), int16(recordWord(logic, 1, cmd.id, 4))),
							Graphics::kPaintCameraRelative);
		}
		return;
	}

	if (cmd.type != 2)
		return;

	const uint16 spriteId = uint16(logic->objectField(cmd.id, 6)) | (uint16(logic->objectField(cmd.id, 7)) << 8);
	if (spriteId == 0xffff)
		return;

	Common::ScopedPtr<Sprite> sprite(logic->engine()->resources()->loadSprite(spriteId));
	debugC(5, kDebugLevelGraphics,
		   "DOS DrawObjectsAtActorY: draw object %u sprite=%u layer=%d pos=(%d,%d)",
		   cmd.id, spriteId, normalizeLayer(cmd.layer),
		   logic->getObjectPosX(cmd.id), logic->getObjectPosY(cmd.id));
	graphics->paint(sprite.get(), Common::Point(logic->getObjectPosX(cmd.id), logic->getObjectPosY(cmd.id)),
					Graphics::kPaintCameraRelative);
	logic->setObjectField(cmd.id, 0x10, uint8(MIN<int>(sprite->w, 255)));
	logic->setObjectField(cmd.id, 0x11, uint8(MIN<int>(sprite->h, 255)));
}

static void paintAnimationsForLayer(Graphics *graphics, const Common::List<Animation *> &animations,
									int16 layer, bool actors) {
	for (Common::List<Animation *>::const_iterator it = animations.begin(); it != animations.end(); ++it) {
		Animation *anim = *it;
		if (anim->isActor() != actors)
			continue;
		if (normalizeLayer(anim->zIndex()) == layer)
			anim->paint(graphics);
	}
}

struct ActorDrawEntry {
	ActorDrawEntry() : anim(0), y(0), order(0) {}
	ActorDrawEntry(Animation *a, int16 drawY, uint drawOrder) : anim(a), y(drawY), order(drawOrder) {}

	Animation *anim;
	int16 y;
	uint order;
};

struct ActorDrawEntryLess {
	bool operator()(const ActorDrawEntry &a, const ActorDrawEntry &b) const {
		if (a.y != b.y)
			return a.y < b.y;

		// DOS CollectActorAnimSlots @ 1000:65ef uses <= while searching
		// for the next minimum y render command, so equal-y commands draw
		// in reverse collection order.
		return a.order > b.order;
	}
};

static void paintActorAnimationsForLayer(Graphics *graphics, Logic *logic, const Common::List<Animation *> &animations,
										 int16 layer) {
	Common::Array<ActorDrawEntry> entries;
	uint order = 0;
	for (Common::List<Animation *>::const_iterator it = animations.begin(); it != animations.end(); ++it, ++order) {
		Animation *anim = *it;
		if (!anim->isActor())
			continue;
		Actor *actor = static_cast<Actor *>(anim);
		if (!logic->activeActor(logic->actorGlobalId(actor)))
			continue;
		if (normalizeLayer(anim->zIndex()) != layer)
			continue;
		if (!anim->hasMainSpriteForDraw())
			continue;
		debugC(5, kDebugLevelActor | kDebugLevelGraphics,
			   "DOS CollectActorAnimSlots: actor %u layer=%d drawY=%d frame=%u pos=(%d,%d)",
			   logic->actorGlobalId(actor), layer, anim->drawY(), actor->frameId(),
			   actor->position().x, actor->position().y);
		entries.push_back(ActorDrawEntry(anim, anim->drawY(), order));
	}

	Common::sort(entries.begin(), entries.end(), ActorDrawEntryLess());
	for (uint i = 0; i < entries.size(); ++i) {
		Actor *actor = static_cast<Actor *>(entries[i].anim);
		const uint16 drawMode = actor->drawModeForLayer(layer);
		debugC(5, kDebugLevelActor | kDebugLevelGraphics,
			   "DOS DrawActorAnimSlot: actor %u layer=%d sortedY=%d frame=%u pos=(%d,%d) mode=0x%04x",
			   logic->actorGlobalId(actor), layer, entries[i].y, actor->frameId(),
			   actor->position().x, actor->position().y, drawMode);
		actor->paint(graphics, drawMode);
	}
}

void Graphics::loadInterface() {
	debugC(1, kDebugLevelGraphics, "loading interface");
	_interface = new Surface;
	_interface->create(320, 50);
	_resources->loadInterfaceImage(_interface->pixelSpan(0x3c00),
								   Common::Span<byte>(_interfacePalette, sizeof(_interfacePalette)));
}

void Graphics::prepareInterfacePalette() {
	debugC(1, kDebugLevelGraphics, "preparing interface palette");
	setPalette(_interfacePalette + 160 * 3, 160, 96);
}

void Graphics::paintInterface() {
	if (_fullscreen)
		return;
	debugC(3, kDebugLevelGraphics, "painting interface");
	_framebuffer->blit(_interface, Common::Rect(0, 152, 320, 200), 0);
	paintInterfaceMinimap();
	paintInventoryObjects();
	paintInterfaceOverlaySprites();
	paintStatusOverlayText();
	paintRoomCloseUp();
	paintInventoryCloseUp();
	paintAutoCloseTimer();
	markDirtyRect(Common::Rect(0, 152, 320, 200));
}

void Graphics::paintInterfaceMinimap() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !_resources)
		return;
	if (!logic->roomActive() || logic->cursorMode() == 0x80 || logic->dialogClickGate() == 0)
		return;

	// DrawDialogChoices @ 1000:b2e8 is the DOS mini-map renderer despite
	// Ghidra's misleading name: Op_e3 supplies the base room-map sprite and
	// Op_e4 supplies optional exit markers gated by the room cell map.
	Common::ScopedPtr<Sprite> mapSprite(_resources->loadSprite(logic->dialogClickGate()));
	paint(mapSprite.get(), Common::Point(logic->dialogCursor0(), logic->dialogCursor1()),
		  kPaintPositionIsTop | kPaintNoDirty);

	MainDat *mainDat = _resources->mainDat();
	if (!mainDat)
		return;

	const Common::Array<Logic::AnimListEntry> &entries = logic->animList();
	for (uint i = 0; i < entries.size(); ++i) {
		const Logic::AnimListEntry &entry = entries[i];
		if (!logic->cellBit(entry.arg3, 0))
			continue;

		const uint16 markerSpriteId = mainDat->getInterfaceMapMarkerSpriteId(entry.arg2);
		if (markerSpriteId == 0xffff) {
			logic->setPendingError(0x0d);
			continue;
		}

		Common::ScopedPtr<Sprite> marker(_resources->loadSprite(markerSpriteId));
		paint(marker.get(), Common::Point(entry.x0, entry.y0),
			  kPaintPositionIsTop | kPaintNoDirty);
	}
}

void Graphics::setRoomCloseUp(Common::Point point) {
	_roomCloseUpActive = true;
	_roomCloseUpPoint = point;
	_inventoryCloseUpObjectId = 0;
}

void Graphics::clearRoomCloseUp() {
	_roomCloseUpActive = false;
}

void Graphics::paintRoomCloseUp() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic)
		return;
	if (logic->cursorMode() != 0x80 || !logic->roomActive() || logic->noStep() || !logic->inputEnabled()) {
		_roomCloseUpActive = false;
		return;
	}

	const Common::Point cursor = cursorPosition();
	if (cursor.x < 0 || cursor.x >= 320 || cursor.y < 0 || cursor.y >= kInterfaceTop) {
		_roomCloseUpActive = false;
		return;
	}

	_roomCloseUpPoint = cursor;
	_roomCloseUpActive = true;
	_inventoryCloseUpObjectId = 0;
	const int srcLeft = CLIP<int>(_roomCloseUpPoint.x - 0x1b, 0, 0x10a);
	const int srcTop = CLIP<int>(_roomCloseUpPoint.y - 0x0b, 0, 0x82);
	for (int y = 0; y < kCloseUpContentHeight; ++y) {
		const byte *src = _framebuffer->pixelAt(srcLeft, srcTop + y);
		byte *dst = _framebuffer->pixelAt(kCloseUpContentLeft, kCloseUpContentTop + y);
		memcpy(dst, src, kCloseUpContentWidth);
	}

	MainDat *mainDat = _resources ? _resources->mainDat() : 0;
	if (mainDat) {
		Common::ScopedPtr<Sprite> leftHalf(_resources->loadSprite(mainDat->getEyeCloseUpSpriteId(false)));
		Common::ScopedPtr<Sprite> rightHalf(_resources->loadSprite(mainDat->getEyeCloseUpSpriteId(true)));
		paint(leftHalf.get(), Common::Point(kCloseUpFrameLeft, kCloseUpFrameTop),
			  kPaintPositionIsTop | kPaintNoDirty);
		paint(rightHalf.get(), Common::Point(kCloseUpFrameRightLeft, kCloseUpFrameTop),
			  kPaintPositionIsTop | kPaintNoDirty);
	}
}

void Graphics::paintInventoryObjects() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !_resources)
		return;

	const Common::Array<uint16> &objectExits = logic->objectExitList();
	for (uint i = 0; i < objectExits.size(); ++i) {
		const uint16 id = objectExits[i];
		if (id == 0 || logic->getObjectRoom(id) != 0xffff)
			continue;

		const uint16 spriteId = uint16(logic->objectField(id, 8)) | (uint16(logic->objectField(id, 9)) << 8);
		if (spriteId == 0xffff)
			continue;

		const SpriteInfo info = _resources->getSpriteInfo(spriteId);
		if (info.empty())
			continue;

		const Common::Point hotPoint = info.hotPoint();
		const Common::Point topLeft(
			int16(0x80 + logic->getObjectPosX(id) - hotPoint.x),
			int16(0xa0 + logic->getObjectPosY(id) - hotPoint.y));
		Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
		paint(sprite.get(), topLeft, kPaintPositionIsTop | kPaintNoDirty | kPaintIgnoreHotPoint);
	}
}

void Graphics::setInventoryCloseUpObject(uint16 objectId) {
	_roomCloseUpActive = false;
	_inventoryCloseUpObjectId = objectId;
}

void Graphics::clearInventoryCloseUpObject() {
	_inventoryCloseUpObjectId = 0;
}

void Graphics::paintInventoryCloseUp() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !_resources)
		return;
	if (logic->cursorMode() != 0x80 || !logic->roomActive() || logic->noStep() || !logic->inputEnabled()) {
		_inventoryCloseUpObjectId = 0;
		return;
	}

	_inventoryCloseUpObjectId = inventoryObjectAtPoint(logic, _resources, cursorPosition());
	if (_inventoryCloseUpObjectId == 0)
		return;

	const uint16 id = _inventoryCloseUpObjectId;
	if (_resources->mainDat() && id > _resources->mainDat()->personsCount()) {
		_inventoryCloseUpObjectId = 0;
		return;
	}

	const uint16 spriteId = uint16(logic->objectField(id, 8)) | (uint16(logic->objectField(id, 9)) << 8);
	if (spriteId == 0xffff)
		return;

	const SpriteInfo info = _resources->getSpriteInfo(spriteId);
	if (info.empty())
		return;
	if (info.width > 0x38 || info.height > 0x19) {
		logic->setPendingError(0x2d);
		return;
	}

	const Common::Point hotPoint = info.hotPoint();
	const Common::Point topLeft(
		int16(0x03 + ((0x38 - int16(info.width)) >> 1) + hotPoint.x),
		int16(0x9b + ((0x19 - int16(info.height)) >> 1) + hotPoint.y));
	Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
	paint(sprite.get(), topLeft, kPaintPositionIsTop | kPaintNoDirty | kPaintIgnoreHotPoint);
}

void Graphics::setInterfaceOverlayAnimationMask(uint16 mask) {
	_interfaceOverlayAnimationMask = mask;
	for (uint i = 0; i < _interfaceOverlaySprites.size();) {
		if ((_interfaceOverlaySprites[i].maskBit & mask) != 0) {
			++i;
		} else {
			_interfaceOverlaySprites.remove_at(i);
		}
	}
	debugC(2, kDebugLevelGraphics, "interface overlay animation mask=0x%02x [DOS Op_28]", mask);
}

void Graphics::setInterfaceOverlaySprite(uint16 maskBit, uint16 spriteId, uint16 x, uint16 y) {
	for (uint i = 0; i < _interfaceOverlaySprites.size(); ++i) {
		InterfaceOverlaySprite &overlay = _interfaceOverlaySprites[i];
		if (overlay.maskBit != maskBit)
			continue;
		overlay.spriteId = spriteId;
		overlay.x = x;
		overlay.y = y;
		return;
	}

	InterfaceOverlaySprite overlay;
	overlay.maskBit = maskBit;
	overlay.spriteId = spriteId;
	overlay.x = x;
	overlay.y = y;
	_interfaceOverlaySprites.push_back(overlay);
}

void Graphics::paintInterfaceOverlaySprites() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || logic->cursorMode() != 0x80) {
		_interfaceOverlaySprites.clear();
		_interfaceOverlayAnimationMask = 0;
		return;
	}
	if (!_resources || !_resources->mainDat() || _interfaceOverlayAnimationMask == 0) {
		_interfaceOverlaySprites.clear();
		return;
	}

	// CycleAllAnimationsByMask @ 1000:c8a1 visits the five verb-overlay
	// animation records in this fixed order and advances each selected slot
	// before DrawCursorSprite/CopyBackBufferToScreen present the frame.
	const uint16 animationBits[] = {0x01, 0x02, 0x10, 0x04, 0x08};
	for (uint i = 0; i < ARRAYSIZE(animationBits); ++i) {
		const uint16 maskBit = animationBits[i];
		if ((_interfaceOverlayAnimationMask & maskBit) == 0)
			continue;

		uint16 spriteId = 0;
		uint16 x = 0;
		uint16 y = 0;
		if (!_resources->mainDat()->cycleCursorOverlayAnimation(maskBit, spriteId, x, y))
			continue;
		setInterfaceOverlaySprite(maskBit, spriteId, x, y);
		debugC(4, kDebugLevelGraphics,
			   "interface overlay anim bit=0x%02x sprite=%u pos=(%u,%u)",
			   maskBit, spriteId, x, y);
		Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
		paint(sprite.get(), Common::Point(x, y), kPaintPositionIsTop | kPaintNoDirty);
	}
}

void Graphics::paintAutoCloseTimer() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !_resources)
		return;

	const uint16 spriteId = logic->updateAutoCloseTimerSprite();
	if (spriteId == 0xffff)
		return;

	Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
	paint(sprite.get(), Common::Point(0x40, 0xbe), kPaintPositionIsTop | kPaintNoDirty);
}

bool Graphics::setStatusOverlayText(Common::Span<const byte> text) {
	_statusOverlayLines.clear();
	if (!text.data())
		return true;

	GraphicsTextCursor cursor(text, "status overlay text");
	bool done = false;
	while (!done) {
		byte line[101];
		uint16 len = 0;
		byte terminator = 0;
		while (len < 100) {
			byte ch = 0;
			if (!cursor.readByte(ch))
				return false;
			if (ch == 0 || ch == '\r') {
				terminator = ch;
				break;
			}
			line[len++] = ch;
		}
		if (len == 100)
			terminator = 0;
		line[len] = 0;

		if (plainTextLineWidth(Common::Span<const byte>(line, len + 1)) > 0x38)
			return false;
		_statusOverlayLines.push_back(Common::String(reinterpret_cast<const char *>(line)));
		done = terminator == 0;
	}

	paintStatusOverlayText();
	return true;
}

void Graphics::paintStatusOverlayText() {
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || logic->cursorMode() != 0x80) {
		_statusOverlayLines.clear();
		return;
	}

	uint16 y = 0xb4;
	for (uint i = 0; i < _statusOverlayLines.size(); ++i) {
		const Common::String &line = _statusOverlayLines[i];
		Common::Span<const byte> text = Logic::textSpan(line);
		const uint16 textWidth = plainTextLineWidth(text);
		const uint16 x = uint16(((0x38 - textWidth) >> 1) + 4);
		paintPlainTextLine(x + 1, y + 1, 0xae, text, false);
		paintPlainTextLine(x, y, 0xeb, text, false);
		y += 9;
	}
}

void Graphics::paintCursorSprite() {
	Logic *logic = _engine->logic();
	if (!logic)
		return;

	if (logic->cursorMode() == 0x20) {
		if (logic->noStep() || logic->dragTarget() == 0)
			return;

		const uint16 id = logic->dragTarget();
		if (_resources->mainDat() && id > _resources->mainDat()->personsCount())
			return;

		// DOS DrawCursorSprite @ 1000:ba8d gets the carried object record
		// and draws its word at offset +8, not the object id itself.
		const uint16 spriteId = uint16(logic->objectField(id, 8)) | (uint16(logic->objectField(id, 9)) << 8);
		if (spriteId == 0xffff)
			return;

		Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
		const Common::Point topLeft(
			int16(cursorPosition().x - sprite->_hotPoint.x),
			int16(cursorPosition().y - sprite->_hotPoint.y));
		paint(sprite.get(), topLeft, kPaintPositionIsTop | kPaintIgnoreHotPoint);
		return;
	}

	if (!logic->stepPending() && logic->noStep())
		return;

	uint16 stepIndex = logic->cursorStepIndex();
	bool stepPending = logic->stepPending();
	uint16 spriteId = 0xffff;
	// DOS DrawCursorSprite @ 1000:ba8d: cursor mode 0x40 (the Op_76 drag-with-
	// target cursor) walks the kMenuCursors table (footer 0x58) keyed by
	// g_drag_target_mode40 (DS:0x667e); every other mode walks the kCursors
	// table (footer 0x54) keyed by the cursor mode itself.
	const uint16 cursorMode = logic->cursorMode();
	const bool menuCursor = cursorMode == 0x40;
	const uint16 cursorKey = menuCursor ? logic->dragTargetMode40() : cursorMode;
	const uint16 cursorFooter = menuCursor ? 0x58 /*kMenuCursors*/ : 0x54 /*kCursors*/;
	if (!_resources->mainDat()->nextCursorSprite(cursorKey, stepIndex, stepPending, spriteId, cursorFooter)) {
		logic->setPendingError(0x26);
		return;
	}

	logic->setCursorStepIndex(stepIndex);
	if (stepPending != logic->stepPending())
		logic->setStepPending(stepPending);

	Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
	const Common::Point topLeft(
		int16(cursorPosition().x - sprite->_hotPoint.x),
		int16(cursorPosition().y - sprite->_hotPoint.y));
	paint(sprite.get(), topLeft, kPaintPositionIsTop | kPaintIgnoreHotPoint);
}

void Graphics::paintCursorObjectName() {
	if (!ConfMan.getBool("show_hover_labels"))
		return;

	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || logic->cursorMode() == 0x80)
		return;

	const Common::String text = EventManager::instance().hoverObjectName(cursorPosition());
	if (text.empty())
		return;

	Common::Span<const byte> line = Logic::textSpan(text);
	const uint16 width = plainTextLineWidth(line);
	const Common::Point cursor = cursorPosition();
	int left = cursor.x - int(width) / 2;
	int top = cursor.y + 11;

	if (left + int(width) + 1 > 320)
		left = 320 - int(width) - 1;
	if (left < 0)
		left = 0;
	if (top + kLineHeight + 1 > 200)
		top = cursor.y - kLineHeight - 5;
	if (top < 0)
		top = 0;

	paintPlainTextLine(uint16(left + 1), uint16(top + 1), 0xae, line);
	paintPlainTextLine(uint16(left), uint16(top), 0xeb, line);
}

void Graphics::setBackdrop(uint16 id) {
	byte palette[0x300];
	_backdrop = Common::SharedPtr<Surface>(_resources->loadBackdrop(id, Common::Span<byte>(palette, sizeof(palette))));
	setPalette(palette, 0, 256);
	_conversationPaletteRestorePending = false;
	prepareInterfacePalette();
	markFullRedraw();
	paintBackdrop();
}

void Graphics::clearBackdrop(byte colour) {
	if (!_backdrop.get()) {
		_backdrop = Common::SharedPtr<Surface>(new Surface);
		_backdrop->create(320, 200);
	}
	_backdrop->fillRect(Common::Rect(0, 0, _backdrop->w, _backdrop->h), colour);
	markFullRedraw();
}

void Graphics::loadGraphicPalette(uint16 id) {
	byte palette[0x300];
	Common::Array<byte> scratch(320 * 200);
	_resources->loadImage(id, Common::Span<byte>(scratch.data(), scratch.size()),
						  Common::Span<byte>(palette, sizeof(palette)));
	setPalette(palette, 0, 256);
}

void Graphics::willFadein(FadeFlags f) {
	_inFade = true;
	_fadeFlags = f;
	if (f & kPartialFade) {
		clearPaletteRange(160, 96);
		storePaletteTarget(_interfacePalette + 160 * 3, 160, 96);
	} else {
		clearPaletteRange(0, 256);
	}
}

void Graphics::paintBackdrop() {
	debugC(3, kDebugLevelGraphics, "painting backdrop");
	if (!_backdrop.get())
		return;

	const int viewHeight = screenHeight();
	_framebuffer->fillRect(Common::Rect(0, 0, 320, viewHeight), 0);
	const Logic *logic = _engine ? _engine->logic() : 0;
	if (logic && logic->scrollChanged())
		markDirtyRect(Common::Rect(0, 0, 320, viewHeight));

	const int srcX = logic ? logic->cameraX() : 0;
	const int srcY = logic ? logic->cameraY() : 0;
	if (srcX < 0 || srcY < 0 || srcX >= _backdrop->w || srcY >= _backdrop->h)
		return;

	const int copyWidth = MIN<int>(320, _backdrop->w - srcX);
	const int copyHeight = MIN<int>(viewHeight, _backdrop->h - srcY);
	const byte *src = _backdrop->pixelAt(srcX, srcY);
	byte *dst = _framebuffer->pixelAt(0, 0);
	for (int y = 0; y < copyHeight; ++y) {
		memcpy(dst, src, copyWidth);
		dst += _framebuffer->pitch;
		src += _backdrop->pitch;
	}

	if (logic) {
		const Common::Array<Logic::OverlayEntry> &overlays = logic->overlayQueue();
		for (Common::Array<Logic::OverlayEntry>::const_iterator it = overlays.begin(); it != overlays.end(); ++it) {
			Common::ScopedPtr<Sprite> overlay(_resources->loadSprite(it->sprite));
			paint(overlay.get(), Common::Point(it->x, it->y), kPaintCameraRelative);
		}
	}
}

void Graphics::paintSpeech() {
	if (_engine->logic()->escBreakPending())
		return;

	if (_speechActive) {
		bool decrementSpeechFrames = false;

		if (!_speechFramesLeft) {
			_speechText.clear();
			_speechActive = false;

			CodePointer cb = _speechDoneCallback;
			const uint16 cbMode = _speechDoneCallbackMode;
			const bool cbHasMode = _speechDoneCallbackHasMode;
			_speechDoneCallback.reset();
			_speechDoneCallbackMode = 0;
			_speechDoneCallbackHasMode = false;
			if (!cb.isEmpty()) {
				if (cbHasMode)
					cb.run(static_cast<OpcodeMode>(cbMode));
				else
					cb.run();
			}

			// Pop the next queued utterance (if any) and start painting it.
			// Run cb FIRST so any side-effects of the previous speech complete
			// before the new one starts (matches DOS behaviour).
			if (!_speechQueue.empty()) {
				SpeechEntry next = _speechQueue.pop();
				_speechText = next.text;
				_speechActive = true;
				_speechFramesLeft = next.frames;
				_speechX = next.x;
				_speechY = next.y;
				_speechColor = next.color;
				_speechMaxLines = next.maxLines;
				_speechBubble = next.bubble;
				_speechBubbleMode = next.bubbleMode;
				_speechDoneCallback = next.cb;
				_speechDoneCallbackMode = next.cbMode;
				_speechDoneCallbackHasMode = next.cbHasMode;
			}
		} else {
			decrementSpeechFrames = true;
		}

		if (_speechActive) {
			Common::Span<const byte> speech = Logic::textSpan(_speechText);
			if (_speechBubble) {
				Sprite bubble;
				bubble._hotPoint = Common::Point(0, 0);
				Common::Rect rect = paintSpeechInBubble(Common::Point(_speechX, _speechY), _speechColor,
														speech, &bubble, _speechBubbleMode, true, _speechMaxLines);
				paint(&bubble, Common::Point(rect.left, rect.top), kPaintSemiTransparent | kPaintPositionIsTop);
			} else {
				Common::Rect rect = paintText(_speechX, _speechY, _speechColor, speech, _framebuffer.get(), 0, 0, kPaintNoDirty);
				markDirtyRect(rect);
			}
		}

		if (decrementSpeechFrames)
			_speechFramesLeft--;
	}

	_engine->logic()->paintSpeechSlots(this);
}

void Graphics::paintAnimations() {
	debugC(3, kDebugLevelGraphics, "painting animations");
	Logic *logic = _engine->logic();
	rebuildDrawCommands(this, logic);
	Common::List<Animation *> animations = _engine->logic()->animations();

	// DOS DrawAllRoomObjects @ 1000:c048 renders layers 0x0b..0x00 and
	// then 0xff. For each layer it draws cast entries, room draw commands,
	// actor slots, then deferred object commands that overlap the actor's
	// feet band.
	for (int16 layer = 0x0b; layer >= 0; --layer) {
		paintAnimationsForLayer(this, animations, layer, false);

		Common::Array<Logic::DrawCommand> deferred;
		const Common::Array<Logic::DrawCommand> &commands = logic->drawCommands();
		for (uint i = 0; i < commands.size(); ++i) {
			if (normalizeLayer(commands[i].layer) != layer)
				continue;
			if (objectDrawShouldDefer(logic, commands[i]))
				deferred.push_back(commands[i]);
			else
				paintDrawCommand(this, logic, commands[i]);
		}

		paintActorAnimationsForLayer(this, logic, animations, layer);
		for (uint i = 0; i < deferred.size(); ++i)
			paintDrawCommand(this, logic, deferred[i]);
		logic->paintDirtyObjectPlacements(this, layer);
	}

	paintAnimationsForLayer(this, animations, -1, false);
	paintActorAnimationsForLayer(this, logic, animations, -1);
}

// it's modal anyway
static int _mOption = 0;
static Common::Rect _optionRects[10];
static uint16 _optionValues[10];

enum {
	kOptionColour = 254,
	kVerbBubbleRawTextColour = 0xdb,
	kSelectedOptionColour = 227,
	kVerbBubbleStashedSelection = 0xfffe,
	kModalCursorTalk = 0x08,
	kModalCursorTopic = 0x01,
	kModalCursorBubble = 0x02,
	kCursorTableNormal = 0x54,
	kCursorTableModal = 0x56
};

static bool modalOptionAt(Common::Point p, uint16 left, uint16 top, uint16 *selectedIndex, uint16 &value) {
	if (_mOption == 0) {
		value = 0xffff;
		return true;
	}

	p.x -= left;
	p.y -= top;
	for (int i = 0; i < _mOption; i++) {
		if (_optionRects[i].contains(p)) {
			if (selectedIndex)
				*selectedIndex = uint16(i);
			value = _optionValues[i];
			return true;
		}
	}

	return false;
}

struct VerbBubbleChoice {
	Common::String label;
	uint16 value;
	Common::Rect rect;
	int16 textLeft;
	int16 textTop;
};

static uint16 verbChoiceLineWidth(const Common::String &text) {
	uint16 width = 0;
	for (uint i = 0; i < text.size(); ++i) {
		const byte ch = byte(text[i]);
		switch (ch) {
		case 0x04:
		case kStringDefaultColour:
			break;
		case kStringSetColour:
			if (i + 1 < text.size())
				++i;
			break;
		case kStringAdvance:
			if (i + 1 < text.size())
				width += byte(text[++i]);
			break;
		default:
			width += Graf.getGlyphWidth(ch);
			break;
		}
	}
	return width;
}

static Common::Array<byte> normalizeBubbleInput(Common::Span<const byte> string) {
	Common::Array<byte> out;
	if (!string.data() || string.size() == 0) {
		out.push_back(0);
		return out;
	}

	uint32 pos = 0;
	bool warned = false;
	auto warnTruncated = [&]() {
		if (!warned) {
			warning("Interspective: truncated verb bubble input at byte %u (size %u)",
					uint(pos), uint(string.size()));
			warned = true;
		}
	};

	while (pos < string.size()) {
		// This rendering pre-pass converts a literal newline 0x0a to the CR
		// (0x0d) row break that formatBubbleText expects. Actor/narrator speech
		// text uses 0x0a AS a newline (see speechTicksForText in actor.cpp,
		// which does the same translation), and live 0x0a/0x0b *conditional
		// markers* never reach this function: the verb-menu opcode path calls
		// formatBubbleText directly on raw script data, and the inline-choice
		// path feeds already-formatted text with markers stripped. Do NOT
		// "classify by raw value to preserve 0x0a markers" here -- that mistakes
		// the byte after a speech newline for a 2-byte global-var offset and
		// over-reads the byte table (formerly an ASan heap-buffer-overflow
		// in formatBubbleText's global-byte lookup).
		byte ch = string.getUint8At(pos++);
		if (ch == '\n')
			ch = '\r';
		out.push_back(ch);
		if (ch == 0)
			return out;

		uint extra = 0;
		if (ch == kStringMove)
			extra = 4;
		else if (ch == kStringGlobalWord || ch == kStringCountSpacesIf0 || ch == kStringCountSpacesIf1)
			extra = 2;
		else if (ch == kStringSetColour || ch == kStringAdvance || ch == kStringCenter)
			extra = 1;

		if (pos + extra > string.size()) {
			warnTruncated();
			break;
		}
		for (uint i = 0; i < extra; ++i)
			out.push_back(string.getUint8At(pos++));

		if (ch != kStringMenuOption)
			continue;

		bool sawLabelTerminator = false;
		while (pos < string.size()) {
			byte lit = string.getUint8At(pos++);
			if (lit == '\n')
				lit = '\r';
			out.push_back(lit);
			if (lit == 0) {
				sawLabelTerminator = true;
				break;
			}
		}
		if (!sawLabelTerminator || pos + 2 > string.size()) {
			warnTruncated();
			break;
		}
		out.push_back(string.getUint8At(pos++));
		out.push_back(string.getUint8At(pos++));
	}

	if (out.empty() || out.back() != 0)
		out.push_back(0);
	return out;
}

static bool parseVerbBubbleChoices(Common::Span<const byte> string, Common::Array<VerbBubbleChoice> &choices,
								   Common::String &displayText) {
	uint32 pos = 0;
	bool warned = false;
	auto warnTruncated = [&]() {
		if (!warned) {
			warning("Interspective: truncated verb bubble choice list at byte %u (size %u)",
					uint(pos), uint(string.size()));
			warned = true;
		}
	};

	while (string.data() && pos < string.size() && string.getUint8At(pos) != 0) {
		if (string.getUint8At(pos) != kStringMenuOption) {
			++pos;
			continue;
		}

		++pos;
		Common::String label;
		while (pos < string.size() && string.getUint8At(pos) != 0)
			label += char(string.getUint8At(pos++));
		if (pos >= string.size()) {
			warnTruncated();
			break;
		}
		++pos;

		if (pos + 2 > string.size()) {
			warnTruncated();
			break;
		}
		const uint16 value = string.getUint16LEAt(pos);
		pos += 2;
		if (label.empty())
			continue;

		// DOS FlushSpeechBubbleLine @ 1000:92ba caps the formatted (mode-1)
		// choice list at 10 entries and raises pending error 0x12 on overflow,
		// emitting neither the text nor a hit-rect for the 11th+ entry.
		if (choices.size() >= 10) {
			Log.setPendingError(0x12);
			break;
		}

		if (!displayText.empty())
			displayText += '\r';

		VerbBubbleChoice choice;
		choice.label = label;
		choice.value = value;
		choice.rect = Common::Rect();
		choice.textLeft = 0;
		choice.textTop = 0;
		choices.push_back(choice);
		displayText += label;
	}

	return !choices.empty();
}

static int verbBubbleChoiceAt(Common::Point p, const Common::Array<VerbBubbleChoice> &choices) {
	for (uint i = 0; i < choices.size(); ++i)
		if (choices[i].rect.contains(p))
			return int(i);
	return -1;
}

static bool modalQuitRequested(Engine *engine) {
	return engine && engine->shouldQuit();
}

static bool handleModalQuitEvent(Engine *engine, const Common::Event &event) {
	if (event.type != Common::EVENT_QUIT && event.type != Common::EVENT_RETURN_TO_LAUNCHER)
		return false;
	if (engine)
		engine->quitGame();
	return true;
}

static int verbBubbleRowShift(uint16 rows) {
	int shift = 0;
	if (rows == 1)
		shift = 0x0c;
	else if (rows == 2)
		shift = 6;
	return shift;
}

// The formatted/inline renderer uses a DIFFERENT 2-row top shift than the raw
// builder: DOS helper @ 1000:9193 loads AX=8 for rows==2 (vs the raw path @ 1000:8d8d
// which uses 6). paintSpeechInBubble already draws formatted 2-line text at
// shift 8 (kSpeechTwoLinesShift), so positionInlineVerbBubbleChoices must match
// that — using verbBubbleRowShift (6) here left the hit-rects/hover 2px above
// the rendered text.
static int verbBubbleRowShiftFormatted(uint16 rows) {
	int shift = 0;
	if (rows == 1)
		shift = 0x0c;
	else if (rows == 2)
		shift = 8;
	return shift;
}

static int16 verbBubbleRowLeft(const Common::Rect &bubbleRect, uint16 rows, uint row) {
	const uint16 remainingRows = uint16(rows - row);
	const int16 indent = (row == 0 || remainingRows == 1) ? 0x19 : 0x0f;
	return int16(bubbleRect.left + indent);
}

static int16 verbBubbleFirstRowTop(const Common::Rect &bubbleRect, uint16 rows) {
	return int16(bubbleRect.top + 8 + verbBubbleRowShift(rows));
}

static void positionVerbBubbleChoices(const Common::Rect &bubbleRect,
									  const Logic::FormattedBubble &metrics, Common::Array<VerbBubbleChoice> &choices) {
	(void)metrics;
	const uint16 rows = MAX<uint16>(1, uint16(choices.size()));
	int16 top = verbBubbleFirstRowTop(bubbleRect, rows);
	for (uint i = 0; i < choices.size(); ++i) {
		const uint16 lineWidth = verbChoiceLineWidth(choices[i].label);
		choices[i].textLeft = verbBubbleRowLeft(bubbleRect, rows, i);
		choices[i].textTop = top;
		choices[i].rect = Common::Rect(choices[i].textLeft, top,
									   choices[i].textLeft + lineWidth, top + Graphics::kLineHeight);
		top += Graphics::kLineHeight;
	}
}

static void positionInlineVerbBubbleChoices(Graphics *graphics, const Common::Rect &bubbleRect,
											Common::Span<const byte> string, Common::Array<VerbBubbleChoice> &choices) {
	choices.clear();
	if (!graphics || !string.data())
		return;

	Common::Array<byte> normalized = normalizeBubbleInput(string);
	Logic::FormattedBubble metrics = Log.formatBubbleText(Common::Span<const byte>(&normalized[0], normalized.size()));
	const uint16 rows = MAX<uint16>(1, metrics.rowCount);
	const uint16 roundedWidthExtra = metrics.maxLineWidth & ~uint16(3);
	uint16 currentLeft = uint16(bubbleRect.left + 15 + 10);
	uint16 currentTop = uint16(bubbleRect.top + 8 + verbBubbleRowShiftFormatted(rows));
	uint16 remainingRows = rows;
	Common::Span<const byte> formatted = Logic::textSpan(metrics.text);
	uint32 pos = 0;

	while (pos < formatted.size() && formatted.getUint8At(pos) != 0) {
		const byte ch = formatted.getUint8At(pos++);
		switch (ch) {
		case kStringCenter: {
			if (pos >= formatted.size())
				return;
			const byte lineWidth = formatted.getUint8At(pos++);
			const uint16 centered = uint16(roundedWidthExtra + 0x41 - lineWidth);
			currentLeft = uint16(bubbleRect.left + (centered >> 1));
			break;
		}
		case kStringAdvance:
			if (pos >= formatted.size())
				return;
			currentLeft += formatted.getUint8At(pos++);
			break;
		case '\n':
		case '\r':
			if (remainingRows != 0)
				--remainingRows;
			if (remainingRows == 0)
				return;
			currentLeft = uint16(bubbleRect.left + 15);
			if (remainingRows == 1)
				currentLeft += 10;
			currentTop += Graphics::kLineHeight;
			break;
		case kStringDefaultColour:
			break;
		case kStringSetColour:
			if (pos >= formatted.size())
				return;
			++pos;
			break;
		case kStringMenuOption: {
			VerbBubbleChoice choice;
			choice.textLeft = currentLeft;
			choice.textTop = currentTop;
			while (pos < formatted.size() && formatted.getUint8At(pos) != 0) {
				const byte lit = formatted.getUint8At(pos++);
				choice.label += char(lit);
				// DOS render loop @ 1000:928c (CMP AL,0x4 / JZ) skips 0x04
				// entirely — no glyph, no cursor advance — and the stored
				// hit-rect uses that same cursor. Keep 0x04 in the label
				// (paintPlainTextLine skips it when drawing) but do not let
				// it widen the rect, or the highlight and click target drift.
				if (lit == 0x04)
					continue;
				currentLeft += graphics->getGlyphWidth(lit);
			}
			if (pos >= formatted.size())
				return;
			++pos;
			if (pos + 2 > formatted.size())
				return;
			choice.value = formatted.getUint16LEAt(pos);
			pos += 2;
			choice.rect = Common::Rect(choice.textLeft, choice.textTop,
									   currentLeft, currentTop + Graphics::kLineHeight);
			if (!choice.label.empty())
				choices.push_back(choice);
			break;
		}
		default:
			currentLeft += graphics->getGlyphWidth(ch);
			break;
		}
	}
}

static void collectVerbBubbleLines(Common::Span<const byte> string, Common::Array<Common::String> &lines) {
	Common::String line;
	if (!string.data())
		return;
	for (uint32 pos = 0; pos < string.size() && string.getUint8At(pos) != 0; ++pos) {
		const byte ch = string.getUint8At(pos);
		if (ch == '\r' || ch == '\n') {
			if (!line.empty())
				lines.push_back(line);
			line.clear();
			continue;
		}
		line += char(ch);
	}
	if (!line.empty())
		lines.push_back(line);
}

static void paintVerbBubbleLines(Graphics *graphics, const Common::Rect &bubbleRect,
								 const Common::Array<Common::String> &lines, byte colour) {
	if (!graphics || lines.empty())
		return;
	const uint16 rows = uint16(lines.size());
	int16 top = verbBubbleFirstRowTop(bubbleRect, rows);
	for (uint i = 0; i < lines.size(); ++i) {
		graphics->paintPlainTextLine(verbBubbleRowLeft(bubbleRect, rows, i), top, colour,
									 Logic::textSpan(lines[i]), false);
		top += Graphics::kLineHeight;
	}
}

static Graphics::SpeechBubbleMode verbBubbleModeForPalette(byte paletteMode) {
	// DrawVerbBubble_DispatchByMode:
	//   mode 1 renders formatted text through RenderSpeechBubble at
	//   (0xe6,0x5d); mode 2 lays out raw choice rows through the left
	//   speech-frame helper at (0x55,0x53); mode 3 renders formatted text
	//   through RenderSpeechBubbleBottomRight at (0x5a,0x5d). Mode 4 draws
	//   a raw active row list plus a stashed formatted bubble.
	if (paletteMode == 1)
		return Graphics::kSpeechBubbleType1;
	if (paletteMode == 2)
		return Graphics::kSpeechBubbleVerbTopLeft;
	if (paletteMode == 4)
		return Graphics::kSpeechBubbleVerbBottomLeft;
	return Graphics::kSpeechBubbleType2;
}

static Common::Point verbBubbleAnchorForPalette(byte paletteMode) {
	if (paletteMode == 1)
		return Common::Point(0xe6, 0x5d);
	if (paletteMode == 2)
		return Common::Point(0x55, 0x53);
	if (paletteMode == 4)
		return Common::Point(0x5a, 0x5d);
	return Common::Point(0x5a, 0x5d);
}

static void paintConversationPortraitSprite(Graphics *graphics, Resources *resources,
											uint16 spriteId, Common::Point pos, Surface *dest) {
	if (!resources || spriteId == 0xffff)
		return;
	Common::ScopedPtr<Sprite> sprite(resources->loadSprite(spriteId));
	if (!sprite || sprite->w == 0 || sprite->h == 0)
		return;
	graphics->paint(sprite.get(), pos, dest,
					Graphics::kPaintPositionIsTop | Graphics::kPaintNoDirty);
}

void Graphics::paintConversationBackdrop() {
	// RunVerbMenuModalLoop @ 1000:8730 clears the 320x200 work/video
	// buffer to 0xae, then DrawVerbBubbleSprites @ 1000:8bd7 draws the
	// Op_4d stashed portrait sprites at (0,0x17) and (0xcb,0x17).
	_framebuffer->fillRect(Common::Rect(0, 0, 320, 200), 0xae);
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic)
		return;
	paintConversationPortraitSprite(this, _resources, logic->menuStashA(),
									Common::Point(0, 0x17), _framebuffer.get());
	paintConversationPortraitSprite(this, _resources, logic->menuStashB(),
									Common::Point(0xcb, 0x17), _framebuffer.get());
}

void Graphics::prepareConversationPalette() {
	// DrawVerbBubbleSprites calls DrawClippedSpriteTransparent. Its
	// PrepSpriteAddress/EnsureGraphicLoaded path reloads fullscreen sprite
	// images while g_palette_overridden is nonzero; LoadGraphicToSlot then
	// reads the embedded palette into g_palette_room before the modal is
	// presented. Reapply the two portrait image palettes in the same draw
	// order, so the right portrait's palette is the final active palette.
	Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !_resources)
		return;

	const uint16 sprites[2] = {logic->menuStashA(), logic->menuStashB()};
	for (uint i = 0; i < ARRAYSIZE(sprites); ++i) {
		if (sprites[i] == 0xffff)
			continue;
		const SpriteInfo info = _resources->getSpriteInfo(sprites[i]);
		if (info.imageId() != 0)
			loadGraphicPalette(info.imageId());
	}
}

void Graphics::beginConversationModal() {
	if (!_conversationPaletteRestorePending)
		memcpy(_conversationSavedPalette, _roomPalette, sizeof(_conversationSavedPalette));
	prepareConversationPalette();
	_conversationPaletteRestorePending = true;
}

void Graphics::finishConversationModal() {
	// RunVerbMenuModalLoop @ 1000:8730 clears the palette-override flag
	// but does not blit the previous room back between modal sentences.
	// Keep the portrait frame visible; the next normal paint restores the
	// room palette before redrawing gameplay.
	_conversationPaletteRestorePending = true;
	markFullRedraw();
}

void Graphics::restoreConversationPalette() {
	if (!_conversationPaletteRestorePending)
		return;
	setPalette(_conversationSavedPalette, 0, 256);
	_conversationPaletteRestorePending = false;
}

static byte verbBubbleTextColourForPalette(byte paletteMode) {
	if (paletteMode == 1)
		return 0xf5;
	if (paletteMode == 2)
		return 0xeb;
	if (paletteMode == 3)
		return 0xeb;
	if (paletteMode == 4)
		return 0xeb;
	return 0xeb;
}

static void paintStashedVerbBubble(Graphics *graphics, Logic *logic, Surface *dest,
								   int hover, Common::Array<VerbBubbleChoice> *choicesOut) {
	if (choicesOut)
		choicesOut->clear();
	if (!graphics || !logic || !dest)
		return;

	const Common::String &savedText = logic->modalState().savedText;
	if (savedText.empty())
		return;

	Sprite bubble;
	bubble._hotPoint = Common::Point(0, 0);
	Common::Rect bubbleRect = graphics->paintSpeechInBubble(Common::Point(0xe6, 0x5d), 0xf5,
															Logic::textSpan(savedText), &bubble, Graphics::kSpeechBubbleType1, true);
	graphics->paint(&bubble, Common::Point(bubbleRect.left, bubbleRect.top), dest,
					Graphics::kPaintSemiTransparent | Graphics::kPaintPositionIsTop);

	Common::Array<VerbBubbleChoice> localChoices;
	Common::Array<VerbBubbleChoice> &choices = choicesOut ? *choicesOut : localChoices;
	positionInlineVerbBubbleChoices(graphics, bubbleRect, Logic::textSpan(savedText), choices);

	if (hover >= 0 && hover < int(choices.size()))
		graphics->paintPlainTextLine(choices[hover].textLeft, choices[hover].textTop,
									 kSelectedOptionColour,
									 Logic::textSpan(choices[hover].label), false);
}

static void paintVerbBubbleConnectors(Graphics *graphics, Resources *resources,
									  byte paletteMode, Common::Point anchor, Surface *dest) {
	if (!graphics || !resources || !dest)
		return;
	if (paletteMode != 2 && paletteMode != 4)
		return;

	Sprite *const *bubbles = resources->bubbles();
	const Sprite *stem = bubbles[kBubbleVerbStem];           // DOS CS:[0x109], footer +0xaa
	const Sprite *connector = bubbles[kBubbleVerbConnector]; // DOS CS:[0x107], footer +0xa8
	if (!stem || !connector)
		return;

	Common::Point stemPos = anchor;
	Common::Point connectorPos(anchor.x + 0x0a, anchor.y);
	if (paletteMode == 2) {
		stemPos.y -= 0x08;
		connectorPos.y -= 0x14;
	} else {
		connectorPos.y += 0x08;
	}

	graphics->paint(stem, stemPos, dest, Graphics::kPaintPositionIsTop);
	graphics->paint(connector, connectorPos, dest, Graphics::kPaintPositionIsTop);
}

uint16 Graphics::askVerbBubble(byte paletteMode, Common::Span<const byte> string, uint16 *selectedIndex) {
	if (selectedIndex)
		*selectedIndex = 0xffff;

	Common::Array<VerbBubbleChoice> choices;
	Common::Array<VerbBubbleChoice> stashedChoices;
	Common::String displayText;
	if (!parseVerbBubbleChoices(string, choices, displayText))
		return 0xffff;

	const SpeechBubbleMode bubbleMode = verbBubbleModeForPalette(paletteMode);
	const Common::Point anchor = verbBubbleAnchorForPalette(paletteMode);
	const byte textColour = verbBubbleTextColourForPalette(paletteMode);
	Logic::FormattedBubble metrics = Log.formatBubbleText(Logic::textSpan(displayText));
	Common::Rect activeBubbleRect;

	beginConversationModal();

	auto paintModalFrame = [&](int hover, int stashedHover) {
		paintConversationBackdrop();
		paintVerbBubbleConnectors(this, _resources, paletteMode, anchor, _framebuffer.get());

		Sprite bubble;
		bubble._hotPoint = Common::Point(0, 0);
		Common::Rect bubbleRect = paintSpeechInBubble(anchor, textColour,
													  Logic::textSpan(displayText), &bubble, bubbleMode, false);
		activeBubbleRect = bubbleRect;
		paint(&bubble, Common::Point(bubbleRect.left, bubbleRect.top),
			  _framebuffer.get(), kPaintSemiTransparent | kPaintPositionIsTop);

		positionVerbBubbleChoices(bubbleRect, metrics, choices);
		const byte choiceColour = (paletteMode == 2 || paletteMode == 4)
									  ? kVerbBubbleRawTextColour
									  : textColour;
		for (uint i = 0; i < choices.size(); ++i)
			paintPlainTextLine(choices[i].textLeft, choices[i].textTop, choiceColour,
							   Logic::textSpan(choices[i].label), false);
		if (hover >= 0 && hover < int(choices.size()))
			paintPlainTextLine(choices[hover].textLeft, choices[hover].textTop,
							   kSelectedOptionColour,
							   Logic::textSpan(choices[hover].label), false);
		if (paletteMode == 4)
			paintStashedVerbBubble(this, _engine ? _engine->logic() : 0, _framebuffer.get(),
								   stashedHover, &stashedChoices);

		_system->copyRectToScreen(_framebuffer->pixels(),
								  _framebuffer->pitch, 0, 0, 320, 200);
		_system->updateScreen();
	};

	updateModalCursor(kModalCursorTalk, kCursorTableNormal, true);
	bool done = false;
	uint16 result = 0xffff;
	uint8 clickDelay = 10;
	int hover = -1;
	int stashedHover = -1;
	paintModalFrame(hover, stashedHover);

	auto updateHover = [&](Common::Point p) {
		stashedHover = (paletteMode == 4) ? verbBubbleChoiceAt(p, stashedChoices) : -1;
		hover = (stashedHover >= 0) ? -1 : verbBubbleChoiceAt(p, choices);
	};

	auto updateCursor = [&]() {
		const bool topicHover = stashedHover >= 0;
		const bool bubbleHover = activeBubbleRect.contains(_engine->eventMan()->getMousePos());
		if (topicHover) {
			updateModalCursor(kModalCursorTopic, kCursorTableModal, true);
		} else if (bubbleHover) {
			updateModalCursor(kModalCursorBubble, kCursorTableModal, true);
		} else {
			updateModalCursor(kModalCursorTalk, kCursorTableNormal, false);
		}
	};

	updateHover(_engine->eventMan()->getMousePos());
	updateCursor();
	paintModalFrame(hover, stashedHover);

	auto acceptHover = [&]() -> bool {
		if (stashedHover >= 0) {
			if (selectedIndex)
				*selectedIndex = kVerbBubbleStashedSelection;
			result = stashedChoices[stashedHover].value;
			return true;
		}
		if (hover >= 0) {
			if (selectedIndex)
				*selectedIndex = uint16(hover);
			result = choices[hover].value;
			return true;
		}
		return false;
	};

	while (!done && !modalQuitRequested(_engine)) {
		_engine->debugger()->onFrame();
		Common::Event event;
		while (_engine->eventMan()->pollEvent(event)) {
			if (handleModalQuitEvent(_engine, event)) {
				result = 0xffff;
				done = true;
				break;
			}
			switch (event.type) {
			case Common::EVENT_MOUSEMOVE:
				setCursorPosition(event.mouse);
				updateHover(event.mouse);
				break;
			case Common::EVENT_KEYDOWN:
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					done = true;
				break;
			case Common::EVENT_RBUTTONDOWN:
				// DOS RunVerbMenuModalLoop @ 1000:879e cancels the menu on a
				// right-click (g_buttons_locked==2), returning no selection.
				// Gate on the same debounce the left-click selection uses.
				setCursorPosition(event.mouse);
				if (clickDelay == 0) {
					result = 0xffff;
					done = true;
				}
				break;
			case Common::EVENT_RBUTTONUP:
				setCursorPosition(event.mouse);
				break;
			case Common::EVENT_LBUTTONDOWN:
				setCursorPosition(event.mouse);
				updateHover(event.mouse);
				if (clickDelay == 0)
					done = acceptHover();
				break;
			default:
				break;
			}
			if (done)
				break;
		}

		if (!done && clickDelay == 0 && (_engine->eventMan()->getButtonState() & 1)) {
			const Common::Point mouse = _engine->eventMan()->getMousePos();
			setCursorPosition(mouse);
			updateHover(mouse);
			done = acceptHover();
		} else if (clickDelay != 0) {
			--clickDelay;
		}

		if (!done) {
			updateCursor();
			paintModalFrame(hover, stashedHover);
		}
		_system->delayMillis(1000 / 60);
	}

	hideCursor();
	finishConversationModal();
	return result;
}

uint16 Graphics::askVerbBubbleText(byte paletteMode, Common::Span<const byte> string, uint16 *selectedIndex,
								   uint16 timeoutFrames) {
	if (selectedIndex)
		*selectedIndex = 0xffff;
	if (!string.data() || string.size() == 0 || string.getUint8At(0) == 0)
		return 0xffff;

	const SpeechBubbleMode bubbleMode = verbBubbleModeForPalette(paletteMode);
	const Common::Point anchor = verbBubbleAnchorForPalette(paletteMode);
	const byte textColour = verbBubbleTextColourForPalette(paletteMode);
	Common::Array<VerbBubbleChoice> choices;
	Common::Array<VerbBubbleChoice> stashedChoices;

	beginConversationModal();

	auto paintModalFrame = [&](int hover, int stashedHover) {
		paintConversationBackdrop();
		paintVerbBubbleConnectors(this, _resources, paletteMode, anchor, _framebuffer.get());

		Sprite bubble;
		bubble._hotPoint = Common::Point(0, 0);
		Common::Rect bubbleRect = paintSpeechInBubble(anchor, textColour, string, &bubble, bubbleMode, true);
		paint(&bubble, Common::Point(bubbleRect.left, bubbleRect.top),
			  _framebuffer.get(), kPaintSemiTransparent | kPaintPositionIsTop);
		positionInlineVerbBubbleChoices(this, bubbleRect, string, choices);
		if (hover >= 0 && hover < int(choices.size()))
			paintPlainTextLine(choices[hover].textLeft, choices[hover].textTop,
							   kSelectedOptionColour,
							   Logic::textSpan(choices[hover].label), false);
		if (paletteMode == 4)
			paintStashedVerbBubble(this, _engine ? _engine->logic() : 0, _framebuffer.get(),
								   stashedHover, &stashedChoices);

		_system->copyRectToScreen(_framebuffer->pixels(),
								  _framebuffer->pitch, 0, 0, 320, 200);
		_system->updateScreen();
	};

	updateModalCursor(kModalCursorTalk, kCursorTableNormal, true);
	bool done = false;
	uint16 result = 0xffff;
	uint8 clickDelay = 10;
	int hover = -1;
	int stashedHover = -1;
	uint16 framesLeft = timeoutFrames;
	paintModalFrame(hover, stashedHover);

	auto updateHover = [&](Common::Point p) {
		stashedHover = (paletteMode == 4) ? verbBubbleChoiceAt(p, stashedChoices) : -1;
		hover = (stashedHover >= 0) ? -1 : verbBubbleChoiceAt(p, choices);
	};

	auto updateCursor = [&]() {
		const bool topicHover = hover >= 0 || stashedHover >= 0;
		updateModalCursor(topicHover ? kModalCursorTopic : kModalCursorTalk,
						  topicHover ? kCursorTableModal : kCursorTableNormal,
						  topicHover);
	};

	updateHover(_engine->eventMan()->getMousePos());
	updateCursor();
	paintModalFrame(hover, stashedHover);

	auto acceptHover = [&]() -> bool {
		if (stashedHover >= 0) {
			if (selectedIndex)
				*selectedIndex = kVerbBubbleStashedSelection;
			result = stashedChoices[stashedHover].value;
			return true;
		}
		if (hover >= 0) {
			if (selectedIndex)
				*selectedIndex = uint16(hover);
			result = choices[hover].value;
			return true;
		}
		return false;
	};

	while (!done && !modalQuitRequested(_engine)) {
		_engine->debugger()->onFrame();
		Common::Event event;
		while (_engine->eventMan()->pollEvent(event)) {
			if (handleModalQuitEvent(_engine, event)) {
				result = 0xffff;
				done = true;
				break;
			}
			switch (event.type) {
			case Common::EVENT_MOUSEMOVE:
				setCursorPosition(event.mouse);
				updateHover(event.mouse);
				break;
			case Common::EVENT_KEYDOWN:
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					done = true;
				break;
			case Common::EVENT_RBUTTONDOWN:
				// DOS RunVerbMenuModalLoop @ 1000:879e cancels the menu on a
				// right-click (g_buttons_locked==2), returning no selection.
				// Gate on the same debounce the left-click selection uses.
				setCursorPosition(event.mouse);
				if (clickDelay == 0) {
					result = 0xffff;
					done = true;
				}
				break;
			case Common::EVENT_RBUTTONUP:
				setCursorPosition(event.mouse);
				break;
			case Common::EVENT_LBUTTONDOWN:
				setCursorPosition(event.mouse);
				updateHover(event.mouse);
				if (clickDelay == 0)
					done = acceptHover();
				break;
			default:
				break;
			}
			if (done)
				break;
		}

		if (!done && clickDelay == 0 && (_engine->eventMan()->getButtonState() & 1)) {
			const Common::Point mouse = _engine->eventMan()->getMousePos();
			setCursorPosition(mouse);
			updateHover(mouse);
			done = acceptHover();
		} else if (clickDelay != 0) {
			--clickDelay;
		}

		if (!done && timeoutFrames != 0) {
			if (framesLeft <= 1) {
				result = 0xffff;
				done = true;
			} else {
				--framesLeft;
			}
		}

		if (!done) {
			updateCursor();
			paintModalFrame(hover, stashedHover);
		}
		_system->delayMillis(1000 / 60);
	}

	hideCursor();
	finishConversationModal();
	return result;
}

bool Graphics::showVerbBubbleText(byte paletteMode, Common::Span<const byte> string, uint16 frames,
								  uint16 forcedRows, uint16 forcedWidthExtra) {
	if (!string.data() || string.size() == 0 || string.getUint8At(0) == 0)
		return false;

	const SpeechBubbleMode bubbleMode = verbBubbleModeForPalette(paletteMode);
	const Common::Point anchor = verbBubbleAnchorForPalette(paletteMode);
	const byte textColour = verbBubbleTextColourForPalette(paletteMode);
	const uint16 dosRows = forcedRows != 0 ? forcedRows : 0;
	const uint16 dosWidthExtra = forcedWidthExtra;

	beginConversationModal();

	auto paintModalFrame = [&]() {
		paintConversationBackdrop();
		paintVerbBubbleConnectors(this, _resources, paletteMode, anchor, _framebuffer.get());

		Sprite bubble;
		bubble._hotPoint = Common::Point(0, 0);
		const bool rawLayout = paletteMode == 2 || paletteMode == 4;
		Common::Rect bubbleRect = paintSpeechInBubble(anchor, textColour, string, &bubble, bubbleMode,
													  !rawLayout, dosRows, dosWidthExtra);
		paint(&bubble, Common::Point(bubbleRect.left, bubbleRect.top),
			  _framebuffer.get(), kPaintSemiTransparent | kPaintPositionIsTop);
		if (rawLayout) {
			Common::Array<Common::String> lines;
			collectVerbBubbleLines(string, lines);
			paintVerbBubbleLines(this, bubbleRect, lines, kVerbBubbleRawTextColour);
		}
		if (paletteMode == 4)
			paintStashedVerbBubble(this, _engine ? _engine->logic() : 0, _framebuffer.get(), -1, 0);

		_system->copyRectToScreen(_framebuffer->pixels(),
								  _framebuffer->pitch, 0, 0, 320, 200);
		_system->updateScreen();
	};

	updateModalCursor(kModalCursorTalk, kCursorTableNormal, true);
	const uint16 ticks = MAX<uint16>(1, frames);
	bool continueModal = true;
	paintModalFrame();
	for (uint16 i = 0; i < ticks; ++i) {
		_engine->debugger()->onFrame();
		Common::Event event;
		bool done = false;
		while (_engine->eventMan()->pollEvent(event)) {
			if (handleModalQuitEvent(_engine, event)) {
				continueModal = false;
				done = true;
				break;
			}
			switch (event.type) {
			case Common::EVENT_MOUSEMOVE:
				setCursorPosition(event.mouse);
				break;
			case Common::EVENT_KEYDOWN:
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
					continueModal = false;
					done = true;
				}
				break;
			case Common::EVENT_RBUTTONDOWN:
				setCursorPosition(event.mouse);
				if (i >= 2)
					done = true;
				break;
			case Common::EVENT_RBUTTONUP:
				setCursorPosition(event.mouse);
				break;
			default:
				break;
			}
			if (done)
				break;
		}
		if (!done && i >= 2 && (_engine->eventMan()->getButtonState() & 2)) {
			setCursorPosition(_engine->eventMan()->getMousePos());
			done = true;
		}
		if (!done && modalQuitRequested(_engine)) {
			continueModal = false;
			done = true;
		}
		if (done)
			break;
		updateModalCursor(kModalCursorTalk, kCursorTableNormal, false);
		paintModalFrame();
		_system->delayMillis(1000 / 60);
	}

	hideCursor();
	finishConversationModal();
	return continueModal && !modalQuitRequested(_engine);
}

uint16 Graphics::ask(uint16 left, uint16 top, byte width, byte height, Common::Span<const byte> string, uint16 *selectedIndex) {
	if (selectedIndex)
		*selectedIndex = 0xffff;

	// PlaceCursorMenu @ 1000:7f87 stores the menu origin, clamps it to
	// the DOS menu area, then positions the cursor near the menu center.
	const uint8 rawWidth = width;
	const uint8 rawHeight = height;
	const int16 dosWidth = int16(uint16(rawWidth) * 0x10 + 0x17);
	const int16 dosHeight = int16(uint16(rawHeight) * 0x0c + 0x13);
	int16 menuLeft = int16(left);
	int16 menuTop = int16(top);
	if (menuLeft + dosWidth >= 0x137)
		menuLeft = int16(0x11f - dosWidth);
	if (menuLeft < 0) {
		Log.setPendingError(0x3e);
		return 0xffff;
	}
	if (menuTop + dosHeight >= 0xbf)
		menuTop = int16(0xab - dosHeight);
	if (menuTop < 0) {
		Log.setPendingError(0x3e);
		return 0xffff;
	}
	left = uint16(menuLeft);
	top = uint16(menuTop);
	setCursorPosition(Common::Point(
		int16(menuLeft + 1 + dosWidth / 2),
		int16(menuTop + 1 + dosHeight / 2 + 10)));

	width += 2;
	height += 2;
	enum {
		kFrameTileHeight = 12,
		kFrameTileWidth = 16
	};

	Surface frame;
	frame.create(width * kFrameTileWidth, height * kFrameTileHeight + 4);

	Sprite *const *frames = _resources->frames();

	Common::Point tile(0, 0);

	paint(frames[kFrameTopLeft], tile, &frame);
	tile.x += kFrameTileWidth;
	for (int x = 1; x < width - 1; x++) {
		paint(frames[kFrameTop], tile, &frame);
		tile.x += kFrameTileWidth;
	}
	paint(frames[kFrameTopRight], tile, &frame);

	tile.y += kFrameTileHeight;
	tile.x = 0;

	for (int y = 1; y < height - 1; y++) {
		paint(frames[kFrameLeft], tile, &frame);
		tile.x += kFrameTileWidth;
		for (int x = 1; x < width - 1; x++) {
			paint(frames[kFrameFill], tile, &frame);
			tile.x += kFrameTileWidth;
		}
		paint(frames[kFrameRight], tile, &frame);
		tile.y += kFrameTileHeight;
		tile.x = 0;
	}

	paint(frames[kFrameBottomLeft], tile, &frame);
	tile.x += kFrameTileWidth;
	for (int x = 1; x < width - 1; x++) {
		paint(frames[kFrameBottom], tile, &frame);
		tile.x += kFrameTileWidth;
	}
	paint(frames[kFrameBottomRight], tile, &frame);

	_mOption = 0;

	// TODO this should use the interpreter's built-in font
	// (but it does look nicer this way)
	paintText(10, 16, 254, string, &frame);

	_system->copyRectToScreen(frame.pixels(), frame.pitch, left, top, width * kFrameTileWidth, height * kFrameTileHeight + 4);

	showCursor();
	bool done = false;
	uint16 result = 0xffff;
	uint8 clickDelay = 10;
	while (!done && !modalQuitRequested(_engine)) {
		_system->updateScreen();
		_engine->debugger()->onFrame();
		Common::Event event;
		while (_engine->eventMan()->pollEvent(event)) {
			if (handleModalQuitEvent(_engine, event)) {
				result = 0xffff;
				done = true;
				break;
			}
			switch (event.type) {
			case Common::EVENT_KEYDOWN:
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					done = true;
				break;
			case Common::EVENT_RBUTTONDOWN:
			case Common::EVENT_RBUTTONUP:
				done = true;
				break;
			case Common::EVENT_LBUTTONDOWN:
				if (clickDelay != 0)
					break;
				{
					uint16 value = 0xffff;
					if (modalOptionAt(event.mouse, left, top, selectedIndex, value)) {
						result = value;
						done = true;
					}
				}
				break;
			default:
				break;
			}
			if (done)
				break;
		}
		if (!done && clickDelay == 0 && (_engine->eventMan()->getButtonState() & 1)) {
			uint16 value = 0xffff;
			if (modalOptionAt(_engine->eventMan()->getMousePos(), left, top, selectedIndex, value)) {
				result = value;
				done = true;
			}
		} else if (clickDelay != 0) {
			--clickDelay;
		}
		_system->delayMillis(1000 / 60);
	}

	hideCursor();
	return result;
}

void Graphics::paintSpeechBubbleColumn(Sprite *top, Sprite *fill, Common::Point &point, uint8 fill_tiles, Surface *dest) {
	if (top)
		paint(top, point, dest, kPaintPositionIsTop);
	point.y += 24;
	for (int i = 0; i < fill_tiles; i++) {
		paint(fill, point, dest, kPaintPositionIsTop);
		point.y += 6;
	}
}

Common::Rect Graphics::paintSpeechInBubble(Common::Point pos, byte colour, Common::Span<const byte> string, Surface *bubble,
										   SpeechBubbleMode mode, bool renderText, uint16 forcedRows,
										   uint16 forcedWidthExtra) {
	int left = pos.x;
	int top = pos.y;
	const char *debugText = string.data() ? reinterpret_cast<const char *>(string.data()) : "(null)";
	debugC(1, kDebugLevelGraphics, "painting speech bubble \"%s\" at %d:%d", debugText, left, top);

	Common::Array<byte> normalized = normalizeBubbleInput(string);
	Logic::FormattedBubble metrics = Log.formatBubbleText(Common::Span<const byte>(&normalized[0], normalized.size()));
	const uint16 rows = forcedRows != 0 ? forcedRows : MAX<uint16>(1, metrics.rowCount);
	const uint16 widthExtra = forcedWidthExtra != 0xffff ? forcedWidthExtra : metrics.maxLineWidth;

	Sprite *const *bubbles = _resources->bubbles();

	const uint8 kNoBubblePart = 0xff;
	uint8 bubble_indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

	uint8 wadj = 0;
	uint8 frameOffsetX = 0;
	uint8 frameOffsetY = 0;
	uint8 topTailPart = kNoBubblePart;
	int16 topTailX = 0;
	const bool verbRawTopLeft = mode == kSpeechBubbleVerbTopLeft;
	const bool verbRawBottomLeft = mode == kSpeechBubbleVerbBottomLeft;
	const int frameHeight = (rows >= 3) ? (rows * kLineHeight + 16) : 60;
	const int rawTopLeftAnchorHeight = (rows >= 3) ? (rows * kLineHeight + 0x20) : 60;
	const int height = verbRawTopLeft ? rawTopLeftAnchorHeight : frameHeight;

	enum DosBubbleVariant {
		kDosRenderSpeechBubble,
		kDosRenderSpeechBubbleTopLeft,
		kDosRenderSpeechBubbleBottomRight,
		kDosRenderSpeechBubbleTopRight,
		kDosLayoutVerbBubbleTopLeft,
		kDosLayoutVerbBubbleBottomLeft
	};

	DosBubbleVariant variant;
	if (verbRawTopLeft) {
		variant = kDosLayoutVerbBubbleTopLeft;
	} else if (verbRawBottomLeft) {
		variant = kDosLayoutVerbBubbleBottomLeft;
	} else if (mode == kSpeechBubbleType1) {
		variant = (top < 0x35) ? kDosRenderSpeechBubbleTopLeft : kDosRenderSpeechBubble;
	} else if (mode == kSpeechBubbleType2) {
		variant = (top < 0x35) ? kDosRenderSpeechBubbleTopRight : kDosRenderSpeechBubbleBottomRight;
	} else {
		// DOS DispatchSpeechBubbleByCorner @ 1000:9a9f adjusts the actor
		// anchor before tail-orientation dispatch.
		top += 4;
		if (left < 0xa0) {
			left += 0x12;
			variant = (top < 0x35) ? kDosRenderSpeechBubbleTopRight : kDosRenderSpeechBubbleBottomRight;
		} else {
			variant = (top < 0x35) ? kDosRenderSpeechBubbleTopLeft : kDosRenderSpeechBubble;
		}
	}

	switch (variant) {
	case kDosRenderSpeechBubble:
		// DOS RenderSpeechBubble @ 1000:8f80: bubble grows up-left.
		if (top < height)
			top = 0;
		else
			top -= height;
		if (left >= 320)
			left = 320;
		if (int(widthExtra + 0x45) > left)
			left = 0;
		else
			left -= widthExtra + 0x45;
		bubble_indices[kBubbleBottomRight] = kBubbleBottomRightPoint;
		break;
	case kDosRenderSpeechBubbleTopLeft:
		// DOS RenderSpeechBubbleTopLeft @ 1000:8ff9: bubble grows down-left.
		top += 7;
		if (top < 0)
			top = 0;
		if (left >= 320)
			left = 320;
		if (int(MAX<uint16>(widthExtra, 4) + 0x45) > left)
			left = 0;
		else
			left -= MAX<uint16>(widthExtra, 4) + 0x45;
		// RenderSpeechBubbleTopLeft skips the normal top-right corner
		// (DS:0x668e = -1) and overlays CS:[0x105] at y - 7.
		bubble_indices[kBubbleTopRight] = kNoBubblePart;
		frameOffsetY = 7;
		topTailPart = kBubbleTopRightPoint;
		topTailX = int16(MAX<uint16>(4, uint16((widthExtra / 4) * 4)) + 0x21);
		break;
	case kDosRenderSpeechBubbleBottomRight:
		// DOS RenderSpeechBubbleBottomRight @ 1000:9086: bubble grows up-right.
		if (top < height)
			top = 0;
		else
			top -= height;
		wadj = 4;
		left += 4;
		bubble_indices[kBubbleBottomLeft] = kBubbleBottomLeftPoint;
		break;
	case kDosRenderSpeechBubbleTopRight:
		// DOS RenderSpeechBubbleTopRight @ 1000:90ee: bubble grows down-right.
		top += 7;
		if (top < 0)
			top = 0;
		left += 4;
		// RenderSpeechBubbleTopRight skips the normal top-left corner
		// (DS:0x668c = -1) and overlays CS:[0x103] at x - 4, y - 7.
		bubble_indices[kBubbleTopLeft] = kNoBubblePart;
		frameOffsetX = 4;
		frameOffsetY = 7;
		topTailPart = kBubbleTopLeftPoint;
		topTailX = 0;
		break;
	case kDosLayoutVerbBubbleTopLeft:
		// DrawVerbBubble_DispatchByMode palette 2:
		// LayoutVerbBubbleText_Right @ 1000:8cb0 draws the connector
		// sprites around CX/DX, then offsets the actual text frame by
		// +0x10 on X and upward by the row-count-dependent height.
		if (top < height)
			top = 0;
		else
			top -= height;
		left += 0x10;
		if (left + int(widthExtra) + 0x41 >= 320)
			left -= left + int(widthExtra) + 0x41 - 320;
		break;
	case kDosLayoutVerbBubbleBottomLeft:
		// DrawVerbBubble_DispatchByMode palette 4:
		// LayoutVerbBubbleText_Left @ 1000:8d1e draws the connector
		// sprites around CX/DX, then offsets the actual text frame by
		// +0x10/+0x10 before the saved formatted bubble is drawn.
		top += 0x10;
		if (top < 0)
			top = 0;
		left += 0x10;
		if (left + int(widthExtra) + 0x41 >= 320)
			left -= left + int(widthExtra) + 0x41 - 320;
		break;
	}
	debugC(2, kDebugLevelGraphics, "painting speech bubble \"%s\" at (adjusted) %d:%d", debugText, left, top);

	uint8 vertical_tiles = 1;
	const uint16 bubbleHeight = MAX<uint16>(60, rows * kLineHeight + 16);
	if (bubbleHeight > 60)
		vertical_tiles += (bubbleHeight - 60 + 5) / 6;

	const uint16 horizontal_tiles = 1 + widthExtra / 4;

	bubble->create(frameOffsetX + 65 + wadj + 4 * horizontal_tiles,
				   frameOffsetY + 54 + 6 * vertical_tiles);
	// DOS draws bubble frame pieces straight into the video buffer with
	// palette index 0 transparent. The C++ path composes a temporary
	// sprite first, so initialize its untouched pixels to that same
	// transparent index before the final clipped blit.
	bubble->fillRect(Common::Rect(0, 0, bubble->w, bubble->h), 0);

	Common::Point position(frameOffsetX + wadj, frameOffsetY);
	paintSpeechBubbleColumn(bubble_indices[kBubbleTopLeft] == kNoBubblePart ? 0 : bubbles[bubble_indices[kBubbleTopLeft]],
							bubbles[bubble_indices[kBubbleLeft]], position, vertical_tiles, bubble);
	position.x -= wadj;
	paint(bubbles[bubble_indices[kBubbleBottomLeft]], position, bubble, kPaintPositionIsTop);

	position.x += 33 + wadj;
	for (int i = 0; i < horizontal_tiles; i++) {
		position.y = frameOffsetY;
		paintSpeechBubbleColumn(bubbles[bubble_indices[kBubbleTop]], bubbles[bubble_indices[kBubbleFill]], position, vertical_tiles, bubble);
		paint(bubbles[bubble_indices[kBubbleBottom]], position, bubble, kPaintPositionIsTop);
		position.x += 4;
	}

	position.y = frameOffsetY;
	paintSpeechBubbleColumn(bubble_indices[kBubbleTopRight] == kNoBubblePart ? 0 : bubbles[bubble_indices[kBubbleTopRight]],
							bubbles[bubble_indices[kBubbleRight]], position, vertical_tiles, bubble);
	paint(bubbles[bubble_indices[kBubbleBottomRight]], position, bubble, kPaintPositionIsTop);

	if (topTailPart != kNoBubblePart)
		paint(bubbles[topTailPart], Common::Point(topTailX, 0), bubble, kPaintPositionIsTop);

	enum {
		kSpeechTwoLinesShift = 8,
		kSpeechVMargin = 8,
		kSpeechOneLineShift = 0xc,
		kSpeechLeftIndent = 15,
		kSpeechFirstLineExtraIndent = 10
	};

	if (renderText) {
		int shift = 0;
		if (rows == 1)
			shift = kSpeechOneLineShift;
		if (rows == 2)
			shift = kSpeechTwoLinesShift;

		const uint16 roundedWidthExtra = widthExtra & ~uint16(3);
		uint16 currentLeft = uint16(frameOffsetX + kSpeechLeftIndent + kSpeechFirstLineExtraIndent);
		uint16 currentTop = uint16(frameOffsetY + kSpeechVMargin + shift);
		uint16 remainingRows = rows;
		byte currentColour = colour;
		Common::Span<const byte> formatted = Logic::textSpan(metrics.text);
		uint32 textPos = 0;

		bool stopText = false;
		while (textPos < formatted.size() && formatted.getUint8At(textPos) != 0 && !stopText) {
			const byte ch = formatted.getUint8At(textPos++);
			switch (ch) {
			case kStringCenter: {
				if (textPos >= formatted.size()) {
					stopText = true;
					break;
				}
				const byte lineWidth = formatted.getUint8At(textPos++);
				// DOS RenderSpeechBubbleText @ 1000:91c9 consumes the next
				// byte as a precomputed line width and centers within the
				// bubble frame, not within the screen.
				const uint16 centered = uint16(roundedWidthExtra + 0x41 - lineWidth);
				currentLeft = uint16(frameOffsetX + (centered >> 1));
				break;
			}
			case kStringAdvance:
				if (textPos >= formatted.size()) {
					stopText = true;
					break;
				}
				currentLeft += formatted.getUint8At(textPos++);
				break;
			case '\n':
			case '\r':
				if (remainingRows != 0)
					--remainingRows;
				if (remainingRows == 0) {
					stopText = true;
					break;
				}
				currentLeft = uint16(frameOffsetX + kSpeechLeftIndent);
				if (remainingRows == 1)
					currentLeft += kSpeechFirstLineExtraIndent;
				currentTop += kLineHeight;
				break;
			case kStringDefaultColour:
				currentColour = colour;
				break;
			case kStringSetColour:
				if (textPos >= formatted.size()) {
					stopText = true;
					break;
				}
				currentColour = formatted.getUint8At(textPos++);
				break;
			case kStringMenuOption:
				while (textPos < formatted.size() && formatted.getUint8At(textPos) != 0)
					currentLeft += paintChar(currentLeft, currentTop, currentColour, formatted.getUint8At(textPos++), bubble);
				if (textPos >= formatted.size()) {
					stopText = true;
					break;
				}
				++textPos;
				if (textPos + 2 > formatted.size()) {
					stopText = true;
					break;
				}
				textPos += 2;
				break;
			default:
				currentLeft += paintChar(currentLeft, currentTop, currentColour, ch, bubble);
			}
		}
	}

	// DOS RenderSpeechBubble* clamps only the coordinate each orientation
	// explicitly adjusts. It does not move the final frame back onscreen on
	// right/bottom overflow; the draw path clips instead. Keep only the
	// negative-origin guard needed by this Surface blitter.
	if (left < 0)
		left = 0;

	if (top < 0)
		top = 0;

	Common::Rect rect(left - frameOffsetX, top - frameOffsetY,
					  left - frameOffsetX + bubble->w, top - frameOffsetY + bubble->h);
	return rect;
}

Common::Rect Graphics::paintText(uint16 left, uint16 top, byte colour, Common::Span<const byte> string, Surface *dest, uint16 *_lines, uint8 firstLineExtraIndent, int flags) {
	uint16 current_left = left + firstLineExtraIndent;
	uint16 current_top = top;
	uint16 max_left = left;
	byte current_colour = colour;
	uint16 lines = 1;
	GraphicsTextCursor cursor(string, "paintText");

	int opt;
	for (;;) {
		byte ch = 0;
		if (!cursor.readByte(ch))
			break;
		if (ch == 0)
			break;

		switch (ch) {
		case kStringMove:
			if (!cursor.readUint16LE(current_left))
				return Common::Rect(left, top, max_left, current_top + kLineHeight);
			if (!cursor.readUint16LE(current_top))
				return Common::Rect(left, top, max_left, current_top + kLineHeight);
			debugC(3, kDebugLevelGraphics, "string move to %d:%d", current_left, current_top);
			break;
		case kStringSetColour:
			if (!cursor.readByte(current_colour))
				return Common::Rect(left, top, max_left, current_top + kLineHeight);
			break;
		case kStringDefaultColour:
			current_colour = colour;
			break;
		case 0x04:
			break;
		case kStringAdvance:
			if (!cursor.readByte(ch))
				return Common::Rect(left, top, max_left, current_top + kLineHeight);
			current_left += ch;
			break;
		case kStringCenter:
			current_left = (320 - calculateLineWidth(cursor.remaining())) / 2;
			break;
		case '\n':
		case '\r':
			current_left = left;
			current_top += kLineHeight;
			lines++;
			break;
		case kStringMenuOption:
			opt = _mOption++;
			_optionRects[opt] = paintText(current_left, current_top, kOptionColour, cursor.remaining(), dest, 0, 0, flags);
			cursor.skipCString();
			if (!cursor.readUint16LE(_optionValues[opt]))
				return Common::Rect(left, top, max_left, current_top + kLineHeight);
			debugC(2, kDebugLevelGraphics | kDebugLevelScript, "option value %d: 0x%x", opt, _optionValues[opt]);
			break;
		default:
			current_left += paintChar(current_left, current_top, current_colour, ch, dest, flags);
			if (current_left > max_left)
				max_left = current_left;
		}
	}

	if (_lines)
		*_lines = lines;

	return Common::Rect(left, top, max_left, current_top + kLineHeight);
}

Common::Rect Graphics::paintTextOneDirty(uint16 left, uint16 top, byte colour, Common::Span<const byte> string) {
	Common::Rect rect = paintText(left, top, colour, string, _framebuffer.get(), 0, 0, kPaintNoDirty);
	markDirtyRect(rect);
	return rect;
}

void Graphics::clearStatusScreenText() {
	_statusScreenText.clear();
}

void Graphics::rememberStatusScreenText(uint16 left, uint16 top, byte colour, const Common::String &text) {
	for (Common::Array<StatusScreenTextEntry>::iterator it = _statusScreenText.begin(); it != _statusScreenText.end(); ++it) {
		if (it->left == left && it->top == top) {
			it->colour = colour;
			it->text = text;
			markFullRedraw();
			return;
		}
	}

	StatusScreenTextEntry entry;
	entry.left = left;
	entry.top = top;
	entry.colour = colour;
	entry.text = text;
	_statusScreenText.push_back(entry);
	markFullRedraw();
}

void Graphics::paintStatusScreenText() {
	const Logic *logic = _engine ? _engine->logic() : 0;
	if (!logic || !logic->inStatusMode() || _statusScreenText.empty())
		return;

	for (Common::Array<StatusScreenTextEntry>::const_iterator it = _statusScreenText.begin(); it != _statusScreenText.end(); ++it)
		paintText(it->left, it->top, it->colour,
				  Logic::textSpan(it->text),
				  _framebuffer.get(), 0, 0, kPaintNoDirty);
}

void Graphics::paintMotionText(Common::Span<const byte> stream) {
	if (!stream.data() || stream.size() == 0)
		return;

	GraphicsTextCursor cursor(stream, "motion text");
	uint16 currentLeft = 0;
	uint16 currentTop = 0;
	byte currentColour = 0xeb;

	for (;;) {
		byte ch = 0;
		if (!cursor.readByte(ch))
			return;
		if (ch == 0)
			break;

		switch (ch) {
		case '\r':
			currentTop = uint16(currentTop + kLineHeight);
			break;
		case kStringMove:
			if (!cursor.readUint16LE(currentLeft))
				return;
			if (!cursor.readUint16LE(currentTop))
				return;
			break;
		case kStringDefaultColour:
			currentColour = 0xeb;
			break;
		case kStringSetColour:
			if (!cursor.readByte(currentColour))
				return;
			break;
		case kStringCenter: {
			uint16 width = 0;
			Common::Span<const byte> rest = cursor.remaining();
			for (uint32 i = 0; i < rest.size(); ++i) {
				const byte widthCh = rest.getUint8At(i);
				if (widthCh == 0 || widthCh == '\r')
					break;
				width = uint16(width + getGlyphWidth(widthCh));
			}
			currentLeft = uint16(uint16(0x0140 - width) >> 1);
			break;
		}
		case kStringAdvance:
			if (!cursor.readByte(ch))
				return;
			currentLeft = uint16(currentLeft + ch);
			break;
		default:
			currentLeft = uint16(currentLeft + paintChar(currentLeft, currentTop, currentColour, ch, _framebuffer.get()));
			break;
		}
	}
}

uint16 Graphics::plainTextLineWidth(Common::Span<const byte> string) const {
	uint16 total = 0;
	for (uint32 pos = 0; string.data() && pos < string.size(); ++pos) {
		const byte ch = string.getUint8At(pos);
		if (ch == 0)
			break;
		if (ch == 0x04)
			continue;
		total += getGlyphWidth(ch);
	}
	return total;
}

Common::Rect Graphics::paintPlainTextLine(uint16 left, uint16 top, byte colour, Common::Span<const byte> string, bool markDirty) {
	uint16 current_left = left;
	for (uint32 pos = 0; string.data() && pos < string.size(); ++pos) {
		const byte ch = string.getUint8At(pos);
		if (ch == 0)
			break;
		if (ch == 0x04)
			continue;
		current_left += paintChar(current_left, top, colour, ch, _framebuffer.get(),
								  markDirty ? kPaintNormal : kPaintNoDirty);
	}
	return Common::Rect(left, top, current_left, top + kLineHeight);
}

// CP437 high-ASCII -> sequential glyph-code translation for the multilingual
// accented builds. Extracted verbatim from SPANISH/FRENCH/GERMAN/ITALIAN.EXE
// (identical 37-entry table; DOS LookupCharSprite helper @ 1000:c77a). Entry i
// maps its CP437 text byte to glyph code 0x7c + i, which IUC_MAIN.<ext>'s
// charmap (footer +0x48) then resolves to the accent glyph sprite. The 0x41/
// 0x45 ('A'/'E') entries are unreachable via this path (those bytes are < 0x7c
// and render directly) — they are kept so the indices match the DOS table.
static const byte kExtendedLatinChars[] = {
	0x84, 0x89, 0x8b, 0x94, 0x81, 0x8e, 0x45, 0xa0, 0x82, 0xa1,
	0xa2, 0xa3, 0x41, 0x90, 0x85, 0x8a, 0x8d, 0x95, 0x97, 0x41,
	0x45, 0x83, 0x88, 0x8c, 0x93, 0x96, 0x41, 0x45, 0xe1, 0x80,
	0x87, 0xa5, 0xa4, 0xa8, 0xad, 0x99, 0x9a};

static byte translateExtendedLatin(byte ch) {
	for (uint i = 0; i < ARRAYSIZE(kExtendedLatinChars); ++i)
		if (kExtendedLatinChars[i] == ch)
			return byte(0x7c + i); // DOS helper @c77a: BL counts up from 0x7c
	return 0;                      // not in the font -> caller substitutes '?'
}

byte Graphics::clampChar(byte ch) const {
	if (ch == '#')
		return '!';
	if (ch < ' ')
		return '?';
	if (ch <= 0x7b)
		return ch;
	// 0x7c..0xff. DOS LookupCharSprite routes ch > 0x7b through the per-build
	// font table. The accented multilingual builds repurpose 0x7c..0x9e as
	// accent glyphs and map CP437 text bytes onto them; everything not in the
	// table becomes '?'. The English/single-language build has no extended
	// glyphs, so '|','}','~' stay literal and the rest is '?'.
	if (_extendedLatinFont) {
		const byte code = translateExtendedLatin(ch);
		return code ? code : '?';
	}
	if (ch <= '~')
		return ch;
	return '?';
}

uint16 Graphics::calculateLineWidth(Common::Span<const byte> string) const {
	uint16 total = 0;
	for (uint32 pos = 0; string.data() && pos < string.size(); ++pos) {
		const byte ch = string.getUint8At(pos);
		if (ch == 0)
			break;
		if (ch == '\n' || ch == '\r')
			break;
		total += getGlyphWidth(ch);
	}
	return total;
}

uint16 Graphics::getGlyphWidth(byte ch) const {
	return _font ? uint16(_font->getCharWidth(ch)) : 4;
}

/**
 * @returns char width
 */
uint16 Graphics::paintChar(uint16 left, uint16 top, byte colour, byte ch, Surface *dest, int flags) const {
	if (!_font)
		return 4;

	Common::Point pos(left, top);
	if (flags & kPaintCameraRelative) {
		const Logic *logic = _engine ? _engine->logic() : 0;
		if (logic) {
			pos.x = int16(pos.x - logic->cameraX());
			pos.y = int16(pos.y - logic->cameraY());
		}
	}

	Common::Rect bounds = _font->getBoundingBox(ch);
	if (flags & kPaintPositionIsTop)
		pos.y = int16(pos.y + bounds.height());
	bounds.translate(pos.x, pos.y);
	const uint16 width = uint16(_font->getCharWidth(ch));

	if (dest && !bounds.isEmpty()) {
		_font->drawChar(dest, ch, pos.x, pos.y, colour);
		if (dest == _framebuffer.get() && !(flags & kPaintNoDirty)) {
			const int clipHeight = ((flags & kPaintCameraRelative) && dest == _framebuffer.get())
									   ? MIN<int>(dest->h, screenHeight())
									   : dest->h;
			bounds.clip(dest->w, clipHeight);
			if (!bounds.isEmpty())
				markDirtyRect(bounds);
		}
	}

	return width;
}

void Graphics::paint(const Sprite *sprite, Common::Point pos, Surface *dest, int flags) const {
	debugC(4, kDebugLevelGraphics, "painting sprite at %d:%d (+%d:%d) [%dx%d]", pos.x, pos.y, sprite->_hotPoint.x, sprite->_hotPoint.y, sprite->w, sprite->h);

	if (flags & kPaintCameraRelative) {
		const Logic *logic = _engine ? _engine->logic() : 0;
		if (logic) {
			pos.x = int16(pos.x - logic->cameraX());
			pos.y = int16(pos.y - logic->cameraY());
		}
	}

	Common::Rect r(sprite->w, sprite->h);
	r.moveTo(pos);
	if (!(flags & kPaintPositionIsTop))
		r.translate(0, -sprite->h); // this is actually bottom
	if (!(flags & kPaintIgnoreHotPoint))
		r.translate(-sprite->_hotPoint.x, sprite->_hotPoint.y);

	// DOS DrawSprite @ 1000:a27a / ClipSpriteRect @ 1000:a2e2 clips room
	// sprites to g_screen_max_y, not the full 200-line framebuffer. Keep the
	// same active-height clamp for camera-relative draws, and still clip small
	// temporary surfaces against their own dimensions.
	const int clipHeight = ((flags & kPaintCameraRelative) && dest == _framebuffer.get())
							   ? MIN<int>(dest->h, screenHeight())
							   : dest->h;

	// DOS clipping moves both the destination rectangle and the source offset.
	const Common::Rect unclipped = r;
	r.clip(dest->w, clipHeight);
	if (r.isEmpty())
		return;
	const Common::Point srcOffset(r.left - unclipped.left, r.top - unclipped.top);
	debugC(4, kDebugLevelGraphics, "transformed rect: %d:%d %d:%d src %d:%d", r.left, r.top, r.right, r.bottom, srcOffset.x, srcOffset.y);

	if (dest == _framebuffer.get() && !(flags & kPaintNoDirty))
		markDirtyRect(r);
	dest->blit(sprite, r, srcOffset, 0, (flags & kPaintSemiTransparent) ? &_tintedPalette : 0);
}

Common::Rect Graphics::layerScaledSpriteRect(const Sprite *sprite, Common::Point pos, uint16 drawMode, int flags) const {
	if (!sprite)
		return Common::Rect();

	if (flags & kPaintCameraRelative) {
		const Logic *logic = _engine ? _engine->logic() : 0;
		if (logic) {
			pos.x = int16(pos.x - logic->cameraX());
			pos.y = int16(pos.y - logic->cameraY());
		}
	}

	Common::Rect unscaled(sprite->w, sprite->h);
	unscaled.moveTo(pos);
	if (!(flags & kPaintPositionIsTop))
		unscaled.translate(0, -sprite->h);
	if (!(flags & kPaintIgnoreHotPoint))
		unscaled.translate(-sprite->_hotPoint.x, sprite->_hotPoint.y);

	if (drawMode == 0)
		return unscaled;

	const uint pattern = layerScalePattern(drawMode);
	const uint16 scaledWidth = scaledAxisLength(sprite->w, kLayerScaleCols[pattern]);
	const uint16 scaledHeight = scaledAxisLength(sprite->h, kLayerScaleRows[pattern]);
	return Common::Rect(unscaled.left, unscaled.bottom - scaledHeight,
						unscaled.left + scaledWidth, unscaled.bottom);
}

Common::Rect Graphics::paintLayerScaledSprite(const Sprite *sprite, Common::Point pos, uint16 drawMode, Surface *dest, int flags) const {
	if (drawMode == 0) {
		paint(sprite, pos, dest, flags);
		Common::Rect r = layerScaledSpriteRect(sprite, pos, drawMode, flags);
		const int clipHeight = ((flags & kPaintCameraRelative) && dest == _framebuffer.get())
								   ? MIN<int>(dest->h, screenHeight())
								   : dest->h;
		r.clip(dest->w, clipHeight);
		return r;
	}

	debugC(4, kDebugLevelGraphics,
		   "painting DOS layer-scaled sprite at %d:%d (+%d:%d) [%dx%d] mode=0x%04x",
		   pos.x, pos.y, sprite->_hotPoint.x, sprite->_hotPoint.y, sprite->w, sprite->h, drawMode);

	const uint8 bh = uint8(drawMode >> 8);
	const uint pattern = layerScalePattern(drawMode);
	const uint8 recolorOffset = uint8(bh << 4);
	Common::Rect scaled = layerScaledSpriteRect(sprite, pos, drawMode, flags);
	const uint16 scaledWidth = uint16(scaled.width());
	const uint16 scaledHeight = uint16(scaled.height());
	const int clipHeight = ((flags & kPaintCameraRelative) && dest == _framebuffer.get())
							   ? MIN<int>(dest->h, screenHeight())
							   : dest->h;
	Common::Rect clipped = scaled;
	clipped.clip(dest->w, clipHeight);
	if (clipped.isEmpty())
		return clipped;

	if (dest == _framebuffer.get() && !(flags & kPaintNoDirty))
		markDirtyRect(clipped);

	Surface transformed;
	transformed.create(scaledWidth, scaledHeight);
	transformed.fillRect(Common::Rect(0, 0, scaledWidth, scaledHeight), 0);

	int transformedY = scaledHeight - 1;
	for (int srcY = sprite->h - 1, rowPattern = 0; srcY >= 0; --srcY, rowPattern = (rowPattern + 1) & 0x0f) {
		if (!kLayerScaleRows[pattern][rowPattern])
			continue;

		int transformedX = 0;
		const byte *src = sprite->row(srcY);
		byte *dst = transformed.row(transformedY);
		for (int srcX = 0, colPattern = 0; srcX < sprite->w; ++srcX, colPattern = (colPattern + 1) & 0x0f) {
			if (!kLayerScaleCols[pattern][colPattern])
				continue;

			byte color = src[srcX];
			if (color != 0) {
				if (recolorOffset != 0 && (color & 0x0f) != 0) {
					color = uint8(color - recolorOffset);
					if (color < 0xa0)
						color = 0xae;
				}
				dst[transformedX] = color;
			}
			++transformedX;
		}
		--transformedY;
	}

	const Common::Rect transformedClip(clipped.left - scaled.left, clipped.top - scaled.top,
									   clipped.right - scaled.left, clipped.bottom - scaled.top);
	dest->copyRectToSurfaceWithKey(transformed, clipped.left, clipped.top, transformedClip, 0);

	return clipped;
}

Common::Point Graphics::cursorPosition() const {
	return _cursorPosition;
}

void Graphics::setCursorPosition(Common::Point pos) {
	// DOS keeps the live cursor within x=1..318 and y=0..199, then copies it
	// to g_cursor_x_locked/g_cursor_y_locked before script dispatch.
	_cursorPosition.x = CLIP<int16>(pos.x, 1, 318);
	_cursorPosition.y = CLIP<int16>(pos.y, 0, 199);
}

void Graphics::syncCursorVisibility() {
	// DOS never enables the INT 33h hardware cursor. Its pointer is a
	// software sprite drawn by DrawCursorSprite @ 1000:ba8d, so keep the
	// ScummVM host cursor hidden even when room restart resets no-step.
	hideCursor();
}

void Graphics::beginFrame() {
	_dirtyRects.clear();
}

void Graphics::markFullRedraw() const {
	_fullRedrawPending = true;
}

void Graphics::markDirtyRect(Common::Rect r) const {
	r.clip(320, 200);
	if (r.isEmpty() || _fullRedrawPending)
		return;

	// AddDirtyRect @ 1000:b172 stores word-aligned screen offsets: odd
	// left edges move one pixel left and the stored width is rounded up.
	if (r.left & 1)
		--r.left;
	if (r.width() & 1)
		++r.right;
	r.clip(320, 200);
	if (r.isEmpty())
		return;

	for (uint i = 0; i < _dirtyRects.size(); ++i)
		if (_dirtyRects[i] == r)
			return;

	if (_dirtyRects.size() >= 50) {
		if (_engine && _engine->logic())
			_engine->logic()->setPendingError(0x33);
		return;
	}

	_dirtyRects.push_back(r);
}

void Graphics::updateScreen() {
	if (_fullRedrawPending || _willFadein) {
		_system->copyRectToScreen(_framebuffer->pixels(), _framebuffer->pitch, 0, 0, 320, 200);
		_previousDirtyRects.clear();
		_previousDirtyRects.push_back(Common::Rect(0, 0, 320, 200));
		_fullRedrawPending = false;
	} else {
		Common::Array<Common::Rect> rects = _previousDirtyRects;
		for (uint i = 0; i < _dirtyRects.size(); ++i) {
			bool duplicate = false;
			for (uint j = 0; j < rects.size(); ++j) {
				if (rects[j] == _dirtyRects[i]) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate)
				rects.push_back(_dirtyRects[i]);
		}

		for (uint i = 0; i < rects.size(); ++i) {
			const Common::Rect &r = rects[i];
			_system->copyRectToScreen(_framebuffer->pixelAt(r.left, r.top),
									  _framebuffer->pitch, r.left, r.top, r.width(), r.height());
		}
		_previousDirtyRects = _dirtyRects;
	}

	if (_willFadein) {
		debugC(3, kDebugLevelGraphics, "performing palette fade in range %u..%u",
			   _fadeStart, _fadeStart + _fadeCount);
		_willFadein = false;
		fadeIn(0, _fadeStart, _fadeCount);
		_inFade = false;
	}

	_system->updateScreen();
}

void Graphics::showCursor() {
	if (_hostCursorShown)
		return;

	Common::ScopedPtr<Sprite> cursor(_resources->getCursor());
	assert(cursor->pitch == cursor->w);
	::Graphics::CursorManager &m = ::Graphics::CursorManager::instance();
	m.replaceCursor(cursor->pixels(), cursor->w, cursor->h, cursor->_hotPoint.x, cursor->_hotPoint.y, 0);
	m.showMouse(true);
	_hostCursorShown = true;
}

void Graphics::updateModalCursor(uint16 cursorKey, uint16 tableFooterOffset, bool resetSequence) {
	if (!_resources || !_resources->mainDat()) {
		showCursor();
		return;
	}

	if (resetSequence || cursorKey != _modalCursorKey || tableFooterOffset != _modalCursorFooter) {
		_modalCursorKey = cursorKey;
		_modalCursorFooter = tableFooterOffset;
		_modalCursorStepIndex = 0;
		_modalCursorStepPending = false;
	}

	uint16 spriteId = 0xffff;
	uint16 stepIndex = _modalCursorStepIndex;
	bool stepPending = _modalCursorStepPending;
	if (!_resources->mainDat()->nextCursorSprite(cursorKey, stepIndex, stepPending, spriteId, tableFooterOffset)) {
		if (_engine && _engine->logic())
			_engine->logic()->setPendingError(0x26);
		showCursor();
		return;
	}

	_modalCursorStepIndex = stepIndex;
	_modalCursorStepPending = stepPending;

	Common::ScopedPtr<Sprite> cursor(_resources->loadSprite(spriteId));
	assert(cursor->pitch == cursor->w);
	::Graphics::CursorManager &m = ::Graphics::CursorManager::instance();
	m.replaceCursor(cursor->pixels(), cursor->w, cursor->h, cursor->_hotPoint.x, cursor->_hotPoint.y, 0);
	m.showMouse(true);
	_hostCursorShown = true;
}

void Graphics::hideCursor() {
	::Graphics::CursorManager::instance().showMouse(false);
	_hostCursorShown = false;
	_modalCursorKey = 0xffff;
	_modalCursorFooter = 0;
	_modalCursorStepIndex = 0;
	_modalCursorStepPending = false;
}

void Graphics::paintRect(const Common::Rect &r, byte colour) {
	_framebuffer->frameRect(r, colour);
	markDirtyRect(r);
}

void Graphics::push(Paintable *p) {
	debugC(3, kDebugLevelGraphics, "pushing to paintables");
	_paintables.push_back(p);
}

void Graphics::pop(Paintable *p) {
	debugC(3, kDebugLevelGraphics, "popping from paintables");
	_paintables.remove(p);
}

void Graphics::hookAfterRepaint(CodePointer &p) {
	_afterRepaintHooks.push_back(p);
}

void Graphics::clearPalette(int offset, int count) {
	byte pal[0x300];
	Common::fill(pal, pal + 0x300, 0);
	storePaletteTarget(pal, offset, count);
	_system->getPaletteManager()->setPalette(pal, offset, count);
}

void Graphics::setPalette(const byte *colours, uint start, uint num) {
	storePaletteTarget(colours, start, num);

	if (!_willFadein)
		_system->getPaletteManager()->setPalette(colours, start, num);

	updateTintedPalette();
}

void Graphics::storePaletteTarget(const byte *colours, uint start, uint num) {
	if (!colours || start >= 256)
		return;

	num = MIN<uint>(num, 256 - start);
	memcpy(_roomPalette + start * 3, colours, num * 3);
}

void Graphics::updateTintedPalette() {
	// DOS BuildDitherTable @ 1000:177a maps each palette entry to the
	// nearest luma among interface/bright palette entries
	// 0xae/0xaf, 0xbe/0xbf, 0xce/0xcf, 0xde/0xdf.
	for (int i = 0; i < 256; ++i) {
		const byte *colour = _roomPalette + i * 3;
		const byte luma = (30 * colour[0] + 60 * colour[1] + 10 * colour[2]) / 100;

		byte curr = 174;
		byte best_diff = 255;
		byte best_color = curr;

		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 2; k++) {
				int16 diff = _roomPalette[(curr + k) * 3] - luma;
				if (diff < 0)
					diff = -diff;
				if (diff < best_diff) {
					best_diff = diff;
					best_color = curr + k;
				}
			}
			curr += 16;
		}

		_tintedPalette[i] = best_color;
	}
}

void Graphics::goFullscreen() {
	setFullscreen(true);
}

void Graphics::setFullscreen(bool enabled) {
	if (_fullscreen != enabled)
		markFullRedraw();
	_fullscreen = enabled;
}

uint16 Graphics::screenHeight() const {
	return _fullscreen ? 200 : 152;
}

uint16 Graphics::backdropWidth() const {
	return _backdrop.get() ? uint16(_backdrop->w) : 320;
}

uint16 Graphics::backdropHeight() const {
	return _backdrop.get() ? uint16(_backdrop->h) : screenHeight();
}

void Graphics::applyRoomChangeWipe() {
	if (!_framebuffer || !_system || _inFade)
		return;

	debugC(2, kDebugLevelGraphics, "performing DOS room-change screen wipe");
	const int viewHeight = screenHeight();
	const Common::Rect view(0, 0, 320, viewHeight);

	// FadeWipeBlackTopDown @ 1000:b0b0 clears one x+y diagonal per pass
	// directly in video memory, for 0x208 passes. The visible height is the
	// current backdrop dimension (0x98 normally, 0xc8 in fullscreen modes).
	for (int diag = 0; diag < 0x208; ++diag) {
		for (int y = 0; y < viewHeight; ++y) {
			const int x = diag - y;
			if (x >= 0 && x < 320)
				*_framebuffer->pixelAt(x, y) = 0;
		}

		if ((diag & 3) == 0) {
			_system->copyRectToScreen(_framebuffer->pixelAt(0, 0),
									  _framebuffer->pitch, 0, 0, 320, viewHeight);
			_system->updateScreen();
			Eng.delay(1);
		}
	}

	// ApplyChangeRoomTransition then calls ClearVideoAndPushToScreen, which
	// leaves the visible playfield black until the restart-room path paints
	// the newly loaded backdrop.
	_framebuffer->fillRect(view, 0);
	_system->copyRectToScreen(_framebuffer->pixelAt(0, 0),
							  _framebuffer->pitch, 0, 0, 320, viewHeight);
	_system->updateScreen();
	markFullRedraw();
}

void Graphics::clearFramebuffer(byte colour) {
	if (_framebuffer) {
		_framebuffer->fillRect(Common::Rect(0, 0, 320, 200), colour);
		markFullRedraw();
	}
}

void Graphics::clearPaletteRange(int start, int count, bool fade) {
	clearPalette(start, count);
	if (fade) {
		_willFadein = true;
		_fadeStart = uint(start);
		_fadeCount = uint(count);
	}
}

struct Tr {
	byte operator()(const byte &b) const { return 0xff & ((b << 1) - 63); }
};

void Graphics::fadeIn(const byte *colours, uint start, uint num) {
	byte buf[0x300];
	if (!colours) {
		memcpy(buf, _roomPalette + start * 3, num * 3);
		colours = buf;
	}

	const int bytes = num * 3;
	byte current[0x300];

	Common::fill(current, current + bytes, 0);

	byte off = 255;
	for (int j = 0; j < 63; j++) {
		off -= 4;
		for (int i = 0; i < bytes; i++)
			current[i] = colours[i] - MIN(off, colours[i]);

		_system->getPaletteManager()->setPalette((current), start, num);
		_system->updateScreen();
		Eng.delay(1000 / 25);

		if (Log.canSkipCutscene() && Eng.escapePressed()) {
			Log.requestSkipCutscene();
			_system->getPaletteManager()->setPalette(colours, start, num);
			storePaletteTarget(colours, start, num);
			updateTintedPalette();
			_system->updateScreen();
			return;
		}
	}

	_system->getPaletteManager()->setPalette(colours, start, num);
	storePaletteTarget(colours, start, num);
	updateTintedPalette();
}

bool Graphics::fadeOut(FadeFlags f) {
	int bytes = 0x300;
	int offset = 0;
	int colours = 256;
	byte current[0x300];

	if (f == kPartialFade) {
		bytes = 96 * 3;
		offset = 160;
		colours = 96;
	}

	memcpy(current, _roomPalette + offset * 3, bytes);

	for (int j = 0; j < 63; j++) {
		for (int i = 0; i < bytes; i++)
			current[i] -= MIN<byte>(4, current[i]);

		_system->getPaletteManager()->setPalette((current), offset, colours);
		storePaletteTarget(current, offset, colours);
		_system->updateScreen();
		Eng.delay(1000 / 25);

		if (Log.canSkipCutscene() && Eng.escapePressed()) {
			Log.requestSkipCutscene();
			updateTintedPalette();
			return false;
		}
	}
	updateTintedPalette();
	return true;
}

void Graphics::say(Common::Span<const byte> text, uint16 frames) {
	// Default narrator position/color = top-left, color 235. Op_47/0x48
	// use sayAt() instead.
	sayAt(text, frames, 0, 0, 235);
}

static Common::Array<Common::String> paginateSpeechText(Common::Span<const byte> text, uint16 maxLines) {
	Common::Array<Common::String> pages;
	Common::String full;
	for (uint32 i = 0; text.data() && i < text.size(); ++i) {
		const byte ch = text.getUint8At(i);
		if (ch == 0)
			break;
		full += char(ch);
	}
	if (maxLines == 0) {
		pages.push_back(full);
		return pages;
	}

	Common::String page;
	uint16 completedLines = 0;
	for (uint i = 0; i < full.size(); ++i) {
		const char ch = full[i];
		if (ch == '\n' || ch == '\r') {
			if (completedLines + 1 >= maxLines) {
				pages.push_back(page);
				page.clear();
				completedLines = 0;
			} else {
				page += '\n';
				++completedLines;
			}
			continue;
		}
		page += ch;
	}

	if (!page.empty())
		pages.push_back(page);
	if (pages.empty())
		pages.push_back(full);
	return pages;
}

void Graphics::sayAt(Common::Span<const byte> text, uint16 frames,
					 uint16 x, uint16 y, byte color, uint16 maxLines,
					 SpeechBubbleMode bubbleMode, bool forceBubble) {
	Common::Array<Common::String> pages = paginateSpeechText(text, maxLines);
	bool started = false;
	for (uint i = 0; i < pages.size(); ++i) {
		SpeechEntry e;
		e.text = pages[i];
		e.length = uint16(pages[i].size());
		e.frames = frames;
		e.x = x;
		e.y = y;
		e.color = color;
		e.maxLines = maxLines;
		e.bubble = forceBubble || maxLines != 0 || x != 0 || y != 0;
		e.bubbleMode = bubbleMode;

		if (_speechActive || i != 0) {
			// Queue this utterance to play after the current one (and any
			// already-queued ones) finish.
			_speechQueue.push(e);
			debugC(2, kDebugLevelGraphics, "queued speech at %u,%u (queue size %u)",
				   x, y, (uint)_speechQueue.size());
			continue;
		}

		_speechText = e.text;
		_speechActive = true;
		_speechFramesLeft = e.frames;
		_speechX = e.x;
		_speechY = e.y;
		_speechColor = e.color;
		_speechMaxLines = e.maxLines;
		_speechBubble = e.bubble;
		_speechBubbleMode = e.bubbleMode;
		started = true;
	}

	if (started)
		paintSpeech();
}

void Graphics::clearSpeech() {
	_speechText.clear();
	_speechActive = false;
	_speechFramesLeft = 0;
	_speechX = 0;
	_speechY = 0;
	_speechColor = 235;
	_speechMaxLines = 0;
	_speechBubble = false;
	_speechBubbleMode = kSpeechBubbleType1;
	_speechDoneCallback.reset();
	_speechDoneCallbackMode = 0;
	_speechDoneCallbackHasMode = false;
	_speechQueue.clear();
}

void Graphics::runWhenSaid(const CodePointer &cb) {
	// Bind the callback to the most-recently-queued utterance (or to the
	// current one if the queue is empty). DOS pattern is: emit speech →
	// emit "wait until said" → callback fires when THAT speech finishes.
	if (!_speechQueue.empty()) {
		const uint16 mode = Log.opcodeMode();
		// Patch the back of the queue. Common::Queue doesn't expose
		// random access, so drain-and-rebuild — cheap given queue is
		// almost always 1..3 entries.
		Common::Queue<SpeechEntry> tmp;
		while (_speechQueue.size() > 1)
			tmp.push(_speechQueue.pop());
		SpeechEntry last = _speechQueue.pop();
		if (!last.cb.isEmpty()) {
			// Multiple runWhenSaid for the same entry — chain by
			// running the old callback first via runLater.
			if (last.cbHasMode)
				Log.runLaterWithMode(last.cb, last.cbMode);
			else
				Log.runLater(last.cb);
		}
		last.cb = cb;
		last.cbMode = mode;
		last.cbHasMode = true;
		while (!tmp.empty())
			_speechQueue.push(tmp.pop());
		_speechQueue.push(last);
		return;
	}

	if (!_speechDoneCallback.isEmpty()) {
		// Active speech already has a callback bound — chain via runLater
		// so both fire (older one first, then this one as part of the
		// post-speech callback chain).
		if (_speechDoneCallbackHasMode)
			Log.runLaterWithMode(_speechDoneCallback, _speechDoneCallbackMode);
		else
			Log.runLater(_speechDoneCallback);
	}
	_speechDoneCallback = cb;
	_speechDoneCallbackMode = Log.opcodeMode();
	_speechDoneCallbackHasMode = true;
}

} // End of namespace Interspective
