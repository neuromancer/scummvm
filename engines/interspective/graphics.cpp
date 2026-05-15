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

#include "interspective/graphics.h"

#include "graphics/palette.h"
#include "graphics/paletteman.h"

#include "common/array.h"
#include "common/events.h"
#include "common/system.h"
#include "common/util.h"
#include "graphics/cursorman.h"

#include "interspective/animation.h"
#include "interspective/actor.h"
#include "interspective/debug.h"
#include "interspective/debugger.h"
#include "interspective/exit.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

using namespace std;

namespace Common {
	DECLARE_SINGLETON(Interspective::Graphics);
}

namespace Interspective {

void Graphics::setEngine(Engine *engine) {
	_engine = engine;
	_framebuffer = Common::SharedPtr<Surface>(new Surface);
	_framebuffer.get()->create(320, 200);
	_willFadein = false;
	_inFade = false;

	_speech = 0;
	_speechFramesLeft = 0;
	_speechX = 0;
	_speechY = 0;
	_speechColor = 235;
	_speechBubble = false;
	_speechBubbleMode = kSpeechBubbleType1;
	_fullscreen = false;
}

void Graphics::init() {
	_resources = _engine->resources();
	_system = _engine->_system;
	loadInterface();
}

void Graphics::paint() {
	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, ">>>start paint procedure");

	if (_engine->logic()->roomChangePending()) {
		debugC(2, kDebugLevelFlow | kDebugLevelGraphics, "skipping paint while room restart is pending");
		return;
	}

	paintBackdrop();
	paintAnimations();
	paintInterface();
	paintSpeech();
	_engine->logic()->paintMotionText();

	debugC(3, kDebugLevelGraphics, "painting paintables");
	foreach (Paintable *, _paintables)
		(*it)->paint(this);

	unless (_afterRepaintHooks.empty()) {
		debugC(3, kDebugLevelGraphics | kDebugLevelScript, "running hooks");
		foreach (CodePointer, _afterRepaintHooks)
			it->run();
		_afterRepaintHooks.clear();
	}

	paintCursorSprite();

	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, "<<<end paint procedure");
}

void Graphics::paintExits() {
	debugC(3, kDebugLevelFlow | kDebugLevelGraphics, "painting exits");
	foreach_const (Exit *, _engine->logic()->room()->exits())
		(*it)->paint(this);
}

void Graphics::loadInterface() {
	debugC(1, kDebugLevelGraphics, "loading interface");
	_interface = new Surface;
	_interface->create(320, 50);
	_resources->loadInterfaceImage(reinterpret_cast<byte *>(_interface->getPixels()), _interfacePalette);
}

void Graphics::prepareInterfacePalette() {
	debugC(1, kDebugLevelGraphics, "preparing interface palette");
	_engine->_system->getPaletteManager()->setPalette(_interfacePalette + 160 * 3, 160, 96);
}

void Graphics::paintInterface() {
	if (_fullscreen) return;
	debugC(3, kDebugLevelGraphics, "painting interface");
	_framebuffer->blit(_interface, Common::Rect(0, 152, 320, 200), 0);
}

void Graphics::paintCursorSprite() {
	Logic *logic = _engine->logic();
	if (!logic || logic->cursorMode() != 0x20 || logic->dragTarget() == 0)
		return;

	const uint16 id = logic->dragTarget();
	if (_resources->mainDat() && id > _resources->mainDat()->personsCount())
		return;

	Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(id));
	paint(sprite.get(), cursorPosition(), kPaintPositionIsTop);
}

void Graphics::setBackdrop(uint16 id) {
	byte palette[0x300];
	_backdrop = Common::SharedPtr<Surface>(_resources->loadBackdrop(id, palette));
	setPalette(palette, 0, 256);
	prepareInterfacePalette();
	paintBackdrop();
}

void Graphics::loadGraphicPalette(uint16 id) {
	byte palette[0x300];
	byte *scratch = new byte[320 * 200];
	_resources->loadImage(id, scratch, 320 * 200, palette);
	delete[] scratch;
	setPalette(palette, 0, 256);
}

