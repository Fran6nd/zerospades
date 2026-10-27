/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <memory>

#include "GLModelManager.h"
#include "GLOptimizedVoxelModel.h"
#include "GLRenderer.h"
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/ResourceChanges.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Core/VoxelModelLoader.h>

namespace spades {
	namespace draw {
		GLModelManager::GLModelManager(GLRenderer& r) : renderer{r} { SPADES_MARK_FUNCTION(); }
		GLModelManager::~GLModelManager() { SPADES_MARK_FUNCTION(); }

		Handle<GLModel> GLModelManager::RegisterModel(const char* name) {
			SPADES_MARK_FUNCTION();

			auto it = models.find(std::string(name));
			if (it == models.end()) {
				Handle<GLOptimizedVoxelModel> m = CreateModel(name);
				models[name] = m;
				return m.Cast<GLModel>();
			}
			return it->second.Cast<GLModel>();
		}

		Handle<GLOptimizedVoxelModel> GLModelManager::CreateModel(const char* name) {
			SPADES_MARK_FUNCTION();

			auto voxelModel = VoxelModelLoader::Load(name);
			return Handle<GLOptimizedVoxelModel>::New(voxelModel.GetPointerOrNull(), renderer);
		}

		void GLModelManager::ClearCache() { models.clear(); }

		void GLModelManager::ReloadResources(const ResourceChanges& changes) {
			SPADES_MARK_FUNCTION();

			for (const auto& entry : models) {
				if (!changes.Contains(entry.first) &&
				    !changes.Contains(VoxelModelLoader::GetMetadataPath(entry.first)))
					continue;
				// Gone with a disabled mod: keep what is loaded.
				if (!FileManager::FileExists(entry.first.c_str()))
					continue;
				try {
					entry.second->Replace(*CreateModel(entry.first.c_str()));
				} catch (const std::exception& ex) {
					// Broken: keep what is loaded.
					SPLog("Model '%s' not reloaded: %s", entry.first.c_str(), ex.what());
				}
			}
		}
	} // namespace draw
} // namespace spades