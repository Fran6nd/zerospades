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

#include "SceneIO.h"

#include <cstdio>

#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		bool SceneIO::Load(const std::string& absPath, std::vector<VoxelObject>& out) {
			std::FILE* f = fs::OpenFile(absPath, "rb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership of the FILE*
				out = VoxelModel2KV6::Load(stream);
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

		bool SceneIO::Save(const std::vector<VoxelObject>& objects, const std::string& absPath) {
			// A scene with nothing in it is still a document: it saves, and opens
			// again as the empty scene it was. Written to a sibling temp file
			// first, then moved over the target, so a failure mid-write can never
			// truncate or corrupt an existing scene.
			std::string tmpPath = absPath + ".savetmp";
			std::FILE* f = fs::OpenFile(tmpPath, "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership; closes/flushes at scope exit
				VoxelModel2KV6::Save(stream, objects);
			} catch (const std::exception&) {
				fs::Delete(tmpPath);
				return false;
			}
			if (!fs::Rename(tmpPath, absPath, true)) {
				fs::Delete(tmpPath);
				return false;
			}
			return true;
		}
	} // namespace gui
} // namespace spades
