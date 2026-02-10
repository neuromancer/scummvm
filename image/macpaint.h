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

#ifndef IMAGE_MACPAINT_H
#define IMAGE_MACPAINT_H

#include "graphics/palette.h"
#include "image/image_decoder.h"

namespace Image {

/**
 * @defgroup image_macpaint MacPaint decoder
 * @ingroup image
 *
 * @brief Decoder for MacPaint images.
 *
 * Supports MacPaint 1.0 and 2.0 files (576x720, 1-bit B&W, PackBits compressed)
 * and raw Macintosh screen bitmaps (headerless 1-bit B&W, MSB-first).
 *
 * Used in engines:
 * - Glk (Angel)
 * @{
 */

class MacPaintDecoder : public ImageDecoder {
public:
	MacPaintDecoder();
	~MacPaintDecoder() override;

	// ImageDecoder API
	void destroy() override;
	bool loadStream(Common::SeekableReadStream &stream) override;
	const Graphics::Surface *getSurface() const override { return _surface; }
	const Graphics::Palette &getPalette() const override { return _palette; }

	/**
	 * Load a raw (headerless) Macintosh 1-bit bitmap.
	 *
	 * The stream must contain exactly width*height/8 bytes of packed pixel
	 * data (8 pixels per byte, MSB = leftmost, 1 = black, 0 = white).
	 *
	 * @param stream  Input stream containing the raw bitmap.
	 * @param width   Image width in pixels (must be a multiple of 8).
	 * @param height  Image height in pixels.
	 * @return Whether loading succeeded.
	 */
	bool loadRawBitmap(Common::SeekableReadStream &stream, uint16 width, uint16 height);

private:
	Graphics::Surface *_surface;
	Graphics::Palette _palette;

	/** Expand packed 1-bit data (MSB-first) into CLUT8 surface pixels. */
	void expandBits(const byte *src, uint16 width, uint16 height, uint16 bytesPerLine);
};

/** @} */
} // End of namespace Image

#endif
