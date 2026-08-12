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

#include "ags/engine/script/system_imports.h"

namespace AGS3 {
using namespace AGS::Shared;

uint32_t SystemImports::add(const String &name, const RuntimeScriptValue &value, ccInstance *anotherscr) {
	// Only do exact match for add, not prefix/separator expansion
	IndexMap::const_iterator it = btree.find(name);
	uint32_t ixof = (it != btree.end()) ? it->_value : UINT32_MAX;
	// Check if symbol already exists
	if (ixof != UINT32_MAX) {
		// Only allow override if not a script-exported function
		if (anotherscr == nullptr) {
			imports[ixof].Value = value;
			imports[ixof].InstancePtr = anotherscr;
		}
		return ixof;
	}

	ixof = imports.size();
	for (size_t i = 0; i < imports.size(); ++i) {
		if (imports[i].Name == nullptr) {
			ixof = i;
			break;
		}
	}

	btree[name] = ixof;
	if (ixof == imports.size())
		imports.push_back(ScriptImport());
	imports[ixof].Name = name;
	imports[ixof].Value = value;
	imports[ixof].InstancePtr = anotherscr;
	return ixof;
}

void SystemImports::remove(const String &name) {
	uint32_t idx = get_index_of(name);
	if (idx == UINT32_MAX)
		return;
	btree.erase(imports[idx].Name);
	imports[idx].Name = nullptr;
	imports[idx].Value.Invalidate();
	imports[idx].InstancePtr = nullptr;
}

const ScriptImport *SystemImports::getByName(const String &name) {
	uint32_t o = get_index_of(name);
	if (o == UINT32_MAX)
		return nullptr;

	return &imports[o];
}

const ScriptImport *SystemImports::getByIndex(uint32_t index) {
	if (index >= imports.size())
		return nullptr;

	return &imports[index];
}

uint32_t SystemImports::get_index_of(const String &name) {
	IndexMap::const_iterator it = btree.find(name);
	if (it != btree.end())
		return it->_value;

	// Find import separator '^' or export separator '$'
	size_t args_at = name.FindChar('^');
	char args_separator = 0;
	if (args_at != String::NoIndex) {
		args_separator = '^';
	} else {
		args_at = name.FindChar('$');
		if (args_at != String::NoIndex) {
			args_separator = '$';
		}
	}

	if (args_separator == 0) {
		// No separator: try prefix search for matching exports
		String name_only = name; // use full name as base
		uint32_t name_only_match = UINT32_MAX;
		IndexMap::const_iterator lb = btree.lower_bound(name_only);
		for (; lb != btree.end(); ++lb) {
			const String &try_sym = lb->_key;
			if (try_sym.CompareLeft(name_only, name_only.GetLength()) != 0)
				break;
			if (try_sym.GetLength() == name_only.GetLength())
				name_only_match = lb->_value;
			else if (try_sym[name_only.GetLength()] == '$')
				return lb->_value; // script export with matching base name found
		}
		return name_only_match;
	}

	String name_only = name.Left(args_at);

	// Request is an import symbol (^)
	if (args_separator == '^') {
		// Search for entries matching base name
		uint32_t name_only_match = UINT32_MAX;
		IndexMap::const_iterator lb = btree.lower_bound(name_only);
		for (; lb != btree.end(); ++lb) {
			const String &try_sym = lb->_key;
			if (try_sym.CompareLeft(name_only, name_only.GetLength()) != 0)
				break;
			if (try_sym.GetLength() == name_only.GetLength())
				name_only_match = lb->_value;
			else if (try_sym[name_only.GetLength()] == '$')
				return lb->_value; // script export with matching base name found
			else if (try_sym[name_only.GetLength()] == '^')
				return lb->_value; // import/plugin symbol with matching base name found
		}
		return name_only_match;
	}

	// Request is an export symbol ($): try base name match without args
	it = btree.find(name_only);
	if (it != btree.end())
		return it->_value;

	return UINT32_MAX;
}

String SystemImports::findName(const RuntimeScriptValue &value) {
	for (const auto &import : imports) {
		if (import.Value == value) {
			return import.Name;
		}
	}
	return String();
}

void SystemImports::RemoveScriptExports(ccInstance *inst) {
	if (!inst) {
		return;
	}

	for (auto &import : imports) {
		if (import.Name == nullptr)
			continue;

		if (import.InstancePtr == inst) {
			btree.erase(import.Name);
			import.Name = nullptr;
			import.Value.Invalidate();
			import.InstancePtr = nullptr;
		}
	}
}

void SystemImports::clear() {
	btree.clear();
	imports.clear();
}

} // namespace AGS3
