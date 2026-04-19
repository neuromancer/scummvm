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

#ifndef NEUROMANCER_DECOMPRESS_H
#define NEUROMANCER_DECOMPRESS_H

#include "common/scummsys.h"

namespace Neuromancer {

// IMH (Image) header: shared by sprites and other image resources.
struct ImhHeader {
	uint16 dx;
	uint16 dy;
	uint16 width;
	uint16 height;
};

// Huffman decompressor used by all BIH/ANH/TXH and inner IMH/PIC payloads.
// Returns the decompressed length in bytes.
uint32 huffmanDecompress(const byte *src, byte *dst);

// RLE pass used inside IMH/PIC decoding.
uint32 decodeRLE(const byte *src, uint32 len, byte *dst);

// Full decompression pipelines. Return decompressed length.
uint32 decompressIMH(const byte *src, byte *dst);
uint32 decompressPIC(const byte *src, byte *dst);
uint32 decompressBIH(const byte *src, byte *dst);
uint32 decompressANH(const byte *src, byte *dst);
uint32 decompressTXH(const byte *src, byte *dst);

} // End of namespace Neuromancer

#endif // NEUROMANCER_DECOMPRESS_H
