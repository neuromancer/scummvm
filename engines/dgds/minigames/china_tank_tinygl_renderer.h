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

#ifndef DGDS_MINIGAMES_CHINA_TANK_TINYGL_RENDERER_H
#define DGDS_MINIGAMES_CHINA_TANK_TINYGL_RENDERER_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

#include "math/vector3d.h"

namespace Graphics {
class ManagedSurface;
struct PixelFormat;
struct Surface;
}

namespace Dgds {

class DgdsPal;

struct ChinaTankBillboard {
	Math::Vector3d position;
	float width;
	float height;

	ChinaTankBillboard() : position(0.0f, 0.0f, 0.0f), width(0.0f), height(0.0f) {}
};

class ChinaTankTinyGLRenderer {
public:
	ChinaTankTinyGLRenderer();
	~ChinaTankTinyGLRenderer();

	static bool isAvailable();

	void beginFrame(const Common::Rect &viewport, const DgdsPal &palette, byte skyColor, byte groundColor, const Math::Vector3d &camera,
			const Math::Vector3d &interest, float fov, float nearClip, float farClip);
	void drawPolygon(const Common::Array<Math::Vector3d> &vertices, byte color, const DgdsPal &palette);
	void drawPolyline(const Common::Array<Math::Vector3d> &vertices, byte color, const DgdsPal &palette, bool closed);
	void drawBillboards(const Common::Array<ChinaTankBillboard> &billboards, const Graphics::Surface &texture);
	void endFrame(Graphics::ManagedSurface &dst, const Common::Rect &viewport, const DgdsPal &palette);

private:
	bool _initialized;
	byte _skyColor;
	byte _groundColor;
	int _horizonY;

	void init();
	void setViewport(const Common::Rect &viewport);
	void updateProjectionMatrix(const Common::Rect &viewport, float fov, float nearClip, float farClip);
	void updateHorizon(const Common::Rect &viewport, const Math::Vector3d &camera, const Math::Vector3d &interest, float fov);
	void clearViewport(const DgdsPal &palette);
	void positionCamera(const Math::Vector3d &camera, const Math::Vector3d &interest);
	void drawBillboardGeometry(const ChinaTankBillboard &billboard);
	byte mapTinyGLPixelToPalette(uint32 pixel, const Graphics::PixelFormat &format, const DgdsPal &palette) const;
	void copyTinyGLToSurface(Graphics::ManagedSurface &dst, const Common::Rect &viewport, const DgdsPal &palette);
};

} // End of namespace Dgds

#endif // DGDS_MINIGAMES_CHINA_TANK_TINYGL_RENDERER_H
