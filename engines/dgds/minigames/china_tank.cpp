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
const int kTankShapeTank = 74;
const int kTankShapeTruck = 75;
const int kTankShapeTankHulk = 76;
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
const int kTankTrigShift = 14;
const int kTankGZero = 9600;
const int kTankCockpitHeight = 1000;
const int kTankFallCameraDistance = 32000;
const int16 kTankFallCameraRotX = 0x0a00;
const uint16 kTankFallCameraHeading = 0x7000;
const int kTankInitialX = 9412;
const int kTankInitialY = -60783;
const int kTankInitialZ = 23400 + kTankGZero;
const uint16 kTankInitialHeading = 0x4400;
const char *const kTankCockpitScreen = "ftank3s.scr";
const char *const kTankDevCockpitScreen = "tankcp.scr";
const char *const kTankCockpitShapes = "cpit.bmp";
const char *const kTankExplosionShapes = "exp.bmp";
const int kTankExplosionFrameCount = 12;
const int kTankFallingExplosionStartY = 210;
const int kTankFallingExplosionY = 204;
const float kTankWorldScale = 1.0f / 1000.0f;
const float kTankMouseSensitivity = 0.006f;
const float kTankMaxPitch = 1.45f;

enum TankTerrainCode {
	kTankTerrainHill = 1,
	kTankTerrainRiver = 2,
	kTankTerrainRollingHill = 3,
	kTankTerrainRoadway = 4,
	kTankTerrainField = 5
};

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

enum TankDynamicObjectIndex {
	kTankDynamicMyTank = 0,
	kTankDynamicAmbushTank = 1,
	kTankDynamicTrickyTank = 2,
	kTankDynamicCowTank = 3,
	kTankDynamicFastTruck = 4,
	kTankDynamicMobileObject = 5,
	kTankDynamicCount = 6
};

