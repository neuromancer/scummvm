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

#include "common/array.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/stream.h"
#include "common/system.h"

#include "dgds/dgds.h"
#include "dgds/game_palettes.h"
#include "dgds/image.h"
#include "dgds/includes.h"
#include "dgds/minigames/china_tank.h"
#include "dgds/minigames/china_tank_tinygl_renderer.h"
#include "dgds/resource.h"

#include "graphics/managed_surface.h"

#include "math/vector3d.h"

namespace Dgds {

namespace {

const int kTankShapeCount = 77;
const int kTankWorldRecordSize = 20;
const int kTankViewLeft = 64;
const int kTankViewTop = 21;
const int kTankViewRight = 256;
const int kTankViewBottom = 105;
const int kTankLowSpeed = 480;
const int kTankHighSpeed = kTankLowSpeed + kTankLowSpeed;
const int kTankAccelSpeed = 40;
const int kTankTurnSpeed = 1024;
const int kTankInitialFuel = 34000;
const int kTankGZero = 9600;
const int kTankInitialX = 9412;
const int kTankInitialY = -60783;
const int kTankInitialZ = 23400 + kTankGZero;
const uint16 kTankInitialHeading = 0x4400;
const char *const kTankCockpitScreen = "ftank3s.scr";
const char *const kTankDevCockpitScreen = "tankcp.scr";
const char *const kTankCockpitShapes = "cpit.bmp";
const float kTankWorldScale = 1.0f / 1000.0f;
const float kTankMouseSensitivity = 0.006f;
const float kTankMaxPitch = 1.45f;

enum TankCellTag {
	kTankCellGroup = 0,
	kTankCellSphere = 1,
	kTankCellBitmap = 2,
	kTankCellCenteredBitmap = 3
};

enum TankPolygonVisibility {
	kTankPolygonAlways = 0,
	kTankPolygonNormal = 1,
	kTankPolygonNever = 2
};

enum TankGearPosition {
	kTankGearNeutral = 0,
	kTankGearLow = 1,
	kTankGearHigh = 2
};

int16 readSint16(const byte *data) {
	return (int16)READ_LE_UINT16(data);
}

int32 readSint32(const byte *data) {
	return (int32)READ_LE_UINT32(data);
}

float tankAngleToRadians(uint16 angle) {
	return (float)((double)angle * 2.0 * M_PI / 65536.0);
}

uint16 tankWrapAngle(int angle) {
	return (uint16)((uint32)angle & 0xffff);
}

int tankMouseDeltaToAngle(int delta) {
	return (int)((double)delta * kTankMouseSensitivity * 65536.0 / (2.0 * M_PI));
}

int tankDialCos(uint16 angle) {
	return (int)(cosf(tankAngleToRadians(angle)) * 16.0f);
}

int tankDialSin(uint16 angle) {
	return (int)(sinf(tankAngleToRadians(angle)) * 16.0f);
}

Common::Rect getTankViewport() {
	return Common::Rect(kTankViewLeft, kTankViewTop, kTankViewRight, kTankViewBottom);
}

struct TankVec3 {
	int32 x;
	int32 y;
	int32 z;
};

struct TankPolygon {
	byte flags;
	byte color;
	byte normal;
	Common::Array<byte> vertices;
};

struct TankPart {
	Common::Array<TankVec3> points;
	Common::Array<TankPolygon> polygons;
};

struct TankShape {
	byte scale;
	int16 radius;
	Common::Array<TankPart> parts;
};

struct TankObject {
	int16 shape;
	int16 rotX;
	int16 rotY;
	int16 rotZ;
	TankVec3 loc;
};

Common::Array<byte> readResourceBytes(ResourceManager *resource, const Common::String &name) {
	Common::ScopedPtr<Common::SeekableReadStream> stream(resource->getResource(name));
	if (!stream)
		return Common::Array<byte>();

	const int64 size = stream->size();
	if (size < 0 || size > 0xffffffff) {
		warning("Tank resource %s has invalid size %" PRId64, name.c_str(), size);
		return Common::Array<byte>();
	}

	Common::Array<byte> data;
	data.resize((uint32)size);
	if (!data.empty() && stream->read(&data[0], data.size()) != data.size()) {
		warning("Could not read tank resource %s", name.c_str());
		return Common::Array<byte>();
	}
	return data;
}

} // End of anonymous namespace

struct ChinaTank::TankScene {
	Common::Array<TankShape> _shapes;
	Common::Array<TankObject> _objects;
	ChinaTankTinyGLRenderer _renderer;
	Common::SharedPtr<Image> _cockpitShapes;
	Graphics::ManagedSurface _cockpitBackground;
	bool _loaded;
	bool _cockpitBackgroundLoaded;
	TankVec3 _tankLoc;
	uint16 _tankHeading;
	float _pitch;
	int _tankSpeed;
	int _fuel;
	TankGearPosition _gearPosition;
	bool _turnLeft;
	bool _turnRight;
	bool _gearUpPressed;
	bool _gearDownPressed;

