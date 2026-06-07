/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#ifndef INTERSPECTIVE_GRAPHICS_H
#define INTERSPECTIVE_GRAPHICS_H

#include "common/array.h"
#include "common/list.h"
#include "common/ptr.h"
#include "common/queue.h"
#include "common/rect.h"
#include "common/singleton.h"
#include "common/str.h"

#include "interspective/types.h"
#include "interspective/value.h"

class OSystem;

namespace Interspective {

class Engine;
class Resources;
class Surface;
class Sprite;

class Graphics : public Common::Singleton<Graphics> {
public:
	Graphics() : _cursorPosition(160, 100), _hostCursorShown(false),
				 _roomCloseUpActive(false), _roomCloseUpPoint(0, 0),
				 _inventoryCloseUpObjectId(0),
				 _conversationPaletteRestorePending(false) {}

	void setEngine(Engine *engine);

	/**
	 * Load interface image and palette; sets the palette.
	 */
	void loadInterface();

	void init();
	void paint();

	/**
	 * Paint the interface to proper portion of the screen.
	 */
	void paintInterface();
	void paintCursorSprite();
	void paintAnimations();
	void paintExits();
	void paintSpeech();
	void prepareInterfacePalette();

	void push(Paintable *p);
	void pop(Paintable *p);
	void hookAfterRepaint(CodePointer &p);

	void setBackdrop(uint16 id);
	void loadGraphicPalette(uint16 id);
	void paintBackdrop();
	void clearBackdropLikeDos(byte colour = 0);

	enum FadeFlags {
		kFullFade = 0,
		kPartialFade = 1
	};
	void applyRoomChangeWipeLikeDos();
	void willFadein(FadeFlags f = kFullFade);
	bool fadeOut(FadeFlags f = kFullFade);
	bool inFade() const { return _inFade; }
	bool palettePendingLikeDos() const { return _willFadein || _inFade; }
	void setInFade(bool v) { _inFade = v; }

	void say(const byte *text, uint16, uint16 frames = 50);
	// DOS Op_47/0x48: narrator bubble with explicit position + color.
	// x/y in DOS units (Op_45..0x48 resolve arg0 into CX/x, arg1 into DX/y).
	// color = 0xeb is the canonical DOS narrator color; 0xae is shadow.
	enum SpeechBubbleMode {
		kSpeechBubbleAuto = 0,          // actor-targeted slot: dispatch by screen quadrant
		kSpeechBubbleType1 = 1,         // DOS RenderSpeechBubble / RenderSpeechBubbleTopLeft
		kSpeechBubbleType2 = 2,         // DOS RenderSpeechBubbleBottomRight / RenderSpeechBubbleTopRight
		kSpeechBubbleVerbTopLeft = 3,   // DOS ComputeBubbleRect_TopLeft + raw row layout
		kSpeechBubbleVerbBottomLeft = 4 // DOS ComputeBubbleRect_BottomLeft + raw row layout
	};
	void sayAt(const byte *text, uint16 length, uint16 frames,
			   uint16 x, uint16 y, byte color, uint16 maxLines = 0,
			   SpeechBubbleMode bubbleMode = kSpeechBubbleType1,
			   bool forceBubble = false);
	bool isSaying() const { return _speech != 0 || !_speechQueue.empty(); }
	void clearSpeech();
	void runWhenSaid(const CodePointer &p);

	uint16 ask(uint16 left, uint16 top, byte width, byte height, byte *string, uint16 *selectedIndex = 0);
	uint16 askVerbBubbleLikeDos(byte paletteMode, byte *string, uint16 *selectedIndex = 0);
	uint16 askVerbBubbleTextLikeDos(byte paletteMode, const byte *string, uint16 *selectedIndex = 0);
	void showVerbBubbleTextLikeDos(byte paletteMode, const byte *string, uint16 frames);
	Common::Rect paintText(uint16 left, uint16 top, byte colour, const byte *string) {
		return paintText(left, top, colour, string, _framebuffer.get(), 0, 0, 0);
	}