enum TankDynamicJob {
	kTankJobAttack = 0,
	kTankJobCliffBoom = 1,
	kTankJobBoom = 2,
	kTankJobTipOver = 3,
	kTankJobFalling = 4,
	kTankJobAmbush = 5,
	kTankJobFiring = 6
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

int tankTrigSin(uint16 angle) {
	return (int)(sinf(tankAngleToRadians(angle)) * (float)(1 << kTankTrigShift));
}

int tankTrigCos(uint16 angle) {
	return (int)(cosf(tankAngleToRadians(angle)) * (float)(1 << kTankTrigShift));
}

int64 tankArithmeticShiftRight64(int64 value, byte shift) {
	if (shift == 0)
		return value;
	if (shift >= 63)
		return value < 0 ? -1 : 0;
	if (value >= 0)
		return value >> shift;

	const int64 divisor = (int64)1 << shift;
	return -((-value + divisor - 1) >> shift);
}

int32 tankArithmeticShiftRight(int32 value, byte shift) {
	if (shift >= 31)
		return value < 0 ? -1 : 0;
	return (int32)tankArithmeticShiftRight64(value, shift);
}

int32 tankAbs32(int32 value) {
	if (value == (-0x7fffffff - 1))
		return 0x7fffffff;
	return value < 0 ? -value : value;
}

int16 tankWrapSint16(int32 value) {
	return (int16)(uint16)value;
}

uint32 tankFarDataOffset(uint16 segment, uint16 offset) {
	return ((uint32)segment << 4) + offset;
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

struct TankProjection {
	bool valid;
	bool onScreen;
	int x;
	int y;
	int size;
	float depth;

	TankProjection() : valid(false), onScreen(false), x(0), y(0), size(0), depth(0.0f) {}
};

struct TankDynamicObject {
	TankObject object;
	bool visible;
	bool target;
	int job;
	int jobWork;
	TankProjection projection;

	TankDynamicObject() : visible(false), target(true), job(kTankJobAttack), jobWork(0) {
		object.shape = 0;
		object.rotX = 0;
		object.rotY = 0;
		object.rotZ = 0;
		object.loc.x = 0;
		object.loc.y = 0;
		object.loc.z = 0;
	}
};

struct TankDynamicInit {
	int16 shape;
	TankVec3 loc;
	uint16 heading;
};

const TankDynamicInit kTankDynamicInitialObjects[kTankDynamicCount] = {
	{ kTankShapeTank,  {   9412,  -60783, 23400 + kTankGZero }, 0x4400 },
	{ kTankShapeTank,  {  12921,  -79136, 12812 + kTankGZero }, 0x4400 },
	{ kTankShapeTank,  {  24786,  -59794, 26100 + kTankGZero }, 0x4400 },
	{ kTankShapeTank,  {-363000, -340000, kTankCockpitHeight + kTankGZero }, 0x9400 },
	{ kTankShapeTruck, {-334065, -390198,     0 + kTankGZero }, 0x94a0 },
	{ kTankShapeTank,  {      0,       0,     0 + kTankGZero }, 0x0000 }
};

struct TankTerrainEdge {
	int16 nxs;
	int16 nys;
	int16 x1;
	int16 y1;
};

struct TankTerrainPolygonInfo {
	int16 normalX;
	int16 normalY;
	int16 normalZ;
	byte slope;
	byte tiltAng;
	byte upAng;
	Common::Array<TankTerrainEdge> edges;
	int16 baseHeight;
	TankTerrainEdge baseEdge;
};

struct TankTerrainInfo {
	bool present;
	int16 horRad;
	int16 verRad;
	byte isRect;
	byte priority;
	byte apcode;
	Common::Array<TankTerrainPolygonInfo> polygons;

	TankTerrainInfo() : present(false), horRad(0), verRad(0), isRect(0), priority(0), apcode(0) {}
};

struct TankGroundInfo {
	int terrainShape;
	int objectIndex;
	int polyNum;
	int32 height;

	TankGroundInfo() { clear(); }

	void clear() {
		terrainShape = -1;
		objectIndex = -1;
		polyNum = 0;
		height = 0;
	}

	bool isValid() const { return terrainShape >= 0 && objectIndex >= 0; }
};

struct TankSavedState {
	TankVec3 loc;
	int16 rotX;
	int16 rotY;
	uint16 heading;
	TankGroundInfo ground;
	int terrainType;
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
	Common::Array<TankDynamicObject> _dynamicObjects;
	Common::Array<TankTerrainInfo> _terrainInfos;
	ChinaTankTinyGLRenderer _renderer;
	Common::SharedPtr<Image> _cockpitShapes;
	Common::SharedPtr<Image> _explosionShapes;
	Graphics::ManagedSurface _cockpitBackground;
	bool _loaded;
	bool _cockpitBackgroundLoaded;
	TankVec3 _tankLoc;
	int16 _tankRotX;
	int16 _tankRotY;
	uint16 _tankHeading;
	float _pitch;
	int _tankSpeed;
	int _fuel;
	TankGearPosition _gearPosition;
	bool _turnLeft;
	bool _turnRight;
	bool _gearUpPressed;
	bool _gearDownPressed;
	TankGroundInfo _groundInfo;
	TankSavedState _oldState;
	int _terrainType;
	int _oldBaseHeight;
	bool _inGap;
	TankVec3 _fallDeltaLoc;
	int16 _fallDeltaRotX;
	int16 _fallDeltaRotY;
	int16 _fallDeltaHeading;
	TankVec3 _externalCameraLoc;
	TankVec3 _externalCameraTarget;
	bool _falling;
	bool _intoWater;
	bool _tankDone;

	TankScene() : _loaded(false),
		_cockpitBackgroundLoaded(false),
		_tankRotX(0),
		_tankRotY(0),
		_tankHeading(kTankInitialHeading),
		_pitch(0.0f),
		_tankSpeed(0),
		_fuel(kTankInitialFuel),
		_gearPosition(kTankGearNeutral),
		_turnLeft(false),
		_turnRight(false),
		_gearUpPressed(false),
		_gearDownPressed(false),
		_terrainType(0),
		_oldBaseHeight(0),
		_inGap(false),
		_fallDeltaRotX(0),
		_fallDeltaRotY(0),
		_fallDeltaHeading(0),
		_falling(false),
		_intoWater(false),
		_tankDone(false)
	{
		_tankLoc.x = kTankInitialX;
		_tankLoc.y = kTankInitialY;
		_tankLoc.z = kTankInitialZ;
		_oldState.loc = _tankLoc;
		_oldState.rotX = _tankRotX;
		_oldState.rotY = _tankRotY;
		_oldState.heading = _tankHeading;
		_oldState.terrainType = _terrainType;
		_fallDeltaLoc.x = 0;
		_fallDeltaLoc.y = 0;
		_fallDeltaLoc.z = 0;
		_externalCameraLoc = _tankLoc;
		_externalCameraTarget = _tankLoc;
	}

	bool load(ResourceManager *resource, Decompressor *decompressor) {
		Common::Array<byte> tbl = readResourceBytes(resource, "tank.tbl");
		Common::Array<byte> wld = readResourceBytes(resource, "tank.wld");
		Common::Array<byte> gi = readResourceBytes(resource, "tank.gi");
		if (tbl.empty() || wld.empty() || gi.empty()) {
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

		if (!loadTerrainInfo(gi))
			return false;

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

		initDynamicObjects();
		adjustPlayerTank();
		_loaded = true;
		loadCockpit(resource, decompressor);
		return true;
	}

	void initDynamicObjects() {
		_dynamicObjects.clear();
		_dynamicObjects.resize(kTankDynamicCount);

		for (int i = 0; i < kTankDynamicCount; i++) {
			TankDynamicObject &object = _dynamicObjects[i];
			object.object.shape = kTankDynamicInitialObjects[i].shape;
			object.object.rotX = 0;
			object.object.rotY = 0;
			object.object.rotZ = tankWrapSint16(kTankDynamicInitialObjects[i].heading);
			object.object.loc = kTankDynamicInitialObjects[i].loc;
			object.visible = false;
			object.target = true;
			object.job = kTankJobAttack;
			object.jobWork = 0;
			object.projection = TankProjection();
		}

		_dynamicObjects[kTankDynamicMyTank].visible = true;
		_dynamicObjects[kTankDynamicTrickyTank].visible = true;
		_dynamicObjects[kTankDynamicMobileObject].target = false;
		_dynamicObjects[kTankDynamicAmbushTank].job = kTankJobAmbush;
	}

	void startDynamicJob(TankDynamicObjectIndex index, int job, int jobWork) {
		if (_dynamicObjects.size() <= (uint)index)
			return;

		TankDynamicObject &object = _dynamicObjects[index];
		object.visible = true;
		object.job = job;
		object.jobWork = jobWork;
	}

	void turnCamera(int dx, int dy) {
		if (_falling || _tankDone)
			return;

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
		if (_tankDone)
			return;

		if (_falling) {
			fallPlayerTank();
			return;
		}

		if (_fuel <= 0)
			_gearPosition = kTankGearNeutral;

		if (_turnLeft)
			_tankHeading = tankWrapAngle(_tankHeading + kTankTurnSpeed);
		else if (_turnRight)
			_tankHeading = tankWrapAngle(_tankHeading - kTankTurnSpeed);

		updateTankSpeed();
		movePlayerTank();
		updateFuel();
		checkTerrainCollision();
		if (_falling && !_tankDone)
			fallPlayerTank();
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
		savePlayerState();

		if (_tankSpeed != 0) {
			if (_tankRotX || _tankRotY) {
				int32 temp = _tankSpeed * -tankTrigCos((uint16)_tankRotX);
				temp = (temp >> kTankTrigShift) * tankTrigSin(_tankHeading);
				_tankLoc.x += temp >> kTankTrigShift;

				temp = _tankSpeed * tankTrigCos((uint16)_tankRotX);
				temp = (temp >> kTankTrigShift) * tankTrigCos(_tankHeading);
				_tankLoc.y += temp >> kTankTrigShift;
				_tankLoc.z += (_tankSpeed * tankTrigSin((uint16)_tankRotX)) >> kTankTrigShift;
			} else if (_tankHeading) {
				_tankLoc.x += (_tankSpeed * -tankTrigSin(_tankHeading)) >> kTankTrigShift;
				_tankLoc.y += (_tankSpeed * tankTrigCos(_tankHeading)) >> kTankTrigShift;
			} else {
				_tankLoc.y += _tankSpeed;
			}
		}

		adjustPlayerTank();
	}

	void savePlayerState() {
		_oldState.loc = _tankLoc;
		_oldState.rotX = _tankRotX;
		_oldState.rotY = _tankRotY;
		_oldState.heading = _tankHeading;
		_oldState.ground = _groundInfo;
		_oldState.terrainType = _terrainType;

		const TankTerrainPolygonInfo *poly = currentTerrainPolygon();
		_oldBaseHeight = poly ? poly->baseHeight : 0;
	}

	void adjustPlayerTank() {
		_tankLoc.z = kTankCockpitHeight;
		_tankRotX = 0;
		_tankRotY = 0;

		TankGroundInfo ground;
		const bool hasGround = readGroundInfo(ground, _tankLoc);
		_groundInfo.clear();
		_terrainType = 0;

		if (hasGround) {
			_groundInfo = ground;
			const TankTerrainInfo &terrain = _terrainInfos[ground.terrainShape];
			_terrainType = terrain.apcode;

			if (terrain.priority == 0) {
				const TankTerrainPolygonInfo &poly = terrain.polygons[ground.polyNum];
				byte xang = (byte)((poly.upAng - (byte)(_tankHeading >> 8)) & 0xff);
				if (xang >= 128)
					xang = 256 - xang;
				xang = (byte)((poly.tiltAng * (64 - xang)) >> 6);

				byte yang = (byte)((poly.upAng - (byte)(_tankHeading >> 8) - 64) & 0xff);
				if (yang >= 128)
					yang = 256 - yang;
				yang = (byte)((poly.tiltAng * (64 - yang)) >> 6);

				_tankLoc.z += ground.height;
				if (terrain.apcode == kTankTerrainRollingHill) {
					_tankLoc.z += 560;
					if (ground.polyNum == 1 && poly.upAng == 192)
						_tankLoc.z += 300;
				}

				_tankRotX = (int16)((uint16)xang << 8);
				_tankRotY = (int16)((uint16)yang << 8);
				return;
			}
		}

		_tankLoc.z += kTankGZero;
	}

	bool readGroundInfo(TankGroundInfo &ground, const TankVec3 &loc) const {
		if (_groundInfo.isValid()) {
			const TankTerrainInfo &terrain = _terrainInfos[_groundInfo.terrainShape];
			if (terrain.priority == 0 && tryGroundObject(_groundInfo.objectIndex, loc, true, ground))
				return true;
		}

		ground.clear();
		bool found = false;
		byte foundPriority = 0;

		for (uint i = 0; i < _objects.size(); i++) {
			const TankObject &object = _objects[i];
			if (object.shape < 0 || object.shape >= (int)_terrainInfos.size())
				continue;

			const TankTerrainInfo &terrain = _terrainInfos[object.shape];
			if (!terrain.present || terrain.polygons.empty())
				continue;
			if (found && terrain.priority > foundPriority)
				continue;

			TankGroundInfo candidate;
			if (!tryGroundObject(i, loc, false, candidate))
				continue;

			ground = candidate;
			found = true;
			foundPriority = terrain.priority;
			if (terrain.priority == 0)
				return true;
		}

		return found;
	}

	bool tryGroundObject(int objectIndex, const TankVec3 &loc, bool exactPoly, TankGroundInfo &ground) const {
		if (objectIndex < 0 || objectIndex >= (int)_objects.size())
			return false;

		const TankObject &object = _objects[objectIndex];
		if (object.shape < 0 || object.shape >= (int)_terrainInfos.size() || object.shape >= (int)_shapes.size())
			return false;

		const TankTerrainInfo &terrain = _terrainInfos[object.shape];
		if (!terrain.present || terrain.polygons.empty())
			return false;

		const byte scale = _shapes[object.shape].scale;
		const int32 localX32 = tankArithmeticShiftRight(loc.x - object.loc.x, scale);
		const int32 localY32 = tankArithmeticShiftRight(loc.y - object.loc.y, scale);
		if (tankAbs32(localX32) > terrain.horRad || tankAbs32(localY32) > terrain.verRad)
			return false;

		const int16 localX = (int16)localX32;
		const int16 localY = (int16)localY32;
		int polyNum = 0;

		if (!terrain.isRect) {
			if (exactPoly && _groundInfo.polyNum >= 0 && _groundInfo.polyNum < (int)terrain.polygons.size()) {
				polyNum = _groundInfo.polyNum;
				if (!boundTerrainPolygon(terrain.polygons[polyNum], localX, localY))
					polyNum = -1;
			} else {
				polyNum = -1;
			}

			if (polyNum < 0) {
				for (uint i = 0; i < terrain.polygons.size(); i++) {
					if (boundTerrainPolygon(terrain.polygons[i], localX, localY)) {
						polyNum = i;
						break;
					}
				}
			}

			if (polyNum < 0)
				return false;
		}

		ground.terrainShape = object.shape;
		ground.objectIndex = objectIndex;
		ground.polyNum = polyNum;
		if (terrain.priority == 0) {
			const int32 localHeight = getTerrainHeightAt(terrain.polygons[polyNum], localX, localY);
			ground.height = (int32)((int64)localHeight * ((int64)1 << scale)) + object.loc.z;
		} else {
			ground.height = 0;
		}
		return true;
	}

	bool boundTerrainPolygon(const TankTerrainPolygonInfo &poly, int16 x, int16 y) const {
		for (const TankTerrainEdge &edge : poly.edges) {
			const int64 value = (int64)edge.nys * (y - edge.y1) + (int64)edge.nxs * (x - edge.x1);
			if (value > 0)
				return false;
		}
		return true;
	}

	int32 getTerrainHeightAt(const TankTerrainPolygonInfo &poly, int16 x, int16 y) const {
		int64 workHeight = 0;
		if (poly.baseEdge.nxs)
			workHeight += (int64)poly.baseEdge.nxs * (poly.baseEdge.x1 - x);
		if (poly.baseEdge.nys)
			workHeight += (int64)poly.baseEdge.nys * (poly.baseEdge.y1 - y);

		int32 height = poly.baseHeight;
		if (workHeight)
			height += (int32)tankArithmeticShiftRight64(workHeight * poly.slope, 16);
		return height;
	}

	const TankTerrainPolygonInfo *currentTerrainPolygon() const {
		if (!_groundInfo.isValid())
			return nullptr;
		if (_groundInfo.terrainShape < 0 || _groundInfo.terrainShape >= (int)_terrainInfos.size())
			return nullptr;

		const TankTerrainInfo &terrain = _terrainInfos[_groundInfo.terrainShape];
		if (_groundInfo.polyNum < 0 || _groundInfo.polyNum >= (int)terrain.polygons.size())
			return nullptr;
		return &terrain.polygons[_groundInfo.polyNum];
	}

	void checkTerrainCollision() {
		if (_terrainType == kTankTerrainHill || _terrainType == kTankTerrainRiver) {
			checkForTooSteep();
		} else if (_terrainType != kTankTerrainRollingHill) {
			checkGap();
			checkBushes();
		}
	}

	void checkForTooSteep() {
		const TankTerrainPolygonInfo *poly = currentTerrainPolygon();
		if (!poly || poly->slope <= 14)
			return;

		_tankSpeed = 0;
		_gearPosition = kTankGearNeutral;
		if (amIFalling(poly))
			startFalling();
		else
			oldPosition();
	}

	bool amIFalling(const TankTerrainPolygonInfo *poly) const {
		if (_terrainType == kTankTerrainRiver)
			return true;
		if (_oldState.terrainType != kTankTerrainHill)
			return false;

		return poly && poly->baseHeight + 35 < _oldBaseHeight;
	}

	void startFalling() {
		_falling = true;
		_tankDone = false;
		_intoWater = _terrainType == kTankTerrainRiver;
		startDynamicJob(kTankDynamicMyTank, kTankJobFalling, 0);

		if (_intoWater) {
			_fallDeltaRotX = 0x1000;
			_tankRotX = tankWrapSint16(0xf800);
		} else {
			_fallDeltaRotX = tankWrapSint16((int32)_oldState.rotX - _tankRotX);
		}

		_fallDeltaLoc.x = _oldState.loc.x - _tankLoc.x;
		_fallDeltaLoc.y = _oldState.loc.y - _tankLoc.y;
		_fallDeltaLoc.z = _oldState.loc.z - _tankLoc.z;
		_fallDeltaRotY = tankWrapSint16((int32)_oldState.rotY - _tankRotY);
		_fallDeltaHeading = tankWrapSint16((int32)(uint16)_oldState.heading - (int32)(uint16)_tankHeading);

		positionExternalCameraForFall();
	}

	void fallPlayerTank() {
		_fallDeltaLoc.z += 25;
		_tankLoc.x -= _fallDeltaLoc.x;
		_tankLoc.y -= _fallDeltaLoc.y;
		_tankLoc.z -= _fallDeltaLoc.z;
		_tankRotX = tankWrapSint16((int32)_tankRotX - tankArithmeticShiftRight(_fallDeltaRotX, 2));
		_tankRotY = tankWrapSint16((int32)_tankRotY - tankArithmeticShiftRight(_fallDeltaRotY, 2));
		_tankHeading = tankWrapAngle((int)_tankHeading - tankArithmeticShiftRight(_fallDeltaHeading, 2));

		if (!_intoWater && _tankLoc.z < 0)
			_tankDone = true;
	}

	void positionExternalCameraForFall() {
		_externalCameraTarget = _tankLoc;
		_externalCameraLoc = _tankLoc;

		int32 temp = kTankFallCameraDistance * -tankTrigCos((uint16)kTankFallCameraRotX);
		temp = (temp >> kTankTrigShift) * tankTrigSin(kTankFallCameraHeading);
		_externalCameraLoc.x += temp >> kTankTrigShift;

		temp = kTankFallCameraDistance * tankTrigCos((uint16)kTankFallCameraRotX);
		temp = (temp >> kTankTrigShift) * tankTrigCos(kTankFallCameraHeading);
		_externalCameraLoc.y += temp >> kTankTrigShift;
		_externalCameraLoc.z += (kTankFallCameraDistance * tankTrigSin((uint16)kTankFallCameraRotX)) >> kTankTrigShift;
	}

	void checkGap() {
		if ((_tankLoc.y < -330000L) && (_tankLoc.y > -388000L) &&
				(_tankLoc.x > -344000L)) {
			if (_tankLoc.x > -164000L) {
				oldPosition();
			} else {
				if (_tankLoc.x < -310000L) {
					if (_tankLoc.y > -375000L)
						oldPosition();
				} else {
					if (_tankLoc.x < -206000L) {
						if (_tankLoc.y > -366000L) {
							oldPosition();
						} else if (_inGap) {
							_inGap = false;
						}
					} else {
						_inGap = true;
						if (_tankLoc.y < -378000L)
							oldPosition();
					}
				}
			}
		}
	}

	void checkBushes() {
		if ((_tankLoc.y > -110000L) && !_terrainType) {
			if ((_tankLoc.y < 130000L) || (_tankLoc.x > -320000L))
				oldPosition();
		} else if (_terrainType != kTankTerrainField) {
			if ((_tankLoc.x > 70000L) ||
					((_tankLoc.y < -565816L) && (_tankLoc.y > -648030L)))
				oldPosition();
		}
	}

	void oldPosition() {
		_tankSpeed = 0;
		_gearPosition = kTankGearNeutral;
		_tankLoc = _oldState.loc;
		_tankRotX = _oldState.rotX;
		_tankRotY = _oldState.rotY;
		_tankHeading = _oldState.heading;
		_groundInfo = _oldState.ground;
		_terrainType = _oldState.terrainType;
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

		if (hasCockpitShapes) {
			_cockpitShapes.reset(new Image(resource, decompressor));
			_cockpitShapes->loadBitmap(kTankCockpitShapes);
			if (_cockpitShapes->loadedFrameCount() < 4) {
				warning("Tank cockpit expected at least 4 %s frames, got %d", kTankCockpitShapes, _cockpitShapes->loadedFrameCount());
				_cockpitShapes.reset();
			}
		}

		loadExplosionShapes(resource, decompressor);
	}

	void loadExplosionShapes(ResourceManager *resource, Decompressor *decompressor) {
		if (!resource->hasResource(kTankExplosionShapes)) {
			warning("Tank explosion UI resource missing: %s", kTankExplosionShapes);
			return;
		}

		_explosionShapes.reset(new Image(resource, decompressor));
		_explosionShapes->loadBitmap(kTankExplosionShapes);
		if (_explosionShapes->loadedFrameCount() < kTankExplosionFrameCount) {
			warning("Tank explosion expected at least %d %s frames, got %d", kTankExplosionFrameCount,
					kTankExplosionShapes, _explosionShapes->loadedFrameCount());
			_explosionShapes.reset();
		}
	}

	bool loadTerrainInfo(const Common::Array<byte> &gi) {
		if (gi.size() < kTankShapeCount * 4) {
			warning("Tank terrain table is too small");
			return false;
		}

		_terrainInfos.clear();
		_terrainInfos.resize(kTankShapeCount);

		for (int i = 0; i < kTankShapeCount; i++) {
			const uint16 offset = READ_LE_UINT16(&gi[i * 4]);
			const uint16 segment = READ_LE_UINT16(&gi[i * 4 + 2]);
			if (!offset && !segment)
				continue;

			const uint32 pos = tankFarDataOffset(segment, offset);
			if (!checkRange(gi, pos, 10, "terrain info"))
				return false;

			TankTerrainInfo info;
			info.present = true;
			info.horRad = readSint16(&gi[pos]);
			info.verRad = readSint16(&gi[pos + 2]);
			info.isRect = gi[pos + 4];
			info.priority = gi[pos + 5];
			info.apcode = gi[pos + 6];
			const byte numPolys = gi[pos + 7];
			const uint16 polyOffset = READ_LE_UINT16(&gi[pos + 8]);

			if (numPolys) {
				const uint32 polyBase = tankFarDataOffset(segment, polyOffset);
				if (!checkRange(gi, polyBase, numPolys * 16, "terrain polygons"))
					return false;

				for (int j = 0; j < numPolys; j++) {
					TankTerrainPolygonInfo poly;
					const uint32 polyPos = polyBase + j * 16;
					poly.normalX = readSint16(&gi[polyPos]);
					poly.normalY = readSint16(&gi[polyPos + 2]);
					poly.normalZ = readSint16(&gi[polyPos + 4]);
					poly.slope = gi[polyPos + 6];
					poly.tiltAng = gi[polyPos + 7];
					poly.upAng = gi[polyPos + 8];
					const byte numEdges = gi[polyPos + 9];
					const uint16 edgeOffset = READ_LE_UINT16(&gi[polyPos + 10]);
					poly.baseHeight = readSint16(&gi[polyPos + 12]);
					const uint16 baseEdgeOffset = READ_LE_UINT16(&gi[polyPos + 14]);

					if (numEdges) {
						const uint32 edgeBase = tankFarDataOffset(segment, edgeOffset);
						if (!checkRange(gi, edgeBase, numEdges * 8, "terrain edges"))
							return false;
						for (int k = 0; k < numEdges; k++)
							poly.edges.push_back(readTerrainEdge(gi, edgeBase + k * 8));
					}

					const uint32 baseEdgePos = tankFarDataOffset(segment, baseEdgeOffset);
					if (!checkRange(gi, baseEdgePos, 8, "terrain base edge"))
						return false;
					poly.baseEdge = readTerrainEdge(gi, baseEdgePos);
					info.polygons.push_back(poly);
				}
			}

			_terrainInfos[i] = info;
		}

		return true;
	}

	TankTerrainEdge readTerrainEdge(const Common::Array<byte> &gi, uint32 pos) const {
		TankTerrainEdge edge;
		edge.nxs = readSint16(&gi[pos]);
		edge.nys = readSint16(&gi[pos + 2]);
		edge.x1 = readSint16(&gi[pos + 4]);
		edge.y1 = readSint16(&gi[pos + 6]);
		return edge;
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

		uint32 selectedDetailPos = detailPos;
		int16 selectedDetailSize = readSint16(&tbl[selectedDetailPos]);
		for (int i = 1; i < numDetails; i++) {
			const uint32 candidateDetailPos = detailPos + i * 6;
			const int16 candidateDetailSize = readSint16(&tbl[candidateDetailPos]);
			if (candidateDetailSize > selectedDetailSize) {
				selectedDetailPos = candidateDetailPos;
				selectedDetailSize = candidateDetailSize;
			}
		}

		byte numParts = tbl[selectedDetailPos + 3];
		uint16 partOffset = READ_LE_UINT16(&tbl[selectedDetailPos + 4]);
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
		syncPlayerDynamicObject();

		const bool externalCamera = useExternalCamera();
		const Common::Rect viewport = activeViewport();
		if (!externalCamera)
			drawCockpitBackground(dst);
		_renderer.beginFrame(viewport, palette, 248, cameraPosition(), cameraInterest(), 70.0f, 0.10f, 1200.0f);

		for (const TankObject &object : _objects)
			renderObject(object, palette);
		for (uint i = 0; i < _dynamicObjects.size(); i++)
			renderDynamicObject(i, externalCamera, viewport, palette);

		_renderer.endFrame(dst, viewport, palette);
		drawDynamicObjectOverlays(dst, viewport);
		if (!externalCamera)
			drawCockpit(dst);
	}

	void syncPlayerDynamicObject() {
		if (_dynamicObjects.size() <= kTankDynamicMyTank)
			return;

		TankDynamicObject &player = _dynamicObjects[kTankDynamicMyTank];
		const int16 shape = player.object.shape == kTankShapeTankHulk ? kTankShapeTankHulk : kTankShapeTank;
		player.object = playerTankObject();
		player.object.shape = shape;
	}

	TankObject playerTankObject() const {
		TankObject object;
		object.shape = kTankShapeTank;
		object.rotX = _tankRotX;
		object.rotY = _tankRotY;
		object.rotZ = tankWrapSint16((int32)_tankHeading);
		object.loc = _tankLoc;
		return object;
	}

	void renderDynamicObject(uint index, bool externalCamera, const Common::Rect &viewport, const DgdsPal &palette) {
		TankDynamicObject &dynamicObject = _dynamicObjects[index];
		dynamicObject.projection = TankProjection();
		if (!shouldRenderDynamicObject(index, externalCamera))
			return;

		dynamicObject.projection = projectObject(dynamicObject.object, viewport);
		renderObject(dynamicObject.object, palette);
	}

	bool shouldRenderDynamicObject(uint index, bool externalCamera) const {
		const TankDynamicObject &dynamicObject = _dynamicObjects[index];
		if (!dynamicObject.visible)
			return false;
		if (index == kTankDynamicMyTank && !externalCamera)
			return false;
		if (index == kTankDynamicMobileObject && externalCamera)
			return false;
		return true;
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
		if (useExternalCamera())
			return tankPointToTinyGL(_externalCameraLoc);

		return tankPointToTinyGL(_tankLoc);
	}

	Math::Vector3d tankPointToTinyGL(const TankVec3 &loc) const {
		return Math::Vector3d(
			loc.x * kTankWorldScale,
			loc.z * kTankWorldScale,
			loc.y * kTankWorldScale
		);
	}

	Math::Vector3d cameraInterest() const {
		if (useExternalCamera())
			return tankPointToTinyGL(_externalCameraTarget);

		const float yaw = tankAngleToRadians(_tankHeading);
		const float pitchScale = cosf(_pitch);
		return Math::Vector3d(
			_tankLoc.x * kTankWorldScale + -sinf(yaw) * pitchScale * 100.0f,
			_tankLoc.z * kTankWorldScale + sinf(_pitch) * 100.0f,
			_tankLoc.y * kTankWorldScale + cosf(yaw) * pitchScale * 100.0f
		);
	}

	Math::Vector3d cullingCameraPosition() const {
		return cameraPosition();
	}

	bool useExternalCamera() const {
		return _falling && !_intoWater;
	}

	Common::Rect activeViewport() const {
		if (useExternalCamera())
			return Common::Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		return getTankViewport();
	}

	TankProjection projectObject(const TankObject &object, const Common::Rect &viewport) const {
		TankProjection projection;
		if (object.shape < 0 || object.shape >= (int)_shapes.size())
			return projection;

		const TankShape &shape = _shapes[object.shape];
		const int32 scaledRadius = (int32)shape.radius * ((int32)1 << shape.scale);
		return projectPoint(object.loc, scaledRadius, viewport);
	}

	TankProjection projectPoint(const TankVec3 &loc, int32 radius, const Common::Rect &viewport) const {
		TankProjection projection;
		const Math::Vector3d camera = cameraPosition();
		const Math::Vector3d interest = cameraInterest();
		Math::Vector3d forward = interest - camera;
		if (forward.getMagnitude() <= 0.0001f)
			return projection;
		forward.normalize();

		Math::Vector3d worldUp(0.0f, 1.0f, 0.0f);
		if (fabsf(Math::Vector3d::dotProduct(forward, worldUp)) > 0.98f)
			worldUp = Math::Vector3d(0.0f, 0.0f, 1.0f);

		Math::Vector3d right = Math::Vector3d::crossProduct(worldUp, forward);
		if (right.getMagnitude() <= 0.0001f)
			return projection;
		right.normalize();
		Math::Vector3d up = Math::Vector3d::crossProduct(forward, right);
		up.normalize();

		const Math::Vector3d rel = tankPointToTinyGL(loc) - camera;
		const float depth = Math::Vector3d::dotProduct(rel, forward);
		if (depth <= 0.10f)
			return projection;

		const float camX = Math::Vector3d::dotProduct(rel, right);
		const float camY = Math::Vector3d::dotProduct(rel, up);
		const float tanY = tanf((float)(70.0 * M_PI / 360.0));
		const float aspect = (float)viewport.width() / (float)viewport.height();
		const float tanX = tanY * aspect;
		const float ndcX = camX / (depth * tanX);
		const float ndcY = camY / (depth * tanY);

		projection.valid = true;
		projection.depth = depth;
		projection.x = viewport.left + viewport.width() / 2 - (int)(ndcX * viewport.width() * 0.5f);
		projection.y = viewport.top + viewport.height() / 2 - (int)(ndcY * viewport.height() * 0.5f);
		projection.size = MAX(1, (int)((float)tankAbs32(radius) * kTankWorldScale / (depth * tanY) * viewport.height() * 0.5f));
		projection.onScreen = projection.x >= viewport.left && projection.x <= viewport.right &&
				projection.y >= viewport.top && projection.y <= viewport.bottom;
		return projection;
	}

	void drawDynamicObjectOverlays(Graphics::ManagedSurface &dst, const Common::Rect &viewport) {
		for (uint i = 0; i < _dynamicObjects.size(); i++) {
			TankDynamicObject &dynamicObject = _dynamicObjects[i];
			if (!dynamicObject.visible || !dynamicObject.projection.valid)
				continue;

			if (dynamicObject.object.shape == kTankShapeTruck)
				afterTruck(dynamicObject, dst, viewport);
			else
				afterTank(i, dynamicObject, dst, viewport);
		}
	}

	void afterTank(uint index, TankDynamicObject &dynamicObject, Graphics::ManagedSurface &dst, const Common::Rect &viewport) {
		const int workVal = dynamicObject.jobWork;
		if (dynamicObject.job == kTankJobFalling && !workVal && dynamicObject.projection.y > kTankFallingExplosionStartY) {
			dynamicObject.jobWork = 1;
			dynamicObject.target = false;
			dynamicObject.projection.y = kTankFallingExplosionY;
		}

		if (dynamicObject.job == kTankJobFiring && workVal <= 6)
			drawExplosionFrame(dst, viewport, workVal + 5, dynamicObject.projection.x, dynamicObject.projection.y, true);

		if (workVal >= 6 && workVal <= 11 &&
				(dynamicObject.job == kTankJobBoom || dynamicObject.job == kTankJobCliffBoom || dynamicObject.job == kTankJobFalling)) {
			drawExplosionFrame(dst, viewport, workVal - 6, dynamicObject.projection.x, dynamicObject.projection.y, false);
			if (workVal == 9)
				dynamicObject.object.shape = kTankShapeTankHulk;
		}

		if (workVal == 11 && index == kTankDynamicMyTank)
			_tankDone = true;
		if (workVal)
			dynamicObject.jobWork++;
	}

	void afterTruck(TankDynamicObject &dynamicObject, Graphics::ManagedSurface &dst, const Common::Rect &viewport) {
		const int workVal = dynamicObject.jobWork;
		if (dynamicObject.job == kTankJobTipOver && workVal >= 6 && workVal <= 11)
			drawExplosionFrame(dst, viewport, workVal - 6, dynamicObject.projection.x, dynamicObject.projection.y, false);
		if (workVal)
			dynamicObject.jobWork++;
	}

	void drawExplosionFrame(Graphics::ManagedSurface &dst, const Common::Rect &viewport, int frame, int x, int y, bool centered) const {
		if (!_explosionShapes || frame < 0 || frame >= _explosionShapes->loadedFrameCount())
			return;

		const int frameWidth = _explosionShapes->width(frame);
		const int frameHeight = _explosionShapes->height(frame);
		const int drawX = x - (frameWidth >> 1);
		const int drawY = centered ? y - (frameHeight >> 1) : y - frameHeight;
		_explosionShapes->drawBitmap(frame, drawX, drawY, viewport, dst);
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
		const Math::Vector3d view = cullingCameraPosition() - objectCenter;
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