	TankScene() : _loaded(false),
		_cockpitBackgroundLoaded(false),
		_tankHeading(kTankInitialHeading),
		_pitch(0.0f),
		_tankSpeed(0),
		_fuel(kTankInitialFuel),
		_gearPosition(kTankGearNeutral),
		_turnLeft(false),
		_turnRight(false),
		_gearUpPressed(false),
		_gearDownPressed(false)
	{
		_tankLoc.x = kTankInitialX;
		_tankLoc.y = kTankInitialY;
		_tankLoc.z = kTankInitialZ;
	}

	bool load(ResourceManager *resource, Decompressor *decompressor) {
		Common::Array<byte> tbl = readResourceBytes(resource, "tank.tbl");
		Common::Array<byte> wld = readResourceBytes(resource, "tank.wld");
		if (tbl.empty() || wld.empty()) {
			warning("Could not load tank 3space files");
			return false;
		}

		_shapes.clear();
		_shapes.resize(kTankShapeCount);

		if (tbl.size() < kTankShapeCount * 4) {
			warning("Tank shape table is too small");
			return false;
		}

		for (int i = 0; i < kTankShapeCount; i++) {
			uint16 offset = READ_LE_UINT16(&tbl[i * 4]);
			uint16 segment = READ_LE_UINT16(&tbl[i * 4 + 2]);
			if (!offset && !segment)
				continue;
			if (!loadShape(tbl, i, segment, offset))
				return false;
		}

		if (wld.size() % kTankWorldRecordSize) {
			warning("Tank world file has invalid size %d", (int)wld.size());
			return false;
		}

		_objects.clear();
		for (uint32 pos = 0; pos < wld.size(); pos += kTankWorldRecordSize) {
			TankObject obj;
			obj.shape = readSint16(&wld[pos]);
			obj.rotX = readSint16(&wld[pos + 2]);
			obj.rotY = readSint16(&wld[pos + 4]);
			obj.rotZ = readSint16(&wld[pos + 6]);
			obj.loc.x = readSint32(&wld[pos + 8]);
			obj.loc.y = readSint32(&wld[pos + 12]);
			obj.loc.z = readSint32(&wld[pos + 16]);
			if (obj.shape < 0 || obj.shape >= kTankShapeCount) {
				warning("Tank world object has invalid shape %d", obj.shape);
				return false;
			}
			_objects.push_back(obj);
		}

		_loaded = true;
		loadCockpit(resource, decompressor);
		return true;
	}

	void turnCamera(int dx, int dy) {
		_tankHeading = tankWrapAngle(_tankHeading + tankMouseDeltaToAngle(dx));
		_pitch += dy * kTankMouseSensitivity;

		if (_pitch < -kTankMaxPitch)
			_pitch = -kTankMaxPitch;
		else if (_pitch > kTankMaxPitch)
			_pitch = kTankMaxPitch;
	}