void Graphics::willFadein(FadeFlags f) {
	_willFadein = true;
	_inFade = true;
	_fadeFlags = f;
	if (f & kPartialFade)
		clearPalette(160, 96);
	else
		clearPalette();
}

void Graphics::paintBackdrop() {
	// TODO cropping
	debugC(3, kDebugLevelGraphics, "painting backdrop");
	_framebuffer->blit(_backdrop.get());
}

void Graphics::paintSpeech() {
	if (_speech) {
		if (!_speechFramesLeft) {
			delete[] _speech;
			_speech = 0;
			CodePointer cb = _speechDoneCallback;
			_speechDoneCallback.reset();
			cb.run();

			// Pop the next queued utterance (if any) and start painting it.
			// Run cb FIRST so any side-effects of the previous speech complete
			// before the new one starts (matches DOS behaviour).
			if (!_speechQueue.empty()) {
				SpeechEntry next = _speechQueue.pop();
				_speech = next.text;
				_speechFramesLeft = next.frames;
				_speechX = next.x;
				_speechY = next.y;
				_speechColor = next.color;
				_speechBubble = next.bubble;
				_speechBubbleMode = next.bubbleMode;
				_speechDoneCallback = next.cb;
				if (_speechBubble) {
					Sprite bubble;
					bubble._hotPoint = Common::Point(0, 0);
					Common::Rect rect = paintSpeechInBubble(Common::Point(_speechX, _speechY), _speechColor, _speech, &bubble, _speechBubbleMode);
					paint(&bubble, Common::Point(rect.left, rect.top), kPaintSemiTransparent | kPaintPositionIsTop);
				} else {
					paintText(_speechX, _speechY, _speechColor, _speech);
				}
			}
		} else {
			if (_speechBubble) {
				Sprite bubble;
				bubble._hotPoint = Common::Point(0, 0);
				Common::Rect rect = paintSpeechInBubble(Common::Point(_speechX, _speechY), _speechColor, _speech, &bubble, _speechBubbleMode);
				paint(&bubble, Common::Point(rect.left, rect.top), kPaintSemiTransparent | kPaintPositionIsTop);
			} else {
				paintText(_speechX, _speechY, _speechColor, _speech);
			}
			_speechFramesLeft--;
		}
	}

	Common::List<Animation *> animations = _engine->logic()->animations();
	for (Common::List<Animation *>::iterator it = animations.begin(); it != animations.end(); ++it) {
		if ((*it)->isActor())
			static_cast<Actor *>(*it)->paintSpeech(this);
	}
}

void Graphics::paintAnimations() {
	debugC(3, kDebugLevelGraphics, "painting animations");
	struct RenderEntry {
		RenderEntry() : key(0), animation(0), exitObj(0) {}
		RenderEntry(int k, Animation *a, Exit *e) : key(k), animation(a), exitObj(e) {}
		int key;
		Animation *animation;
		Exit *exitObj;
	};

	Common::Array<RenderEntry> sorted;
	if (_engine->logic()->room()) {
		foreach_const(Exit *, _engine->logic()->room()->exits())
			sorted.push_back(RenderEntry((*it)->zIndex() == 0xff ? -1 : (*it)->zIndex(), 0, *it));
	}

	Common::List<Animation *> animations = _engine->logic()->animations();
	for (Common::List<Animation *>::iterator it = animations.begin(); it != animations.end(); ++it) {
		const uint8 layer = uint8((*it)->zIndex());
		sorted.push_back(RenderEntry(layer == 0xff ? -1 : layer, *it, 0));
	}

	// DOS DrawAllRoomObjects renders layer 0x0b down to 0x00, then 0xff.
	// Later draws appear on top, so sort descending by layer key. Exits are
	// inserted before actors to match the per-layer object pass before
	// CollectActorAnimSlots.
	for (uint i = 1; i < sorted.size(); ++i) {
		RenderEntry entry = sorted[i];
		uint j = i;
		while (j > 0 && sorted[j - 1].key < entry.key) {
			sorted[j] = sorted[j - 1];
			--j;
		}
		sorted[j] = entry;
	}

	for (uint i = 0; i < sorted.size(); ++i) {
		if (sorted[i].exitObj)
			sorted[i].exitObj->paint(this);
		else
			sorted[i].animation->paint(this);
	}
}

