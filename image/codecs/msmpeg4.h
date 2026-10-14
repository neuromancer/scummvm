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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMAGE_CODECS_MSMPEG4_H
#define IMAGE_CODECS_MSMPEG4_H

#include "image/codecs/codec.h"

namespace Image {

// Microsoft MPEG-4 version 3 (MP43), 8-bit YUV420 I/P pictures.
class MSMPEG4Decoder : public Codec {
public:
	MSMPEG4Decoder(int width, int height);
	~MSMPEG4Decoder() override;
	const Graphics::Surface *decodeFrame(Common::SeekableReadStream &stream) override;
	Graphics::PixelFormat getPixelFormat() const override { return _format; }
	bool setOutputPixelFormat(const Graphics::PixelFormat &format) override;

private:
	struct Context;
	Context *_context;
	Graphics::Surface _surface;
	Graphics::PixelFormat _format;
};

} // namespace Image

#endif