	void setMoveKey(Common::KeyCode keycode, bool pressed) {
		switch (keycode) {
		case Common::KEYCODE_KP7:
			setGearUpPressed(pressed);
			_turnLeft = pressed;
			break;
		case Common::KEYCODE_KP9:
			setGearUpPressed(pressed);
			_turnRight = pressed;
			break;
		case Common::KEYCODE_KP1:
			setGearDownPressed(pressed);
			_turnLeft = pressed;
			break;
		case Common::KEYCODE_KP3:
			setGearDownPressed(pressed);
			_turnRight = pressed;
			break;
		case Common::KEYCODE_w:
		case Common::KEYCODE_UP:
		case Common::KEYCODE_KP8:
			setGearUpPressed(pressed);
			break;
		case Common::KEYCODE_s:
		case Common::KEYCODE_DOWN:
		case Common::KEYCODE_KP2:
			setGearDownPressed(pressed);
			break;
		case Common::KEYCODE_d:
		case Common::KEYCODE_RIGHT:
		case Common::KEYCODE_KP6:
			_turnRight = pressed;
			break;
		case Common::KEYCODE_a:
		case Common::KEYCODE_LEFT:
		case Common::KEYCODE_KP4:
			_turnLeft = pressed;
			break;
		case Common::KEYCODE_RETURN:
		case Common::KEYCODE_KP5:
		case Common::KEYCODE_KP_ENTER:
			if (pressed)
				_gearPosition = kTankGearNeutral;
			break;
		default:
			break;
		}
	}

	void setGearUpPressed(bool pressed) {
		if (pressed && !_gearUpPressed && _gearPosition != kTankGearHigh)
			_gearPosition = (TankGearPosition)(_gearPosition + 1);
		_gearUpPressed = pressed;
	}

	void setGearDownPressed(bool pressed) {
		if (pressed && !_gearDownPressed && _gearPosition != kTankGearNeutral)
			_gearPosition = (TankGearPosition)(_gearPosition - 1);
		_gearDownPressed = pressed;
	}

	void updatePlayerMovement() {
		if (_fuel <= 0)
			_gearPosition = kTankGearNeutral;

		if (_turnLeft)
			_tankHeading = tankWrapAngle(_tankHeading + kTankTurnSpeed);
		else if (_turnRight)
			_tankHeading = tankWrapAngle(_tankHeading - kTankTurnSpeed);

		updateTankSpeed();
		movePlayerTank();
		updateFuel();
	}

	void updateTankSpeed() {
		if (_gearPosition == kTankGearHigh) {
			if (_tankSpeed < kTankHighSpeed)
				_tankSpeed += kTankAccelSpeed;
		} else if (_gearPosition == kTankGearLow) {
			if (_tankSpeed < kTankLowSpeed)
				_tankSpeed += kTankAccelSpeed;
			else if (_tankSpeed > kTankLowSpeed)
				_tankSpeed -= kTankAccelSpeed;
		} else {
			for (int i = 0; i < 3 && _tankSpeed > 0; i++)
				_tankSpeed -= kTankAccelSpeed;
			if (_tankSpeed < 0)
				_tankSpeed = 0;
		}
	}

	void movePlayerTank() {
		if (_tankSpeed == 0)
			return;

		const float angle = tankAngleToRadians(_tankHeading);
		_tankLoc.x += (int32)(-_tankSpeed * sinf(angle));
		_tankLoc.y += (int32)(_tankSpeed * cosf(angle));
	}

	void updateFuel() {
		if (_fuel < 0)
			return;

		_fuel -= 2 + (_tankSpeed >> 6);
		if (_fuel < 0)
			_gearPosition = kTankGearNeutral;
	}

	void loadCockpit(ResourceManager *resource, Decompressor *decompressor) {
		Common::String cockpitScreenName;
		if (resource->hasResource(kTankCockpitScreen))
			cockpitScreenName = kTankCockpitScreen;
		else if (resource->hasResource(kTankDevCockpitScreen))
			cockpitScreenName = kTankDevCockpitScreen;

		const bool hasCockpitScreen = !cockpitScreenName.empty();
		const bool hasCockpitShapes = resource->hasResource(kTankCockpitShapes);
		if (!hasCockpitScreen)
			warning("Tank cockpit UI resource missing: %s", kTankCockpitScreen);
		if (!hasCockpitShapes)
			warning("Tank cockpit UI resource missing: %s", kTankCockpitShapes);

		if (hasCockpitScreen) {
			Image cockpitScreen(resource, decompressor);
			_cockpitBackground.create(SCREEN_WIDTH, SCREEN_HEIGHT, Graphics::PixelFormat::createFormatCLUT8());
			cockpitScreen.drawScreen(cockpitScreenName, _cockpitBackground);
			_cockpitBackgroundLoaded = true;
		}

		if (!hasCockpitShapes)
			return;
		_cockpitShapes.reset(new Image(resource, decompressor));
		_cockpitShapes->loadBitmap(kTankCockpitShapes);
		if (_cockpitShapes->loadedFrameCount() < 4) {
			warning("Tank cockpit expected at least 4 %s frames, got %d", kTankCockpitShapes, _cockpitShapes->loadedFrameCount());
			_cockpitShapes.reset();
			return;
		}
	}

