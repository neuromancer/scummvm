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

#include "neuromancer/decompress.h"

#include "common/endian.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

struct HuffmanNode {
	byte value;
	HuffmanNode *left;
	HuffmanNode *right;
};

class HuffmanBitReader {
public:
	explicit HuffmanBitReader(const byte *src) : _src(src), _mask(0), _byte(0) {}

	uint32 readBits(int n) {
		uint32 bits = 0;
		for (int i = 0; i < n; i++) {
			if (_mask == 0) {
				_byte = *_src++;
				_mask = 0x80;
			}
			bits = (bits << 1) | ((_byte & _mask) ? 1 : 0);
			_mask >>= 1;
		}
		return bits;
	}

	uint32 readLE32() {
		byte b0 = *_src++;
		byte b1 = *_src++;
		byte b2 = *_src++;
		byte b3 = *_src++;
		return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
	}

private:
	const byte *_src;
	byte _mask;
	byte _byte;
};

HuffmanNode *buildTree(HuffmanBitReader &reader) {
	HuffmanNode *node = new HuffmanNode();
	if (reader.readBits(1)) {
		node->left = node->right = nullptr;
		node->value = (byte)reader.readBits(8);
	} else {
		node->right = buildTree(reader);
		node->left = buildTree(reader);
		node->value = 0;
	}
	return node;
}

void destroyTree(HuffmanNode *node) {
	if (!node)
		return;
	destroyTree(node->left);
	destroyTree(node->right);
	delete node;
}

// Differential row XOR: each row[i+1] ^= row[i]. Used as a final transform
// on IMH/PIC pixel data to recover the image from the RLE-decoded delta form.
void xorRows(byte *data, uint32 width, uint32 height) {
	if (height < 2)
		return;
	for (uint32 i = 0; i < height - 1; i++) {
		byte *prev = data + i * width;
		byte *next = prev + width;
		for (uint32 j = 0; j < width; j++)
			next[j] ^= prev[j];
	}
}

uint32 decodeImhStream(const byte *src, uint32 len, byte *dst) {
	uint32 total = 0;
	while (len >= sizeof(ImhHeader)) {
		const ImhHeader *hdr = (const ImhHeader *)src;
		uint32 w = READ_LE_UINT16(&hdr->width);
		uint32 h = READ_LE_UINT16(&hdr->height);
		uint32 pixels = w * h;

		memcpy(dst, src, sizeof(ImhHeader));
		src += sizeof(ImhHeader);
		dst += sizeof(ImhHeader);
		len -= sizeof(ImhHeader);
		total += sizeof(ImhHeader);

		uint32 processed = decodeRLE(src, pixels, dst);
		xorRows(dst, w, h);
		src += processed;
		len -= processed;
		dst += pixels;
		total += pixels;
	}
	return total;
}

} // anonymous namespace

uint32 huffmanDecompress(const byte *src, byte *dst) {
	HuffmanBitReader reader(src);
	uint32 length = reader.readLE32();
	HuffmanNode *root = buildTree(reader);
	HuffmanNode *node = root;

	uint32 i = 0;
	while (i < length) {
		node = reader.readBits(1) ? node->left : node->right;
		if (!node->left) {
			dst[i++] = node->value;
			node = root;
		}
	}

	destroyTree(root);
	return length;
}

uint32 decodeRLE(const byte *src, uint32 len, byte *dst) {
	const byte *start = src;
	while (len) {
		if (*src > 0x7F) {
			// Literal run: next (0x100 - b) bytes copied verbatim.
			uint32 n = 0x100 - *src++;
			while (n--) {
				*dst++ = *src++;
				len--;
			}
		} else {
			// Repeated byte: (count+1) copies of value.
			uint32 n = *src++;
			byte v = *src++;
			n++;
			memset(dst, v, n);
			dst += n;
			len -= n;
		}
	}
	return (uint32)(src - start);
}

uint32 decompressIMH(const byte *src, byte *dst) {
	// Temporary buffer sized for the DOS-era worst case (320x200).
	byte tmp[64000];
	uint32 len = huffmanDecompress(src, tmp);
	return decodeImhStream(tmp, len, dst);
}

uint32 decompressPIC(const byte *src, byte *dst) {
	// PIC frames are a fixed 152x112 after Huffman+RLE+XOR.
	byte tmp[64000];
	huffmanDecompress(src, tmp);
	decodeRLE(tmp, 152 * 112, dst);
	xorRows(dst, 152, 112);
	return 152 * 112;
}

uint32 decompressBIH(const byte *src, byte *dst) { return huffmanDecompress(src, dst); }
uint32 decompressANH(const byte *src, byte *dst) { return huffmanDecompress(src, dst); }
uint32 decompressTXH(const byte *src, byte *dst) { return huffmanDecompress(src, dst); }

} // End of namespace Neuromancer
