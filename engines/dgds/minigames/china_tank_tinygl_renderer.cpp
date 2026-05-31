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

#include "common/debug.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/system.h"

#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

#include "dgds/dgds.h"
#include "dgds/game_palettes.h"
#include "dgds/includes.h"
#include "dgds/minigames/china_tank_tinygl_renderer.h"

#if defined(USE_TINYGL)
#include "graphics/tinygl/tinygl.h"
#include "math/glmath.h"
#endif

namespace Dgds {

namespace {

const bool kTankFlipTinyGLCopyY = true;

} // End of anonymous namespace

ChinaTankTinyGLRenderer::ChinaTankTinyGLRenderer() : _initialized(false) {
}

ChinaTankTinyGLRenderer::~ChinaTankTinyGLRenderer() {
#if defined(USE_TINYGL)
	if (_initialized)
		TinyGL::destroyContext();
#endif
}

bool ChinaTankTinyGLRenderer::isAvailable() {
#if defined(USE_TINYGL)
	return true;
#else
	return false;
#endif
}

void ChinaTankTinyGLRenderer::beginFrame(const Common::Rect &viewport, const DgdsPal &palette, byte clearColor,
		const Math::Vector3d &camera, const Math::Vector3d &interest, float fov, float nearClip, float farClip) {
#if defined(USE_TINYGL)
	init();
	setViewport(viewport);
	updateProjectionMatrix(viewport, fov, nearClip, farClip);
	clearViewport(palette, clearColor);
	positionCamera(camera, interest);
#endif
}

void ChinaTankTinyGLRenderer::drawPolygon(const Common::Array<Math::Vector3d> &vertices, byte color, const DgdsPal &palette) {
#if defined(USE_TINYGL)
	if (vertices.size() < 3)
		return;

	byte r, g, b;
	palette.get(color, r, g, b);
	tglColor4ub(r, g, b, 255);

	tglBegin(TGL_POLYGON);
	for (const Math::Vector3d &vertex : vertices)
		tglVertex3f(vertex.x(), vertex.y(), vertex.z());
	tglEnd();
#endif
}

void ChinaTankTinyGLRenderer::endFrame(Graphics::ManagedSurface &dst, const Common::Rect &viewport, const DgdsPal &palette) {
#if defined(USE_TINYGL)
	copyTinyGLToSurface(dst, viewport, palette);
#endif
}

void ChinaTankTinyGLRenderer::init() {
#if defined(USE_TINYGL)
	if (_initialized)
		return;

	TinyGL::createContext(SCREEN_WIDTH, SCREEN_HEIGHT, Graphics::PixelFormat::createFormatRGBA32(), 512, true, true);

	tglDisable(TGL_LIGHTING);
	tglDisable(TGL_TEXTURE_2D);
	tglDisable(TGL_CULL_FACE);
	tglEnable(TGL_DEPTH_TEST);
	tglDepthFunc(TGL_LEQUAL);
	tglEnable(TGL_SCISSOR_TEST);
	tglShadeModel(TGL_FLAT);

	_initialized = true;
#endif
}

void ChinaTankTinyGLRenderer::setViewport(const Common::Rect &viewport) {
#if defined(USE_TINYGL)
	tglViewport(viewport.left, SCREEN_HEIGHT - viewport.bottom, viewport.width(), viewport.height());
	tglScissor(viewport.left, SCREEN_HEIGHT - viewport.bottom, viewport.width(), viewport.height());
#endif
}

void ChinaTankTinyGLRenderer::updateProjectionMatrix(const Common::Rect &viewport, float fov, float nearClip, float farClip) {
#if defined(USE_TINYGL)
	const float aspect = (float)viewport.width() / (float)viewport.height();
	const float ymax = nearClip * tanf((float)(fov * M_PI / 360.0));
	const float xmax = ymax * aspect;

	tglMatrixMode(TGL_PROJECTION);
	tglLoadIdentity();
	tglFrustum(-xmax, xmax, -ymax, ymax, nearClip, farClip);
#endif
}

void ChinaTankTinyGLRenderer::clearViewport(const DgdsPal &palette, byte color) {
#if defined(USE_TINYGL)
	byte r, g, b;
	palette.get(color, r, g, b);
	tglClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
	tglClear(TGL_COLOR_BUFFER_BIT | TGL_DEPTH_BUFFER_BIT | TGL_STENCIL_BUFFER_BIT);
#endif
}

void ChinaTankTinyGLRenderer::positionCamera(const Math::Vector3d &camera, const Math::Vector3d &interest) {
#if defined(USE_TINYGL)
	tglMatrixMode(TGL_MODELVIEW);
	tglLoadIdentity();

	Math::Matrix4 lookMatrix = Math::makeLookAtMatrix(camera, interest, Math::Vector3d(0.0f, 1.0f, 0.0f));
	tglMultMatrixf(lookMatrix.getData());
	tglTranslatef(-camera.x(), -camera.y(), -camera.z());
#endif
}

byte ChinaTankTinyGLRenderer::mapTinyGLPixelToPalette(uint32 pixel, const Graphics::PixelFormat &format, const DgdsPal &palette) const {
	byte r, g, b;
	format.colorToRGB(pixel, r, g, b);

	int bestColor = 0;
	int bestDistance = 0x7fffffff;
	for (int i = 0; i < 256; i++) {
		byte palR, palG, palB;
		palette.get(i, palR, palG, palB);

		const int dr = (int)r - palR;
		const int dg = (int)g - palG;
		const int db = (int)b - palB;
		const int distance = dr * dr + dg * dg + db * db;
		if (distance == 0)
			return i;
		if (distance < bestDistance) {
			bestDistance = distance;
			bestColor = i;
		}
	}

	return bestColor;
}

void ChinaTankTinyGLRenderer::copyTinyGLToSurface(Graphics::ManagedSurface &dst, const Common::Rect &viewport, const DgdsPal &palette) {
#if defined(USE_TINYGL)
	Common::List<Common::Rect> dirtyAreas;
	TinyGL::presentBuffer(dirtyAreas);

	Graphics::Surface glBuffer;
	TinyGL::getSurfaceRef(glBuffer);
	if (dst.format.bytesPerPixel != 1) {
		warning("Tank tinygl renderer expected an 8-bit target surface, got %d bytes per pixel", dst.format.bytesPerPixel);
		return;
	}

	Common::HashMap<uint32, byte> paletteCache;
	for (int y = viewport.top; y < viewport.bottom; y++) {
		byte *dstRow = (byte *)dst.getBasePtr(viewport.left, y);
		const int srcY = kTankFlipTinyGLCopyY ? viewport.bottom - 1 - (y - viewport.top) : y;
		for (int x = viewport.left; x < viewport.right; x++) {
			const uint32 pixel = glBuffer.getPixel(x, srcY);
			if (!paletteCache.contains(pixel))
				paletteCache[pixel] = mapTinyGLPixelToPalette(pixel, glBuffer.format, palette);
			dstRow[x - viewport.left] = paletteCache[pixel];
		}
	}
#endif
}

} // End of namespace Dgds