	bool loadShape(const Common::Array<byte> &tbl, int shapeIndex, uint16 segment, uint16 offset) {
		uint32 base = (uint32)segment << 4;
		uint32 pos = base + offset;
		if (!checkRange(tbl, pos, 16, "shape"))
			return false;

		TankShape &shape = _shapes[shapeIndex];
		shape.scale = tbl[pos];
		shape.radius = readSint16(&tbl[pos + 4]);

		int16 numDetails = readSint16(&tbl[pos + 12]);
		uint16 detailOffset = READ_LE_UINT16(&tbl[pos + 14]);
		if (numDetails < 0) {
			warning("Tank shape %d has invalid detail count %d", shapeIndex, numDetails);
			return false;
		}
		if (numDetails == 0)
			return true;

		uint32 detailPos = base + detailOffset;
		if (!checkRange(tbl, detailPos, numDetails * 6, "details"))
			return false;

		// The first detail is the highest-detail representation used by the original
		// unless distance-based shape sizing selects a cheaper one.
		byte numParts = tbl[detailPos + 3];
		uint16 partOffset = READ_LE_UINT16(&tbl[detailPos + 4]);
		uint32 partPos = base + partOffset;
		if (!checkRange(tbl, partPos, numParts * 10, "parts"))
			return false;

		for (int i = 0; i < numParts; i++) {
			TankPart part;
			if (!loadPart(tbl, base, partPos + i * 10, part))
				return false;
			shape.parts.push_back(part);
		}

		return true;
	}

	bool loadPart(const Common::Array<byte> &tbl, uint32 base, uint32 pos, TankPart &part) {
		byte numPoints = tbl[pos + 3];
		uint16 pointOffset = READ_LE_UINT16(&tbl[pos + 4]);
		int16 numCells = readSint16(&tbl[pos + 6]);
		uint16 cellOffset = READ_LE_UINT16(&tbl[pos + 8]);
		if (numCells < 0) {
			warning("Tank part has invalid cell count %d", numCells);
			return false;
		}

		uint32 pointPos = base + pointOffset;
		if (!checkRange(tbl, pointPos, numPoints * 6, "points"))
			return false;

		for (int i = 0; i < numPoints; i++) {
			TankVec3 point;
			point.x = readSint16(&tbl[pointPos + i * 6]);
			point.y = readSint16(&tbl[pointPos + i * 6 + 2]);
			point.z = readSint16(&tbl[pointPos + i * 6 + 4]);
			part.points.push_back(point);
		}

		uint32 cellPos = base + cellOffset;
		if (!checkRange(tbl, cellPos, numCells * 7, "cells"))
			return false;

		for (int i = 0; i < numCells; i++) {
			uint32 cell = cellPos + i * 7;
			byte tag = tbl[cell];
			if (tag != kTankCellGroup)
				continue;

			int16 numPolys = readSint16(&tbl[cell + 1]);
			if (numPolys < 0) {
				warning("Tank group cell has invalid polygon count %d", numPolys);
				return false;
			}
			uint16 polyOffset = READ_LE_UINT16(&tbl[cell + 3]);
			if (!loadPolygons(tbl, base, polyOffset, numPolys, part))
				return false;
		}

		return true;
	}

