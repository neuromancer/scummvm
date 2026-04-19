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

#include "neuromancer/gfx.h"

#include "common/endian.h"
#include "common/rect.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

namespace Neuromancer {

static const byte kEgaPalette[16 * 3] = {
	0x00, 0x00, 0x00,   // 0  black (transparent key)
	0x00, 0x00, 0xAA,   // 1  blue
	0x00, 0xAA, 0x00,   // 2  green
	0x00, 0xAA, 0xAA,   // 3  cyan
	0xAA, 0x00, 0x00,   // 4  red
	0xAA, 0x00, 0xAA,   // 5  magenta
	0xAA, 0x55, 0x00,   // 6  brown
	0xAA, 0xAA, 0xAA,   // 7  light gray
	0x55, 0x55, 0x55,   // 8  dark gray
	0x55, 0x55, 0xFF,   // 9  light blue
	0x00, 0xFF, 0x55,   // 10 light green
	0x55, 0xFF, 0xFF,   // 11 light cyan
	0xFF, 0x55, 0x55,   // 12 light red
	0xFF, 0x55, 0xFF,   // 13 light magenta
	0xFF, 0xFF, 0x55,   // 14 yellow
	0xFF, 0xFF, 0xFF,   // 15 white
};

// Transparent-key colour index. Matches the DOS sprite chain which treats
// index 0 as "see-through" for non-opaque layers.
static const byte kTransparentKey = 0;

void setEgaPalette() {
	byte palette[256 * 3];
	memset(palette, 0, sizeof(palette));
	memcpy(palette, kEgaPalette, sizeof(kEgaPalette));
	g_system->getPaletteManager()->setPalette(palette, 0, 256);
}

Graphics::ManagedSurface *unpackImh4bpp(const byte *imh, int16 &dxOut, int16 &dyOut) {
	dxOut         = (int16)READ_LE_UINT16(imh + 0);
	dyOut         = (int16)READ_LE_UINT16(imh + 2);
	uint16 packedW = READ_LE_UINT16(imh + 4);
	uint16 height  = READ_LE_UINT16(imh + 6);
	const byte *pix = imh + sizeof(ImhHeader);

	int pixelWidth = (int)packedW * 2;
	Graphics::ManagedSurface *surface =
		new Graphics::ManagedSurface(pixelWidth, (int)height,
		                             Graphics::PixelFormat::createFormatCLUT8());

	for (int y = 0; y < (int)height; y++) {
		byte *dst = (byte *)surface->getBasePtr(0, y);
		const byte *src = pix + y * packedW;
		for (int b = 0; b < (int)packedW; b++) {
			byte v = src[b];
			dst[b * 2]     = v >> 4;
			dst[b * 2 + 1] = v & 0x0F;
		}
	}

	return surface;
}

SpriteChain::SpriteChain()
	: _frame(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8()) {}

SpriteChain::~SpriteChain() {
	clearAll();
}

void SpriteChain::addSprite(int layerIdx, int left, int top, const byte *imhData, bool opaque) {
	if (layerIdx < 0 || layerIdx >= kNumLayers) {
		warning("Neuromancer: SpriteChain::addSprite invalid layer %d", layerIdx);
		return;
	}

	SpriteLayer &layer = _layers[layerIdx];
	delete layer.surface;
	layer.surface = unpackImh4bpp(imhData, layer.dx, layer.dy);
	layer.active  = true;
	layer.opaque  = opaque;
	layer.left    = left;
	layer.top     = top;
}

void SpriteChain::clearSprite(int layerIdx) {
	if (layerIdx < 0 || layerIdx >= kNumLayers)
		return;
	SpriteLayer &layer = _layers[layerIdx];
	delete layer.surface;
	layer.surface = nullptr;
	layer.active = false;
}

void SpriteChain::clearAll() {
	for (int i = 0; i < kNumLayers; i++)
		clearSprite(i);
}

void SpriteChain::renderToScreen() {
	_frame.fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);

	// Back-to-front: highest index drawn first so lower indices overlay it.
	for (int i = kNumLayers - 1; i >= 0; i--) {
		const SpriteLayer &layer = _layers[i];
		if (!layer.active || !layer.surface)
			continue;

		Common::Point dest(layer.left - layer.dx, layer.top - layer.dy);
		if (layer.opaque)
			_frame.blitFrom(*layer.surface, dest);
		else
			_frame.transBlitFrom(*layer.surface, dest, kTransparentKey);
	}

	g_system->copyRectToScreen(_frame.getPixels(), _frame.pitch,
	                           0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
}

} // End of namespace Neuromancer
