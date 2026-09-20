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

#include <cstdint>
#include <string>
#include <vector>

#include <Core/RefCountedObject.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	class VoxelModel;
	namespace gui {
		/**
		 * Loads and saves KV6 models by absolute path, and names the folder the
		 * editor opens in.
		 *
		 * Browsing and other file operations belong to `LocalFileSystem`; this only
		 * covers what is specific to KV6 documents.
		 */
		class KV6ScreenHelper : public RefCountedObject {
		public:
			KV6ScreenHelper();

			/** The folder to open in (absolute): the data dir's kv6/. */
			std::string DefaultDir();

			/**
			 * Load / save a KV6 model by absolute path. `Load` returns null on
			 * failure (e.g. missing or corrupt file) rather than throwing; `Save`
			 * writes through a temp file so a failure cannot truncate the target.
			 */
			VoxelModel* Load(const std::string& absPath);
			bool Save(VoxelModel* model, const std::string& absPath);

			// Load / save .2kv6 scenes
			std::vector<VoxelObject> Load2KV6(const std::string& absPath);
			bool Save2KV6(const std::vector<VoxelObject>& scene, const std::string& absPath);

			// Create a new .2kv6 scene with a single unnamed root object containing an empty KV6
			std::vector<VoxelObject> NewScene2KV6(int sizeXYZ);

			// Load a .kv6 file as a VoxelObject (wraps KV6 data in a named container)
			VoxelObject LoadKV6AsObject(const std::string& absPath, const std::string& name = "");

		protected:
			~KV6ScreenHelper();

		private:
			std::string defaultDirAbs; // <user data dir>/kv6
		};
	} // namespace gui
} // namespace spades