	bool loadPolygons(const Common::Array<byte> &tbl, uint32 base, uint16 offset, int16 count, TankPart &part) {
		uint32 pos = base + offset;
		if (!checkRange(tbl, pos, count * 8, "polygons"))
			return false;

		for (int i = 0; i < count; i++) {
			uint32 polyPos = pos + i * 8;
			TankPolygon poly;
			poly.flags = tbl[polyPos];
			poly.color = tbl[polyPos + 2];
			poly.normal = tbl[polyPos + 5];
			if ((poly.flags & 0x03) == kTankPolygonNormal && poly.normal >= part.points.size()) {
				warning("Tank polygon references normal %d outside %d points", poly.normal, (int)part.points.size());
				return false;
			}

			uint16 vlistOffset = READ_LE_UINT16(&tbl[polyPos + 6]);
			uint32 vlistPos = base + vlistOffset;
			if (!checkRange(tbl, vlistPos, 1, "vertex list"))
				return false;

			bool terminated = false;
			for (int j = 0; j < 256; j++) {
				if (!checkRange(tbl, vlistPos + j, 1, "vertex list"))
					return false;

				byte vertex = tbl[vlistPos + j];
				if (vertex == 0xff) {
					terminated = true;
					break;
				}
				if (vertex >= part.points.size()) {
					warning("Tank polygon references vertex %d outside %d points", vertex, (int)part.points.size());
					return false;
				}
				poly.vertices.push_back(vertex);
			}

			if (!terminated) {
				warning("Tank polygon has unterminated vertex list at 0x%x", vlistPos);
				return false;
			}

			if (poly.vertices.size() >= 3)
				part.polygons.push_back(poly);
		}

		return true;
	}

	bool checkRange(const Common::Array<byte> &data, uint32 pos, uint32 size, const char *what) const {
		if (pos > data.size() || size > data.size() - pos) {
			warning("Tank %s data outside resource: pos 0x%x size %u resource %u", what, pos, size, (uint)data.size());
			return false;
		}
		return true;
	}

	void render(Graphics::ManagedSurface &dst, const DgdsPal &palette) {
		if (!_loaded || !ChinaTankTinyGLRenderer::isAvailable())
			return;

		updatePlayerMovement();

		const Common::Rect viewport = getTankViewport();
		drawCockpitBackground(dst);
		_renderer.beginFrame(viewport, palette, 248, cameraPosition(), cameraInterest(), 70.0f, 0.10f, 1200.0f);

		for (const TankObject &object : _objects)
			renderObject(object, palette);

		_renderer.endFrame(dst, viewport, palette);
		drawCockpit(dst);
	}

	void renderObject(const TankObject &object, const DgdsPal &palette) {
		if (object.shape < 0 || object.shape >= (int)_shapes.size())
			return;

		const TankShape &shape = _shapes[object.shape];
		if (shape.parts.empty())
			return;

		for (const TankPart &part : shape.parts) {
			for (const TankPolygon &polygon : part.polygons) {
				if (!isPolygonVisible(part, polygon, object, shape.scale))
					continue;

				Common::Array<Math::Vector3d> vertices;
				for (byte vertexIndex : polygon.vertices) {
					const TankVec3 &point = part.points[vertexIndex];
					vertices.push_back(transformPoint(point, object, shape.scale));
				}
				_renderer.drawPolygon(vertices, polygon.color, palette);
			}
		}
	}

	Math::Vector3d cameraPosition() const {
		return Math::Vector3d(
			_tankLoc.x * kTankWorldScale,
			_tankLoc.z * kTankWorldScale,
			_tankLoc.y * kTankWorldScale
		);
	}

	Math::Vector3d cameraInterest() const {
		const float yaw = tankAngleToRadians(_tankHeading);
		const float pitchScale = cosf(_pitch);
		return Math::Vector3d(
			_tankLoc.x * kTankWorldScale + -sinf(yaw) * pitchScale * 100.0f,
			_tankLoc.z * kTankWorldScale + sinf(_pitch) * 100.0f,
			_tankLoc.y * kTankWorldScale + cosf(yaw) * pitchScale * 100.0f
		);
	}