// it's modal anyway
static int _mOption = 0;
static Common::Rect _optionRects[10];
static uint16 _optionValues[10];

uint16 Graphics::ask(uint16 left, uint16 top, byte width, byte height, byte *string) {
	width += 2;
	height += 2;
	enum {
		kFrameTileHeight = 12,
		kFrameTileWidth = 16
	};

	Surface frame;
	frame.create(width * kFrameTileWidth, height * kFrameTileHeight+4);

	Sprite **frames = _resources->frames();

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

	_system->copyRectToScreen(reinterpret_cast<byte *>(frame.getPixels()), frame.pitch, left, top, width * kFrameTileWidth, height * kFrameTileHeight+4);

	bool show = true;
	while (show) {
		_system->updateScreen();
		_engine->debugger()->onFrame();
		Common::Event event;
		while (_engine->eventMan()->pollEvent(event)) {
			switch(event.type) {
			case Common::EVENT_LBUTTONUP:
				if (_mOption == 0)
					return 0xffff;
				else
					for (int i = 0; i < _mOption; i++) {
						Common::Point p = event.mouse;
						p.x -= left;
						p.y -= top;
						if (_optionRects[i].contains(p))
							return _optionValues[i];
					}
			default:
				break;
			}
		}
		_system->delayMillis(1000/60);
	}

	return 0xffff;
}

enum {
	kOptionColour = 254,
	kSelectedOptionColour = 227
};

void Graphics::paintSpeechBubbleColumn(Sprite *top, Sprite *fill, Common::Point &point, uint8 fill_tiles, Surface *dest) {
	paint(top, point, dest, kPaintPositionIsTop);
	point.y += 24;
	for (int i = 0; i < fill_tiles; i++) {
		paint(fill, point, dest, kPaintPositionIsTop);
		point.y += 6;
	}
}

