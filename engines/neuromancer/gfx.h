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

#ifndef NEUROMANCER_GFX_H
#define NEUROMANCER_GFX_H

#include "neuromancer/decompress.h"  // ImhHeader

#include "common/scummsys.h"
#include "graphics/managed_surface.h"

namespace Neuromancer {

// DOS screen dimensions.
enum {
	kScreenWidth  = 320,
	kScreenHeight = 200,
	kNumLayers    = 11
};

// Sprite layer identities used throughout the original engine. Preserve the
// numeric ordering: SpriteChain composites back-to-front (index 10 first),
// so lower indices render on top.
enum SpriteLayerIndex {
	kLayerCursor         = 0,
	kLayerDebugOverlay   = 1, // engine-added debug text (e.g. level indicator)
	kLayerNeuroMenu      = 2,
	kLayerDialogBubble   = 3,
	kLayerCharacter      = 4,
	kLayerPaxWindow      = 5, // PAX terminal panel (covers level BG while active)
	kLayerStatusWidget   = 8, // cash / CON / time / date readout inside the
	                          // UI panel. Lower in the stack than popups so
	                          // the Inventory / PAX windows cover it cleanly.
	kLayerLevelBg        = 9,
	kLayerBackground     = 10
};

// Initialize the 16-colour EGA palette on the ScummVM screen.
// Indices 0..15 get the EGA table; 16..255 remain black.
void setEgaPalette();

// Unpack a 4bpp-packed IMH buffer into a newly-allocated 8bpp indexed
// ManagedSurface. Each source byte encodes two pixels (high nibble = left
// pixel). `dx` and `dy` are preserved via the out-parameters, since they
// are per-sprite render offsets not captured by the Surface itself.
//
// Caller owns the returned surface.
Graphics::ManagedSurface *unpackImh4bpp(const byte *imh, int16 &dxOut, int16 &dyOut);

// A single composition layer.
struct SpriteLayer {
	bool active;
	bool opaque;
	byte transKey; // palette index used as the transparency key when !opaque
	int left;      // screen-space top-left origin
	int top;
	int16 dx;      // from the IMH header -- subtracted from (left, top)
	int16 dy;
	Graphics::ManagedSurface *surface; // owned; 8bpp indexed; nullptr when inactive

	SpriteLayer()
		: active(false), opaque(false), transKey(0),
		  left(0), top(0), dx(0), dy(0), surface(nullptr) {}
};

// Manages the 11 composition layers and pushes composited frames to the
// screen. Each layer caches an unpacked 8bpp ManagedSurface; compositing
// uses ScummVM's standard blitFrom/transBlitFrom primitives into an
// off-screen ManagedSurface, then a single copyRectToScreen push.
class SpriteChain {
public:
	SpriteChain();
	~SpriteChain();

	// Unpacks `imhData` into a surface owned by this layer. Any previous
	// surface on the layer is freed first. `transKey` is used as the
	// transparent colour index when `opaque` is false; defaults to 0
	// (matching the DOS sprite chain's "black passes through" convention
	// for partial-cover layers like the mouse cursor).
	void addSprite(int layerIdx, int left, int top, const byte *imhData,
	               bool opaque, byte transKey = 0);
	void clearSprite(int layerIdx);
	void clearAll();

	// Composite every active layer (back-to-front) onto the internal
	// frame surface and push it to the screen.
	void renderToScreen();

	SpriteLayer &layer(int idx) { return _layers[idx]; }

private:
	SpriteLayer _layers[kNumLayers];
	Graphics::ManagedSurface _frame;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_GFX_H