	void drawCockpitBackground(Graphics::ManagedSurface &dst) {
		if (_cockpitBackgroundLoaded) {
			dst.blitFrom(_cockpitBackground);
			return;
		}

		const Common::Rect screen(SCREEN_WIDTH, SCREEN_HEIGHT);
		const Common::Rect viewport = getTankViewport();
		dst.fillRect(screen, 0);
		dst.fillRect(Common::Rect(0, 0, SCREEN_WIDTH, viewport.top), 248);
		dst.fillRect(Common::Rect(0, viewport.bottom, SCREEN_WIDTH, SCREEN_HEIGHT), 0);
		dst.fillRect(Common::Rect(0, viewport.top, viewport.left, viewport.bottom), 0);
		dst.fillRect(Common::Rect(viewport.right, viewport.top, SCREEN_WIDTH, viewport.bottom), 0);
	}

	void drawCockpit(Graphics::ManagedSurface &dst) {
		drawSpeedDial(dst);
		drawHeadingDial(dst);
		drawFuelDial(dst);
		drawViewportBorder(dst);
	}

	void drawDial(Graphics::ManagedSurface &dst, int centerX, int centerY, int deltaX, int deltaY) {
		dst.drawLine(centerX, centerY, centerX + deltaX, centerY + deltaY, 7);
	}

	void drawCockpitShape(Graphics::ManagedSurface &dst, uint frame, int x, int y) {
		if (!_cockpitShapes)
			return;

		_cockpitShapes->drawBitmap(frame, x, y, Common::Rect(SCREEN_WIDTH, SCREEN_HEIGHT), dst);
	}

	void drawSpeedDial(Graphics::ManagedSurface &dst) {
		uint16 angle = _tankSpeed <= 0 ? 0 : (uint16)(_tankSpeed * 24);
		const int newX = tankDialCos(angle + 0x800);
		const int newY = tankDialSin(angle + 0x800);
		drawDial(dst, 118, 178, -newX, -newY);
		drawCockpitShape(dst, 3, 101, 174);
	}

	void drawHeadingDial(Graphics::ManagedSurface &dst) {
		const uint16 angle = _tankHeading + 0x4000;
		const int newX = tankDialCos(angle);
		const int newY = tankDialSin(angle);
		drawDial(dst, 319, 166, newX, -newY);
		drawCockpitShape(dst, 0, 316, 164);
		drawCockpitShape(dst, 1, 303, 179);
	}

	void drawFuelDial(Graphics::ManagedSurface &dst) {
		uint16 angle = (uint16)(_fuel + 200);
		angle = (angle >> 3) + 0x1400;
		const int newX = tankDialCos(angle);
		const int newY = tankDialSin(angle);
		drawDial(dst, 184, 178, -newX, -newY);
		drawCockpitShape(dst, 2, 168, 174);
	}

	void drawViewportBorder(Graphics::ManagedSurface &dst) {
		const Common::Rect viewport = getTankViewport();
		const int left = viewport.left - 1;
		const int top = viewport.top - 1;
		const int right = viewport.right + 1;
		const int bottom = viewport.bottom + 1;
		dst.drawLine(left, top, right, top, 0);
		dst.drawLine(right, top, right, bottom, 0);
		dst.drawLine(right, bottom, left, bottom, 0);
		dst.drawLine(left, bottom, left, top, 0);
	}

	bool isPolygonVisible(const TankPart &part, const TankPolygon &polygon, const TankObject &object, byte scale) const {
		switch (polygon.flags & 0x03) {
		case kTankPolygonAlways:
			return true;
		case kTankPolygonNever:
			return false;
		case kTankPolygonNormal:
			break;
		default:
			return true;
		}

		const Math::Vector3d normal = transformVector(part.points[polygon.normal], object, scale);
		const Math::Vector3d objectCenter(
			object.loc.x * kTankWorldScale,
			object.loc.z * kTankWorldScale,
			object.loc.y * kTankWorldScale
		);
		const Math::Vector3d view = cameraPosition() - objectCenter;
		return Math::Vector3d::dotProduct(normal, view) > 0.0f;
	}