	Common::Rect textMetrics(const byte *string, uint16 *lines = 0, uint16 left = 0, uint16 top = 0) {
		return paintText(left, top, 235, string, 0, lines, 0, 0);
	}
	Common::Rect paintText(uint16 left, uint16 top, byte colour, const byte *string, Surface *s, uint16 *lines = 0, uint8 firstLineExtraIndent = 0, int flags = 0);
	Common::Rect paintTextOneDirtyLikeDos(uint16 left, uint16 top, byte colour, const byte *string);
	void clearStatusScreenTextLikeDos();
	void rememberStatusScreenTextLikeDos(uint16 left, uint16 top, byte colour, const Common::String &text);
	void paintMotionText(const byte *stream, uint16 length);
	uint16 plainTextLineWidth(const byte *string) const;
	Common::Rect paintPlainTextLine(uint16 left, uint16 top, byte colour, const byte *string, bool markDirty = true);
	bool setStatusOverlayTextLikeDos(const byte *text);
	void setInterfaceOverlaySprite(uint16 maskBit, uint16 spriteId, uint16 x, uint16 y);
	void setRoomCloseUpLikeDos(Common::Point point);
	void clearRoomCloseUpLikeDos();
	void setInventoryCloseUpObjectLikeDos(uint16 objectId);
	void clearInventoryCloseUpObjectLikeDos();

	void paintSpeechBubbleColumn(Sprite *top, Sprite *fill, Common::Point &point, uint8 fill_tiles, Surface *dest);
	Common::Rect paintSpeechInBubble(Common::Point pos, byte colour, const byte *string, Surface *dest,
									 SpeechBubbleMode mode = kSpeechBubbleAuto, bool renderText = true,
									 uint16 forcedRows = 0);

	void paintRect(const Common::Rect &r, byte colour = 235);

	enum PaintFlags {
		kPaintNormal = 0,
		kPaintPositionIsTop = 1,
		kPaintSemiTransparent = 2,
		kPaintCameraRelative = 4,
		kPaintNoDirty = 8,
		kPaintIgnoreHotPoint = 16
	};
	void paint(const Sprite *sprite, Common::Point pos, int flags = kPaintNormal) const {
		paint(sprite, pos, _framebuffer.get(), flags);
	}
	void paint(const Sprite *sprite, uint16 left, uint16 top, Surface *dest, int flags = kPaintNormal) const {
		paint(sprite, Common::Point(left, top), dest, flags);
	}
	void paint(const Sprite *sprite, Common::Point pos, Surface *s, int flags = kPaintNormal) const;

	Common::Point cursorPosition() const;
	void setCursorPosition(Common::Point pos);
	void syncCursorVisibility();
	void showCursor();
	void hideCursor();

	void updateScreen();
	void markDirtyRect(Common::Rect r) const;
	void setPalette(const byte *colours, uint start, uint num);

	/** Go fullscreen. This will hide the interface. */
	void goFullscreen();
	void setFullscreen(bool enabled);
	uint16 screenHeight() const;
	uint16 backdropWidth() const;
	uint16 backdropHeight() const;
	void clearFramebuffer(byte colour = 0);
	void clearPaletteRange(int start, int count, bool fade = true);

	// Per-glyph width (variable-width font). Mirrors DOS
	// LookupCharSprite @ 1000:c69c — used by Logic::formatBubbleText
	// for DOS-faithful pixel width accumulation.
	uint16 getGlyphWidth(byte ch) const;
	enum {
		kLineHeight = 12
	};

private:
	byte clampChar(byte ch) const;
	uint16 calculateLineWidth(const byte *string) const;
	Sprite *getGlyph(byte ch) const;
	void prepareConversationPaletteLikeDos();
	void beginConversationModalLikeDos();
	void finishConversationModalLikeDos();
	void restoreConversationPaletteLikeDos();
	void paintConversationBackdropLikeDos();
	void paintInterfaceMinimapLikeDos();
	void paintRoomCloseUpLikeDos();
	void paintInventoryObjectsLikeDos();
	void paintInventoryCloseUpLikeDos();
	void paintInterfaceOverlaySprites();
	void paintAutoCloseTimerLikeDos();
	void paintStatusOverlayText();
	void paintStatusScreenTextLikeDos();

