/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <vector>

#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace gui {
		/**
		 * Reads and writes .2kv6 scenes by absolute path: what is specific to
		 * scene documents, as KV6ModelIO is for models.
		 */
		namespace SceneIO {
			/** Reads the scene at `absPath` into `out`. A scene may hold no
			 *  objects, so failure is said apart from what was read; never
			 *  throws. */
			bool Load(const std::string& absPath, std::vector<VoxelObject>& out);

			/** Writes `objects` to `absPath` through a temporary file, so a
			 *  failure can never truncate what was there; false on failure. */
			bool Save(const std::vector<VoxelObject>& objects, const std::string& absPath);
		} // namespace SceneIO
	} // namespace gui
} // namespace spades
