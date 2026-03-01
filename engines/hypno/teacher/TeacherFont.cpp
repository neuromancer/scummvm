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

#include "hypno/teacher/TeacherFont.h"
#include "hypno/grammar.h"
#include "hypno/hypno.h"
#include "common/file.h"
#include "common/path.h"
#include "common/debug.h"
#include "common/system.h"
#include "graphics/paletteman.h"

namespace Hypno {

TeacherFont::TeacherFont()
	: _loaded(false), _firstChar(0x21), _glyphHeight(0),
	  _spaceWidth(0), _charAdvance(2), _markerValue(1), _hasPalette(false),
	  _remapBuilt(false) {
	memset(_palette, 0, sizeof(_palette));
	memset(_remapTable, 0, sizeof(_remapTable));
}

TeacherFont::~TeacherFont() {
	if (_loaded) {
		_glyphSheet.free();
	}
}

bool TeacherFont::load(const Common::String &filename) {
	HypnoSmackerDecoder decoder;
	Common::File *file = new Common::File();

	if (!file->open(Common::Path(filename))) {
		warning("TeacherFont: cannot open '%s'", filename.c_str());
		delete file;
		return false;
	}

	if (!decoder.loadStream(file)) {
		warning("TeacherFont: cannot load SMK '%s'", filename.c_str());
		return false;
	}

	decoder.start();

	const Graphics::Surface *frame = decoder.decodeNextFrame();
	if (!frame) {
		warning("TeacherFont: cannot decode first frame of '%s'", filename.c_str());
		decoder.close();
		return false;
	}

	if (decoder.hasDirtyPalette() && decoder.getPalette()) {
		memcpy(_palette, decoder.getPalette(), 256 * 3);
		_hasPalette = true;
	}

	_glyphSheet.copyFrom(*frame);
	decoder.close();

	buildGlyphTable();
	_loaded = true;

	warning("TeacherFont: loaded '%s' (%dx%d) — %d glyphs, glyphHeight=%d",
		filename.c_str(), _glyphSheet.w, _glyphSheet.h,
		(int)_glyphTable.size(), _glyphHeight);

	return true;
}

// Original: AnimatedAsset::BuildGlyphTable (0x421260)
//
// The original uses a bottom-up VBuffer (like Windows BMP), so it accesses
// row (height-1-offset) in memory to read visual row 'offset' from the top.
// ScummVM's SmackerDecoder outputs a top-down surface, so visual row 'offset'
// is simply memory row 'offset'. The algorithm is otherwise identical.
void TeacherFont::buildGlyphTable() {
	int w = _glyphSheet.w;
	int h = _glyphSheet.h;
	const byte *pixels = (const byte *)_glyphSheet.getPixels();

	// Original: glyphCount = (0x7e - firstChar) + 1 = 94
	int glyphCount = (0x7e - _firstChar) + 1;
	_glyphTable.resize(glyphCount);
	for (int i = 0; i < glyphCount; i++) {
		_glyphTable[i].left = 0;
		_glyphTable[i].top = 0;
		_glyphTable[i].right = 0;
		_glyphTable[i].bottom = 0;
	}

	// Original: scans from visual row 1 downward looking for marker at col 0.
	// Original formula: row = (height-1-glyphHeight) in bottom-up VBuffer
	//                 = visual row glyphHeight from top.
	// ScummVM top-down: visual row glyphHeight = memory row glyphHeight.
	_glyphHeight = 1;
	if (h > 1) {
		byte glyphVal = _markerValue;
		do {
			if (pixels[_glyphHeight * w + 0] == glyphVal)
				break;
			_glyphHeight++;
		} while (h > _glyphHeight);
	}

	// Scan glyph rows from visual top to bottom.
	// Original formula: row = (height-1-offset) in bottom-up = visual row 'offset'.
	// ScummVM top-down: visual row 'offset' = memory row 'offset'.
	int col = 0;
	int offset = 0;

	while (offset < h && col < glyphCount) {
		int x = 0;
		int startX = 0;
		int row = offset;

		while (x < w) {
			if (pixels[row * w + x] == _markerValue) {
				_glyphTable[col].left = startX;
				_glyphTable[col].right = x - 1;
				_glyphTable[col].top = offset;
				_glyphTable[col].bottom = offset + _glyphHeight - 1;
				if (glyphCount - col == 1)
					goto done;
				startX = x + 1;
				col++;
			}
			x++;
		}

		offset = offset + _glyphHeight + 1;
	}
done:

	// Original: spaceWidth = ComputeTextMetrics("A") * 2 / 3
	int aIdx = 'A' - _firstChar;
	if (aIdx >= 0 && aIdx < glyphCount && _glyphTable[aIdx].right > _glyphTable[aIdx].left) {
		int aWidth = _glyphTable[aIdx].right - _glyphTable[aIdx].left;
		_spaceWidth = aWidth * 2 / 3;
	} else {
		int totalWidth = 0;
		int validCount = 0;
		for (int i = 0; i < glyphCount; i++) {
			int gw = _glyphTable[i].right - _glyphTable[i].left;
			if (gw > 0) {
				totalWidth += gw;
				validCount++;
			}
		}
		_spaceWidth = validCount > 0 ? (totalWidth / validCount) * 2 / 3 : 8;
	}
}

int TeacherFont::getFontHeight() const {
	return _glyphHeight > 0 ? _glyphHeight : 16;
}

int TeacherFont::getMaxCharWidth() const {
	int maxW = 0;
	for (uint i = 0; i < _glyphTable.size(); i++) {
		int gw = _glyphTable[i].right - _glyphTable[i].left;
		if (gw > maxW) maxW = gw;
	}
	return maxW > 0 ? maxW + _charAdvance : 16;
}

int TeacherFont::getCharWidth(uint32 chr) const {
	if (chr == ' ' || chr == '\t')
		return _spaceWidth + _charAdvance;

	int idx = (int)chr - _firstChar;
	if (idx < 0 || idx >= (int)_glyphTable.size())
		return _spaceWidth + _charAdvance;

	return (_glyphTable[idx].right - _glyphTable[idx].left) + _charAdvance;
}

// Build a remap table from font palette indices to scene palette indices.
// The font's SMK has its own palette, but the scene may use a different one.
// Original CallBlitter2 did raw pixel copy because the game always used one
// shared palette; in ScummVM we need to remap.
void TeacherFont::buildRemapTable() const {
	byte scenePalette[256 * 3];
	g_system->getPaletteManager()->grabPalette(scenePalette, 0, 256);

	for (int i = 0; i < 256; i++) {
		int fr = _palette[i * 3];
		int fg = _palette[i * 3 + 1];
		int fb = _palette[i * 3 + 2];

		int bestDist = 0x7FFFFFFF;
		int bestIdx = 0;
		for (int j = 1; j < 256; j++) { // Skip index 0 (usually transparent/black)
			int dr = fr - scenePalette[j * 3];
			int dg = fg - scenePalette[j * 3 + 1];
			int db = fb - scenePalette[j * 3 + 2];
			int dist = dr * dr + dg * dg + db * db;
			if (dist < bestDist) {
				bestDist = dist;
				bestIdx = j;
				if (dist == 0)
					break;
			}
		}
		_remapTable[i] = (byte)bestIdx;
	}
	_remapTable[0] = 0; // Preserve transparent
	_remapBuilt = true;
}

// Original: AnimatedAsset::DrawChar (0x421420)
// Uses CallBlitter2 (TPaste) mode: raw pixel copy from glyph sheet.
// top/bottom in glyphTable are in visual coordinates (top-down), which
// match ScummVM's surface layout directly.
void TeacherFont::drawChar(Graphics::Surface *dst, uint32 chr, int x, int y, uint32 color) const {
	if (chr == ' ' || chr == '\t')
		return;

	int idx = (int)chr - _firstChar;
	if (idx < 0 || idx >= (int)_glyphTable.size())
		return;

	const GlyphRect &r = _glyphTable[idx];
	if (r.right <= r.left)
		return;

	// Build palette remap table on first use
	if (_hasPalette && !_remapBuilt)
		buildRemapTable();

	int glyphW = r.right - r.left + 1;
	int glyphH = r.bottom - r.top + 1;

	const byte *srcPixels = (const byte *)_glyphSheet.getPixels();
	int srcW = _glyphSheet.w;
	int srcH = _glyphSheet.h;

	for (int row = 0; row < glyphH; row++) {
		int dy = y + row;
		if (dy < 0 || dy >= dst->h)
			continue;

		int srcRow = r.top + row;
		if (srcRow < 0 || srcRow >= srcH)
			continue;

		for (int col = 0; col < glyphW; col++) {
			int dx = x + col;
			if (dx < 0 || dx >= dst->w)
				continue;

			int srcCol = r.left + col;
			byte srcPix = srcPixels[srcRow * srcW + srcCol];

			// Skip marker and black (transparent) pixels
			if (srcPix == _markerValue || srcPix == 0)
				continue;

			byte destPix = (_hasPalette && _remapBuilt) ? _remapTable[srcPix] : srcPix;
			*((byte *)dst->getBasePtr(dx, dy)) = destPix;
		}
	}
}

} // End of namespace Hypno
