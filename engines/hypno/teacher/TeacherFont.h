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

#ifndef HYPNO_TEACHER_FONT_H
#define HYPNO_TEACHER_FONT_H

#include "graphics/font.h"
#include "graphics/surface.h"
#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"

namespace Hypno {

// Original: AnimatedAsset class (0x421260)
// Loads a Smacker file as a font sprite sheet. The first frame contains all
// glyphs laid out horizontally, separated by marker pixels (value 1).
// Characters start at ASCII 0x21 ('!').
class TeacherFont : public Graphics::Font {
public:
	TeacherFont();
	~TeacherFont() override;

	bool load(const Common::String &filename);

	// Graphics::Font interface
	int getFontHeight() const override;
	int getMaxCharWidth() const override;
	int getCharWidth(uint32 chr) const override;
	void drawChar(Graphics::Surface *dst, uint32 chr, int x, int y, uint32 color) const override;

private:
	void buildGlyphTable();
	void buildRemapTable() const;

	Graphics::Surface _glyphSheet;  // Decoded first frame of the SMK
	bool _loaded;

	// Original: GlyphRect array — bounding rect for each glyph
	struct GlyphRect {
		int left, top, right, bottom;
		GlyphRect() : left(0), top(0), right(0), bottom(0) {}
	};

	Common::Array<GlyphRect> _glyphTable;
	int _firstChar;     // Original: 0x21 ('!')
	int _glyphHeight;   // Height of glyphs (rows)
	int _spaceWidth;    // Width of space character
	int _charAdvance;   // Inter-character advance pixels (original: char_adv.advance = 2)
	byte _markerValue;  // Separator pixel value (auto-detected)
	byte _palette[256 * 3]; // Font's own palette from the SMK file
	bool _hasPalette;

	// Remap table: font palette index -> scene palette index
	mutable byte _remapTable[256];
	mutable bool _remapBuilt;
};

} // End of namespace Hypno

#endif