Common::Rect Graphics::paintSpeechInBubble(Common::Point pos, byte colour, const byte *string, Surface *bubble,
                                           SpeechBubbleMode mode) {
	int left = pos.x, top = pos.y;
	debugC(1, kDebugLevelGraphics, "painting speech bubble \"%s\" at %d:%d", string, left, top);

	Common::String normalized;
	for (const byte *p = string; p && *p; ++p)
		normalized += char(*p == '\n' ? '\r' : *p);
	Logic::FormattedBubble metrics = Log.formatBubbleText(reinterpret_cast<const byte *>(normalized.c_str()));
	const uint16 rows = MAX<uint16>(1, metrics.rowCount);
	const uint16 widthExtra = metrics.maxLineWidth;

	Sprite * const *bubbles = _resources->bubbles();

	uint8 bubble_indices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

	uint8 wadj = 0;
	const int height = (rows >= 3) ? (rows * kLineHeight + 16) : 60;

	enum DosBubbleVariant {
		kDosRenderSpeechBubble,
		kDosRenderSpeechBubbleTopLeft,
		kDosRenderSpeechBubbleBottomRight,
		kDosRenderSpeechBubbleTopRight
	};

	DosBubbleVariant variant;
	if (mode == kSpeechBubbleType1) {
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
		bubble_indices[kBubbleTopRight] = kBubbleTopRightPoint;
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
		bubble_indices[kBubbleTopLeft] = kBubbleTopLeftPoint;
		break;
	}
	debugC(2, kDebugLevelGraphics, "painting speech bubble \"%s\" at (adjusted) %d:%d", string, left, top);

	uint8 vertical_tiles = 1;
	const uint16 bubbleHeight = MAX<uint16>(60, rows * kLineHeight + 16);
	if (bubbleHeight > 60)
		vertical_tiles += (bubbleHeight - 60 + 5) / 6;

	const uint16 horizontal_tiles = 1 + widthExtra / 4;

	bubble->create(65 + wadj + 4 * horizontal_tiles, 54 + 6 * vertical_tiles);

	Common::Point position(wadj, 0);
	paintSpeechBubbleColumn(bubbles[bubble_indices[kBubbleTopLeft]], bubbles[bubble_indices[kBubbleLeft]], position, vertical_tiles, bubble);
	position.x -= wadj;
	paint(bubbles[bubble_indices[kBubbleBottomLeft]], position, bubble, kPaintPositionIsTop);

	position.x += 33 + wadj;
	for (int i = 0; i < horizontal_tiles; i++) {
		position.y = 0;
		paintSpeechBubbleColumn(bubbles[bubble_indices[kBubbleTop]], bubbles[bubble_indices[kBubbleFill]], position, vertical_tiles, bubble);
		paint(bubbles[bubble_indices[kBubbleBottom]], position, bubble, kPaintPositionIsTop);
		position.x += 4;
	}

	position.y = 0;
	paintSpeechBubbleColumn(bubbles[bubble_indices[kBubbleTopRight]], bubbles[bubble_indices[kBubbleRight]], position, vertical_tiles, bubble);
	paint(bubbles[bubble_indices[kBubbleBottomRight]], position, bubble, kPaintPositionIsTop);

	enum {
		kSpeechTwoLinesShift = 8,
		kSpeechVMargin = 8,
		kSpeechOneLineShift = 0xc,
		kSpeechLeftIndent = 15,
		kSpeechFirstLineExtraIndent = 10
	};

	int shift = 0;
	if (rows == 1)
		shift = kSpeechOneLineShift;
	if (rows == 2)
		shift = kSpeechTwoLinesShift;

	const uint16 roundedWidthExtra = widthExtra & ~uint16(3);
	uint16 currentLeft = kSpeechLeftIndent + kSpeechFirstLineExtraIndent;
	uint16 currentTop = kSpeechVMargin + shift;
	uint16 remainingRows = rows;
	byte currentColour = colour;
	const byte *p = reinterpret_cast<const byte *>(metrics.text.c_str());

	bool stopText = false;
	while (p && *p && !stopText) {
		const byte ch = *p++;
		switch (ch) {
		case kStringCenter: {
			const byte lineWidth = *p++;
			// DOS RenderSpeechBubbleText @ 1000:91c9 consumes the next
			// byte as a precomputed line width and centers within the
			// bubble frame, not within the screen.
			const uint16 centered = uint16(roundedWidthExtra + 0x41 - lineWidth);
			currentLeft = centered >> 1;
			break;
		}
		case kStringAdvance:
			currentLeft += *p++;
			break;
		case '\n':
		case '\r':
			if (remainingRows != 0)
				--remainingRows;
			if (remainingRows == 0) {
				stopText = true;
				break;
			}
			currentLeft = kSpeechLeftIndent;
			if (remainingRows == 1)
				currentLeft += kSpeechFirstLineExtraIndent;
			currentTop += kLineHeight;
			break;
		case kStringDefaultColour:
			currentColour = colour;
			break;
		case kStringSetColour:
			currentColour = *p++;
			break;
		case kStringMenuOption:
			while (*p)
				currentLeft += paintChar(currentLeft, currentTop, currentColour, *p++, bubble);
			++p;
			p += 2;
			break;
		default:
			currentLeft += paintChar(currentLeft, currentTop, currentColour, ch, bubble);
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

	Common::Rect rect(left, top, left + bubble->w, top + bubble->h);
	return rect;
}

Common::Rect Graphics::paintText(uint16 left, uint16 top, byte colour, const byte *string, Surface *dest, uint16 *_lines, uint8 firstLineExtraIndent) {
	byte ch = 0;
	uint16 current_left = left + firstLineExtraIndent;
	uint16 current_top = top;
	uint16 max_left = left;
	byte current_colour = colour;
	uint16 lines = 1;

	int opt;
	while ((ch = *(string++))) {
		switch(ch) {
		case kStringMove:
			current_left = READ_LE_UINT16(string);
			string += 2;
			current_top = READ_LE_UINT16(string);
			string += 2;
			debugC(3, kDebugLevelGraphics, "string move to %d:%d", current_left, current_top);
			break;
		case kStringSetColour:
			current_colour = *(string++);
			break;
		case kStringDefaultColour:
			current_colour = colour;
			break;
		case 0x04:
			break;
		case kStringAdvance:
			current_left += *(string++);
			break;
		case kStringCenter:
			current_left = (320 - calculateLineWidth(string))/2;
			break;
		case '\n':
		case '\r':
			current_left = left;
			current_top += kLineHeight;
			lines++;
			break;
		case kStringMenuOption:
			opt = _mOption++;
			_optionRects[opt] = paintText(current_left, current_top, kOptionColour, string, dest);
			while (*(string++));
			_optionValues[opt] = READ_LE_UINT16(string);
			debugC(2, kDebugLevelGraphics | kDebugLevelScript, "option value %d: 0x%x", opt, _optionValues[opt]);
			string += 2;
			break;
		default:
			current_left += paintChar(current_left, current_top, current_colour, ch, dest);
			if (current_left > max_left)
				max_left = current_left;
		}
	}

	if (_lines)
		*_lines = lines;

	return Common::Rect(left, top, max_left, current_top + kLineHeight);
}

uint16 Graphics::plainTextLineWidth(const byte *string) const {
	uint16 total = 0;
	byte ch;
	while ((ch = *(string++)))
		total += getGlyphWidth(ch);
	return total;
}

Common::Rect Graphics::paintPlainTextLine(uint16 left, uint16 top, byte colour, const byte *string) {
	uint16 current_left = left;
	byte ch;
	while ((ch = *(string++)))
		current_left += paintChar(current_left, top, colour, ch, _framebuffer.get());
	return Common::Rect(left, top, current_left, top + kLineHeight);
}

byte Graphics::clampChar(byte ch) {
	if (ch == '#')
		return '!';
	if (ch < ' ' || ch > '~')
		return '?';
	return ch;
}

uint16 Graphics::calculateLineWidth(const byte *string) const {
	byte ch;
	uint16 total = 0;
	while ((ch = *(string++))) {
		if (ch == '\n' || ch == '\r')
			break;
		total += getGlyphWidth(ch);
	}
	return total;
}

uint16 Graphics::getGlyphWidth(byte ch) const {
	if (ch == ' ')
		return 4;
	else
		return getGlyph(ch)->w-1;
}

Sprite *Graphics::getGlyph(byte ch) const {
	// TODO perhaps cache or sth
	ch = clampChar(ch);
	if (ch == ' ')
		return 0; // space has no glyph, just width 4
	return _resources->getGlyph(ch);
}

/**
 * @returns char width
 */
uint16 Graphics::paintChar(uint16 left, uint16 top, byte colour, byte ch, Surface *dest) const {
	Sprite *glyph = getGlyph(ch);
	int w;
	if (glyph) {
		glyph->recolour(colour);
		if (dest)
			paint(glyph, Common::Point(left, top+glyph->h), dest);
		w = glyph->w - 1;
		delete glyph;
	} else return 4;
	return w;
}

void Graphics::paint(const Sprite *sprite, Common::Point pos, Surface *dest, int flags) const {
	debugC(4, kDebugLevelGraphics, "painting sprite at %d:%d (+%d:%d) [%dx%d]", pos.x, pos.y, sprite->_hotPoint.x, sprite->_hotPoint.y, sprite->w, sprite->h);

	Common::Rect r(sprite->w, sprite->h);
	r.moveTo(pos);
	if (!(flags & kPaintPositionIsTop))
		r.translate(0, -sprite->h); // this is actually bottom
	r.translate(-sprite->_hotPoint.x, sprite->_hotPoint.y);

	// Clip to the destination surface, not the screen — `dest` is often a small
	// auto-allocated surface (e.g. a speech bubble buffer) and a sprite that
	// poked even one pixel past its right/bottom edge would write past the end
	// of the heap allocation. Caught by ASan in resources.cpp:74.
	r.clip(dest->w - 1, dest->h - 1);
	debugC(4, kDebugLevelGraphics, "transformed rect: %d:%d %d:%d", r.left, r.top, r.right, r.bottom);

	dest->blit(sprite, r, 0, (flags & kPaintSemiTransparent) ? &_tintedPalette : 0);
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

void Graphics::updateScreen() {
	_system->copyRectToScreen(reinterpret_cast<byte *>(_framebuffer->getPixels()), _framebuffer->pitch, 0, 0, 320, 200);

	if (_willFadein && (_fadeFlags & kPartialFade)) {
		debugC(3, kDebugLevelGraphics, "performing partial fade in");
		_willFadein = false;
		fadeIn(_interfacePalette + 160*3, 160, 96);
		_inFade = false;
	} else if (_willFadein && !(_fadeFlags & kPartialFade)) {
		fadeIn();
		_willFadein = false;
		_inFade = false;
	}

	_system->updateScreen();
}

void Graphics::showCursor() {
	if (_hostCursorShown)
		return;

	Sprite *cursor = _resources->getCursor();
	assert(cursor->pitch == cursor->w);
	::Graphics::CursorManager &m = ::Graphics::CursorManager::instance();
	m.replaceCursor(reinterpret_cast<byte *>(cursor->getPixels()), cursor->w, cursor->h, cursor->_hotPoint.x, cursor->_hotPoint.y, 0);
	m.showMouse(true);
	_hostCursorShown = true;
}

void Graphics::hideCursor() {
	::Graphics::CursorManager::instance().showMouse(false);
	_hostCursorShown = false;
}

void Graphics::paintRect(const Common::Rect &r, byte colour) {
	_framebuffer->frameRect(r, colour);
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

const char Graphics::_charwidths[] = {
	#include "charwidths.data"
};

void Graphics::clearPalette(int offset, int count) {
	byte pal[0x300];
	Common::fill(pal, pal+0x300, 0);
	_system->getPaletteManager()->setPalette(pal, offset, count);
}

void Graphics::setPalette(const byte *colours, uint start, uint num) {
	_system->getPaletteManager()->setPalette(colours, start, num);

	// calculate tinted palette
	for (int i = 0; i < 256; ++i) {
		const byte *colour = colours + i*3;
		const byte luma = (30 * colour[0] + 60 * colour[1] + 10 * colour[2])/100;

		byte curr = 174;
		byte best_diff = 255;
		byte best_color = curr;

		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 2; k++) {
				int16 diff = _interfacePalette[(curr + k) * 3] - luma;
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
	_fullscreen = enabled;
}

void Graphics::clearFramebuffer(byte colour) {
	if (_framebuffer)
		_framebuffer->fillRect(Common::Rect(0, 0, 320, 200), colour);
}

void Graphics::clearPaletteRange(int start, int count) {
	clearPalette(start, count);
}

struct Tr {
	byte operator()(const byte &b) const { return 0xff & ((b << 1) - 63); }
};

void Graphics::fadeIn(const byte *colours, uint start, uint num) {
	byte buf[0x300];
	if (!colours) {
		_system->getPaletteManager()->grabPalette(buf, start, num);
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
		Eng.delay(1000/25);

		if (Log.canSkipCutscene() && Eng.escapePressed()) {
			Log.requestSkipCutscene();
			_system->getPaletteManager()->setPalette(colours, start, num);
			_system->updateScreen();
			return;
		}
	}
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

	_system->getPaletteManager()->grabPalette(current, offset, colours);

	for (int j = 0; j < 63; j++) {
		for (int i = 0; i < bytes; i++)
			current[i] -= MIN<byte>(4, current[i]);

		_system->getPaletteManager()->setPalette((current), offset, colours);
		_system->updateScreen();
		Eng.delay(1000/25);

		if (Log.canSkipCutscene() && Eng.escapePressed()) {
			Log.requestSkipCutscene();
			return false;
		}

	}
	return true;
}

void Graphics::say(const byte *text, uint16 length, uint16 frames) {
	// Default position/color = top-left, color 235 (matches the
	// pre-Pass2 behaviour). Op_47/0x48 use sayAt() instead.
	sayAt(text, length, frames, 0, 0, 235);
}

static Common::Array<Common::String> paginateSpeechText(const byte *text, uint16 length, uint16 maxLines) {
	Common::Array<Common::String> pages;
	Common::String full(reinterpret_cast<const char *>(text), length);
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

static byte *copySpeechText(const Common::String &page) {
	byte *buf = new byte[page.size() + 1];
	memcpy(buf, page.c_str(), page.size());
	buf[page.size()] = 0;
	return buf;
}

static uint16 speechTicksForText(const Common::String &text) {
	// DOS speech slots use the low byte of FormatBubbleText's height
	// return as the countdown (AllocSpeechSlot @ 1000:9b1f,
	// AllocSpeechSlot_NoFormatting @ 1000:9c7e).
	Common::String normalized;
	for (uint i = 0; i < text.size(); ++i)
		normalized += char(text[i] == '\n' ? '\r' : text[i]);
	Logic::FormattedBubble fb = Log.formatBubbleText(reinterpret_cast<const byte *>(normalized.c_str()));
	return uint8(fb.totalHeight & 0xff);
}

void Graphics::sayAt(const byte *text, uint16 length, uint16 frames,
                     uint16 x, uint16 y, byte color, uint16 maxLines,
                     SpeechBubbleMode bubbleMode, bool forceBubble) {
	// Callers pass `length = strlen(text)` (no NUL counted). paintText
	// scans with `while ((ch = *string++))` and needs a NUL terminator,
	// so over-allocate by 1 and write a sentinel. Without this, paintText
	// reads past the end of the heap allocation and ASan trips
	// (caught iter-29: 21-byte allocation, paintText reading byte 22).
	Common::Array<Common::String> pages = paginateSpeechText(text, length, maxLines);
	bool started = false;
	for (uint i = 0; i < pages.size(); ++i) {
		SpeechEntry e;
		e.text = copySpeechText(pages[i]);
		e.length = uint16(pages[i].size());
		e.frames = (pages.size() == 1) ? frames : speechTicksForText(pages[i]);
		e.x = x;
		e.y = y;
		e.color = color;
		e.bubble = forceBubble || maxLines != 0 || x != 0 || y != 0;
		e.bubbleMode = bubbleMode;

		if (_speech || i != 0) {
			// Queue this utterance to play after the current one (and any
			// already-queued ones) finish. The text buffer is owned by the
			// queue entry until paintSpeech promotes it into _speech.
			_speechQueue.push(e);
			debugC(2, kDebugLevelGraphics, "queued speech at %u,%u (queue size %u)",
				x, y, (uint)_speechQueue.size());
			continue;
		}

		_speech = e.text;
		_speechFramesLeft = e.frames;
		_speechX = e.x;
		_speechY = e.y;
		_speechColor = e.color;
		_speechBubble = e.bubble;
		_speechBubbleMode = e.bubbleMode;
		started = true;
	}

	if (started)
		paintSpeech();
}

void Graphics::clearSpeech() {
	delete[] _speech;
	_speech = 0;
	_speechFramesLeft = 0;
	_speechX = 0;
	_speechY = 0;
	_speechColor = 235;
	_speechBubble = false;
	_speechBubbleMode = kSpeechBubbleType1;
	_speechDoneCallback.reset();

	while (!_speechQueue.empty()) {
		SpeechEntry entry = _speechQueue.pop();
		delete[] entry.text;
	}
}

void Graphics::runWhenSaid(const CodePointer &cb) {
	// Bind the callback to the most-recently-queued utterance (or to the
	// current one if the queue is empty). DOS pattern is: emit speech →
	// emit "wait until said" → callback fires when THAT speech finishes.
	if (!_speechQueue.empty()) {
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
			Log.runLater(last.cb);
		}
		last.cb = cb;
		while (!tmp.empty())
			_speechQueue.push(tmp.pop());
		_speechQueue.push(last);
		return;
	}

	if (!_speechDoneCallback.isEmpty()) {
		// Active speech already has a callback bound — chain via runLater
		// so both fire (older one first, then this one as part of the
		// post-speech callback chain).
		Log.runLater(_speechDoneCallback);
	}
	_speechDoneCallback = cb;
}

} // End of namespace Interspective
