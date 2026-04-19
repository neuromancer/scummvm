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

#include "neuromancer/detection.h"
#include "neuromancer/neuromancer.h"

namespace Neuromancer {

class NeuromancerMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override {
		return "neuromancer";
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *gd) const override {
		*engine = new NeuromancerEngine(syst, gd);
		return Common::kNoError;
	}

	// Enable ScummVM's built-in save/load UI (list, pick, delete slots;
	// thumbnails + play time + meta-info on saves). The actual state
	// serialization lives in NeuromancerEngine::syncGame.
	bool hasFeature(MetaEngineFeature f) const override {
		return checkExtendedSaves(f) ||
		       (f == kSupportsLoadingDuringStartup);
	}

	// Up to 99 numbered save slots, matching the ScummVM default.
	int getMaximumSaveSlot() const override { return 99; }
};

} // End of namespace Neuromancer

#if PLUGIN_ENABLED_DYNAMIC(NEUROMANCER)
REGISTER_PLUGIN_DYNAMIC(NEUROMANCER, PLUGIN_TYPE_ENGINE, Neuromancer::NeuromancerMetaEngine);
#else
REGISTER_PLUGIN_STATIC(NEUROMANCER, PLUGIN_TYPE_ENGINE, Neuromancer::NeuromancerMetaEngine);
#endif
