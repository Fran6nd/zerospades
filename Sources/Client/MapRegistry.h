/*
 Copyright (c) 2026 ZeroSpades contributors

 This file is part of ZeroSpades.

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

#include <cstdint>
#include <string>
#include <vector>

namespace spades {
	namespace client {

		struct MapEntry {
			std::string path;        // Absolute path to the .vxl file.
			std::string displayName; // Filename with the .vxl extension stripped.
			uint64_t sizeBytes = 0;
			int64_t mtime = 0; // Last-modified time, seconds since the Unix epoch.
		};

		/**
		 * Enumerates user-provided .vxl maps found under <userResources>/Maps.
		 *
		 * The bundled Maps/ folder inside Resources/ is intentionally not scanned:
		 * it holds menu-backdrop assets, not playable maps. Users drop .vxl files
		 * into the per-user Maps directory alongside Demos/.
		 */
		class MapRegistry {
		public:
			/**
			 * Sets the base directory under which the Maps/ subfolder lives.
			 * Must be called before ListMaps(). Defaults to the current working
			 * directory if never called.
			 */
			static void SetBaseDirectory(const std::string& dir);

			/** Returns the absolute path to the Maps directory. */
			static std::string GetMapsDirectory();

			/**
			 * Returns all .vxl files in the Maps directory, sorted by display name
			 * (case-insensitive). Missing directory yields an empty list.
			 */
			static std::vector<MapEntry> ListMaps();
		};

	} // namespace client
} // namespace spades
