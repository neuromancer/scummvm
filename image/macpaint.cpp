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

#include "image/macpaint.h"

#include "common/formats/iff_container.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Image {

// MacPaint canvas: 576 pixels wide, 720 lines tall
static const uint16 kMacPaintWidth = 576;
static const uint16 kMacPaintHeight = 720;
static const uint16 kMacPaintBytesPerLine = 72;  // 576 / 8
static const uint32 kMacPaintHeaderSize = 512;

// 1-bit palette: index 0 = white, index 1 = black (Mac convention: 1 = black)
static const byte macPaintPalette[2 * 3] = {
	0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00
};

MacPaintDecoder::MacPaintDecoder() : _surface(nullptr), _palette(macPaintPalette, 2) {
}

MacPaintDecoder::~MacPaintDecoder() {
	destroy();
}

void MacPaintDecoder::destroy() {
	if (_surface) {
		_surface->free();
		delete _surface;
		_surface = nullptr;
	}
}

void MacPaintDecoder::expandBits(const byte *src, uint16 width, uint16 height, uint16 bytesPerLine) {
	_surface = new Graphics::Surface();
	_surface->create(width, height, Graphics::PixelFormat::createFormatCLUT8());

	for (uint16 y = 0; y < height; y++) {
		byte *dst = (byte *)_surface->getBasePtr(0, y);
		const byte *lineStart = src + y * bytesPerLine;

		for (uint16 x = 0; x < width;) {
			byte packed = lineStart[x / 8];
			// MSB = leftmost pixel
			for (int k = 7; k >= 0 && x < width; k--, x++) {
				*dst++ = (packed >> k) & 1;
			}
		}
	}
}

bool MacPaintDecoder::loadStream(Common::SeekableReadStream &stream) {
	destroy();

	// MacPaint files have a 512-byte header:
	//   Bytes 0-3:     version (0x00000000 = v1, 0x00000002 = v2)
	//   Bytes 4-307:   38 brush patterns (8 bytes each)
	//   Bytes 308-511: reserved/padding
	//   Bytes 512+:    PackBits-compressed bitmap (72 bytes/line, 720 lines)

	if (stream.size() < (int64)kMacPaintHeaderSize + 2) {
		warning("MacPaintDecoder: File too small (%d bytes)", (int)stream.size());
		return false;
	}

	uint32 version = stream.readUint32BE();
	if (version != 0 && version != 2) {
		warning("MacPaintDecoder: Unexpected version %u (expected 0 or 2)", version);
		return false;
	}

	// Skip to the compressed data
	stream.seek(kMacPaintHeaderSize);

	// Decompress PackBits data using Common::PackBitsReadStream
	uint32 expectedSize = kMacPaintBytesPerLine * kMacPaintHeight;
	byte *decompressed = new byte[expectedSize];
	memset(decompressed, 0, expectedSize);

	Common::PackBitsReadStream packStream(stream);
	uint32 bytesRead = packStream.read(decompressed, expectedSize);
	if (bytesRead < expectedSize) {
		warning("MacPaintDecoder: Only decompressed %u of %u bytes", bytesRead, expectedSize);
	}

	expandBits(decompressed, kMacPaintWidth, kMacPaintHeight, kMacPaintBytesPerLine);

	delete[] decompressed;
	return true;
}

bool MacPaintDecoder::loadRawBitmap(Common::SeekableReadStream &stream, uint16 width, uint16 height) {
	destroy();

	uint16 bytesPerLine = (width + 7) / 8;
	uint32 expectedSize = bytesPerLine * height;

	if (stream.size() < (int64)expectedSize) {
		warning("MacPaintDecoder::loadRawBitmap: File too small (%d bytes, expected %u for %ux%u)",
		        (int)stream.size(), expectedSize, width, height);
		return false;
	}

	byte *raw = new byte[expectedSize];
	stream.read(raw, expectedSize);

	expandBits(raw, width, height, bytesPerLine);

	delete[] raw;
	return true;
}

} // End of namespace Image