	Math::Vector3d transformVector(const TankVec3 &point, const TankObject &object, byte scale) const {
		const float shapeScale = (float)(1 << scale);
		float x = (float)point.x * shapeScale;
		float y = (float)point.y * shapeScale;
		float z = (float)point.z * shapeScale;

		if (object.rotX) {
			float angle = tankAngleToRadians((uint16)object.rotX);
			float c = cosf(angle);
			float s = sinf(angle);
			float yy = y * c - z * s;
			float zz = y * s + z * c;
			y = yy;
			z = zz;
		}

		if (object.rotY) {
			float angle = tankAngleToRadians((uint16)object.rotY);
			float c = cosf(angle);
			float s = sinf(angle);
			float xx = x * c + z * s;
			float zz = -x * s + z * c;
			x = xx;
			z = zz;
		}

		if (object.rotZ) {
			float angle = tankAngleToRadians((uint16)object.rotZ);
			float c = cosf(angle);
			float s = sinf(angle);
			float xx = x * c - y * s;
			float yy = x * s + y * c;
			x = xx;
			y = yy;
		}

		// TinyGL uses Y as the vertical axis here. The original tank data uses Z.
		return Math::Vector3d(
			x * kTankWorldScale,
			z * kTankWorldScale,
			y * kTankWorldScale
		);
	}

	Math::Vector3d transformPoint(const TankVec3 &point, const TankObject &object, byte scale) const {
		const Math::Vector3d vector = transformVector(point, object, scale);
		return Math::Vector3d(
			object.loc.x * kTankWorldScale + vector.x(),
			object.loc.z * kTankWorldScale + vector.y(),
			object.loc.y * kTankWorldScale + vector.z()
		);
	}
};

ChinaTank::ChinaTank() : _tankScene(nullptr), _initialized(false), _loadFailed(false), _oldPalette(0) {
}

ChinaTank::~ChinaTank() {
	delete _tankScene;
}

void ChinaTank::init() {
	if (_initialized || _loadFailed)
		return;

	DgdsEngine *engine = DgdsEngine::getInstance();
	_oldPalette = engine->getGamePals()->getCurPalNum();

	if (engine->getResourceManager()->hasResource("tanksim.pal"))
		engine->getGamePals()->loadPalette("tanksim.pal");
	else
		engine->getGamePals()->loadPalette("ftank3s.pal");

	_tankScene = new TankScene();
	_initialized = _tankScene->load(engine->getResourceManager(), engine->getDecompressor());

	if (!_initialized) {
		delete _tankScene;
		_tankScene = nullptr;
		_loadFailed = true;
		engine->getGamePals()->selectPalNum(_oldPalette);
		warning("Tank minigame geometry could not be initialized");
	} else {
		engine->disableKeymapper();
		g_system->warpMouse(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
	}
}

void ChinaTank::tick() {
	if (!_initialized && !_loadFailed)
		init();

	if (!_initialized || !_tankScene)
		return;

	if (!ChinaTankTinyGLRenderer::isAvailable()) {
		g_system->displayMessageOnOSD(Common::U32String("Tank minigame requires TinyGL"));
		return;
	}

	DgdsEngine *engine = DgdsEngine::getInstance();
	_tankScene->render(engine->_compositionBuffer, engine->getGamePals()->getCurPal());
}

void ChinaTank::onMouseMove(int x, int y) {
	if (!_initialized || !_tankScene)
		return;

	const int centerX = SCREEN_WIDTH / 2;
	const int centerY = SCREEN_HEIGHT / 2;
	if (x == centerX && y == centerY)
		return;

	_tankScene->turnCamera(x - centerX, centerY - y);
	g_system->warpMouse(centerX, centerY);
}

void ChinaTank::onKeyDown(const Common::KeyState &kbd) {
	if (_tankScene)
		_tankScene->setMoveKey(kbd.keycode, true);
}

void ChinaTank::onKeyUp(const Common::KeyState &kbd) {
	if (_tankScene)
		_tankScene->setMoveKey(kbd.keycode, false);
}

void ChinaTank::end() {
	const bool restorePalette = _initialized;

	delete _tankScene;
	_tankScene = nullptr;
	_initialized = false;
	_loadFailed = false;

	if (restorePalette)
		DgdsEngine::getInstance()->getGamePals()->selectPalNum(_oldPalette);
	if (restorePalette)
		DgdsEngine::getInstance()->enableKeymapper();
}

} // end namespace Dgds
