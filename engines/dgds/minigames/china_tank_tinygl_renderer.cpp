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
#include "graphics/surface.h"

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

const bool kTankFlipTinyGLCopyY = false;

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

void ChinaTankTinyGLRenderer::drawPolyline(const Common::Array<Math::Vector3d> &vertices, byte color, const DgdsPal &palette, bool closed) {
#if defined(USE_TINYGL)
	if (vertices.size() < 2)
		return;

	byte r, g, b;
	palette.get(color, r, g, b);
	tglColor4ub(r, g, b, 255);

	tglBegin(closed ? TGL_LINE_LOOP : TGL_LINE_STRIP);
	for (const Math::Vector3d &vertex : vertices)
		tglVertex3f(vertex.x(), vertex.y(), vertex.z());
	tglEnd();
#endif
}

void ChinaTankTinyGLRenderer::drawBillboards(const Common::Array<ChinaTankBillboard> &billboards, const Graphics::Surface &texture) {
#if defined(USE_TINYGL)
	if (billboards.empty() || !texture.getPixels())
		return;
	if (texture.format != Graphics::PixelFormat::createFormatRGBA32()) {
		warning("Tank tinygl billboard expected an RGBA32 texture");
		return;
	}

	TGLuint textureId = 0;
	tglGenTextures(1, &textureId);
	tglBindTexture(TGL_TEXTURE_2D, textureId);
	tglTexImage2D(TGL_TEXTURE_2D, 0, TGL_RGBA, texture.w, texture.h, 0, TGL_RGBA, TGL_UNSIGNED_BYTE, texture.getPixels());
	tglTexParameteri(TGL_TEXTURE_2D, TGL_TEXTURE_MIN_FILTER, TGL_NEAREST);
	tglTexParameteri(TGL_TEXTURE_2D, TGL_TEXTURE_MAG_FILTER, TGL_NEAREST);
	tglTexParameteri(TGL_TEXTURE_2D, TGL_TEXTURE_WRAP_S, TGL_CLAMP_TO_EDGE);
	tglTexParameteri(TGL_TEXTURE_2D, TGL_TEXTURE_WRAP_T, TGL_CLAMP_TO_EDGE);

	tglEnable(TGL_TEXTURE_2D);
	tglTexEnvi(TGL_TEXTURE_ENV, TGL_TEXTURE_ENV_MODE, TGL_REPLACE);
	tglEnable(TGL_ALPHA_TEST);
	tglAlphaFunc(TGL_GREATER, 0.0f);
	tglColor4ub(255, 255, 255, 255);

	for (const ChinaTankBillboard &billboard : billboards)
		drawBillboardGeometry(billboard);

	tglDisable(TGL_ALPHA_TEST);
	tglTexEnvi(TGL_TEXTURE_ENV, TGL_TEXTURE_ENV_MODE, TGL_MODULATE);
	tglBindTexture(TGL_TEXTURE_2D, 0);
	tglDisable(TGL_TEXTURE_2D);
	tglDeleteTextures(1, &textureId);
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
	// TinyGL's render target is bottom-left oriented. Flip the projection once
	// here so the copy path can stay a straight viewport blit.
	tglScalef(-1.0f, 1.0f, 1.0f);
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

void ChinaTankTinyGLRenderer::drawBillboardGeometry(const ChinaTankBillboard &billboard) {
#if defined(USE_TINYGL)
	if (billboard.width <= 0.0f || billboard.height <= 0.0f)
		return;

	tglMatrixMode(TGL_MODELVIEW);
	tglPushMatrix();
	tglTranslatef(billboard.position.x(), billboard.position.y(), billboard.position.z());

	TGLfloat modelView[16];
	tglGetFloatv(TGL_MODELVIEW_MATRIX, modelView);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++)
			modelView[i * 4 + j] = (i == j) ? 1.0f : 0.0f;
	}
	tglLoadMatrixf(modelView);

	const float halfWidth = billboard.width * 0.5f;
	tglBegin(TGL_QUADS);
		tglTexCoord2f(0.0f, 1.0f); tglVertex3f(-halfWidth, 0.0f, 0.0f);
		tglTexCoord2f(1.0f, 1.0f); tglVertex3f( halfWidth, 0.0f, 0.0f);
		tglTexCoord2f(1.0f, 0.0f); tglVertex3f( halfWidth, billboard.height, 0.0f);
		tglTexCoord2f(0.0f, 0.0f); tglVertex3f(-halfWidth, billboard.height, 0.0f);
	tglEnd();

	tglPopMatrix();
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