	/**
	 * paint a character on screen
	 * @returns char width
	 */
	uint16 paintChar(uint16 left, uint16 top, byte colour, byte character, Surface *s, int flags = kPaintNormal) const;
	Surface *_interface;
	Engine *_engine;
	Resources *_resources;
	OSystem *_system;
	Common::Point _cursorPosition;
	bool _hostCursorShown;
	Common::SharedPtr<Surface> _backdrop, _framebuffer;

	static const char _charwidths[];

private:
	void clearPalette(int start = 0, int count = 256);
	void fadeIn(const byte *colours = 0, uint start = 0, uint num = 256);
	void updateTintedPalette();
	void storePaletteTarget(const byte *colours, uint start, uint num);
	void beginFrame();
	void markFullRedraw() const;

	Common::List<Paintable *> _paintables;
	Common::List<CodePointer> _afterRepaintHooks;

	bool _willFadein;
	bool _inFade;
	FadeFlags _fadeFlags;
	uint _fadeStart;
	uint _fadeCount;
	byte _roomPalette[0x300];
	byte _interfacePalette[0x300];
	byte _tintedPalette[256];
	struct InterfaceOverlaySprite {
		InterfaceOverlaySprite() : maskBit(0), spriteId(0xffff), x(0), y(0) {}
		uint16 maskBit;
		uint16 spriteId;
		uint16 x;
		uint16 y;
	};
	Common::Array<InterfaceOverlaySprite> _interfaceOverlaySprites;
	Common::Array<Common::String> _statusOverlayLines;
	struct StatusScreenTextEntry {
		StatusScreenTextEntry() : left(0), top(0), colour(0) {}
		uint16 left;
		uint16 top;
		byte colour;
		Common::String text;
	};
	Common::Array<StatusScreenTextEntry> _statusScreenText;
	bool _roomCloseUpActive;
	Common::Point _roomCloseUpPoint;
	uint16 _inventoryCloseUpObjectId;
	bool _conversationPaletteRestorePending;
	byte _conversationSavedPalette[0x300];
	// True for the multilingual accented builds (Spanish/French/German/Italian),
	// whose font repurposes glyph codes 0x7c..0x9e for accents and whose text is
	// CP437-encoded — see clampChar / translateExtendedLatin.
	bool _extendedLatinFont;

	// Active speech bubble. _speech is the text being painted; when
	// _speechFramesLeft hits 0 the painter invokes _speechDoneCallback (if
	// set), then pops the next entry from _speechQueue. The queue is FIFO.
	byte *_speech;
	uint16 _speechFramesLeft;
	uint16 _speechX, _speechY; // current narrator-bubble position
	byte _speechColor;         // current narrator-bubble color
	uint16 _speechMaxLines;
	bool _speechBubble;
	SpeechBubbleMode _speechBubbleMode;
	CodePointer _speechDoneCallback;
	uint16 _speechDoneCallbackMode;
	bool _speechDoneCallbackHasMode;
	struct SpeechEntry {
		SpeechEntry() : text(0), length(0), x(0), y(0), frames(0), color(235), maxLines(0),
						bubble(false), bubbleMode(kSpeechBubbleType1),
						cbMode(0), cbHasMode(false) {}
		byte *text; // owned: caller transferred via say()
		uint16 length;
		uint16 x, y; // top-left coords (Op_47/0x48 narrator pos)
		uint16 frames;
		byte color; // text color (Op_47/0x48 color arg)
		uint16 maxLines;
		bool bubble; // explicit speech-slot bubble vs raw subtitle text
		SpeechBubbleMode bubbleMode;
		CodePointer cb;
		uint16 cbMode;
		bool cbHasMode;
	};
	Common::Queue<SpeechEntry> _speechQueue;
	bool _fullscreen;
	mutable Common::Array<Common::Rect> _dirtyRects;
	mutable Common::Array<Common::Rect> _previousDirtyRects;
	mutable bool _fullRedrawPending;
};

#define Graf Graphics::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_GRAPHICS_H
