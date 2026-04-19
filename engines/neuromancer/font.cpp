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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#include "neuromancer/font.h"

#include "common/scummsys.h"
#include "graphics/fonts/dosfont.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Neuromancer {

// Singleton DosFont instance. DOS Neuromancer's glyphs are the standard PC
// BIOS 8x8 CP437 set, which is exactly what Graphics::DosFont ships. Using
// it means we inherit ScummVM's Font base class for free (kerning helpers,
// word wrap, alignment) instead of carrying a private glyph table.
static const Graphics::Font &neuroFont() {
	static Graphics::DosFont s_font;
	return s_font;
}

const Graphics::Font &getFont() {
	return neuroFont();
}

Common::String expandText(const char *raw,
                          const char *playerName,
                          const char *dateString) {
	if (!raw)
		return Common::String();
	Common::String out;
	while (*raw) {
		byte b = (byte)*raw++;
		if (b == 0x01) {
			if (playerName) out += playerName;
		} else if (b == 0x02) {
			if (dateString) out += dateString;
		} else if (b == '\r') {
			out += '\n';
		} else if (b == '\n' || b >= 0x20) {
			out += (char)b;
		}
		// Other control bytes (< 0x20, not \r/\n): drop silently.
	}
	return out;
}

Common::String wrapText(const char *text, int columns) {
	if (!text || columns <= 0)
		return Common::String();

	Common::String out;
	int lineLen = 0;

	while (*text) {
		if (*text == '\n') {
			out += '\n';
			lineLen = 0;
			text++;
			continue;
		}

		const char *wordStart = text;
		while (*text && *text != ' ' && *text != '\n')
			text++;
		int wordLen = (int)(text - wordStart);

		if (lineLen > 0 && lineLen + 1 + wordLen > columns) {
			out += '\n';
			lineLen = 0;
		}

		if (lineLen > 0) {
			out += ' ';
			lineLen++;
		}
		out += Common::String(wordStart, wordLen);
		lineLen += wordLen;

		if (*text == ' ') text++;
	}

	return out;
}

// Pack an 8bpp CLUT8 row into two-pixels-per-byte 4bpp used by our IMH
// sprite buffers. `src` is `widthPx` bytes, `dst` is `widthPx / 2` bytes.
// Colour index 15 stays 0xF in each nibble, everything else clamps to the
// low-4-bit value of the source byte (which our palette limits to 0..15).
static void packRow4bpp(const byte *src, byte *dst, int widthPx) {
	const int packedW = widthPx / 2;
	for (int i = 0; i < packedW; i++) {
		byte hi = (byte)(src[i * 2 + 0] & 0x0F);
		byte lo = (byte)(src[i * 2 + 1] & 0x0F);
		dst[i] = (byte)((hi << 4) | lo);
	}
}

void drawString(const char *string, int widthPx, int heightPx,
                int leftPx, int topPx, byte *dst) {
	if (!string || !dst || widthPx <= 0 || heightPx <= 0)
		return;

	// Build a temporary CLUT8 ManagedSurface at the full widget extent,
	// pre-populated from the packed 4bpp destination so glyph pixels end
	// up overlaid on whatever chrome the caller already painted (e.g. the
	// white DOS window body, the transparent-key sentinel for the scroll
	// widget, or the bubble border). We draw BLACK ink on top, matching
	// drawChar's default colour for EGA index 0.
	Graphics::ManagedSurface scratch(widthPx, heightPx,
	                                 Graphics::PixelFormat::createFormatCLUT8());

	const int packedW = widthPx / 2;
	for (int y = 0; y < heightPx; y++) {
		const byte *srcRow = dst + y * packedW;
		byte *dstRow = (byte *)scratch.getBasePtr(0, y);
		for (int x = 0; x < packedW; x++) {
			byte v = srcRow[x];
			dstRow[x * 2 + 0] = (byte)((v >> 4) & 0x0F);
			dstRow[x * 2 + 1] = (byte)(v & 0x0F);
		}
	}

	const Graphics::Font &font = neuroFont();
	const int charW = font.getMaxCharWidth();
	const int charH = font.getFontHeight();

	int curX = leftPx;
	int curY = topPx;

	while (char c = *string++) {
		if (c == '\n') {
			curX = leftPx;
			curY += charH;
			continue;
		}
		if ((byte)c < 0x20 || (byte)c > 0xFE)
			continue;
		if (curY + charH > heightPx)
			break;
		if (curX + charW > widthPx) {
			// Caller should pre-wrap; drop the rest of the line.
			while (*string && *string != '\n') string++;
			continue;
		}
		font.drawChar(scratch.surfacePtr(), (uint32)(byte)c, curX, curY, 0);
		curX += charW;
	}

	// Pack the result back into the caller's 4bpp buffer.
	for (int y = 0; y < heightPx; y++) {
		const byte *srcRow = (const byte *)scratch.getBasePtr(0, y);
		byte *dstRow = dst + y * packedW;
		packRow4bpp(srcRow, dstRow, widthPx);
	}
}

} // End of namespace Neuromancer
