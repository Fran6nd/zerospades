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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "IGLDevice.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class GameMap;
	}
	namespace draw {
		class GLRenderer;
		class GLRadiosityRenderer;

		/**
		 * Generates a shadow map of the game map.
		 *
		 * The map is seen from the sun along an oblique projection, in which the point
		 * `(x, y, z)` falls on the texel `(x, y) - shear * z`. Each texel holds the depth
		 * at which the sunlight through it first meets a voxel, the colour of that voxel
		 * and the face it met.
		 *
		 * The projection follows the sun. When the sun has moved far enough, the whole
		 * map is baked again for the new shear on a worker thread, and swapped in at once.
		 */
		class GLMapShadowRenderer {
			friend class GLRadiosityRenderer;

			struct Hit;
			class Bake;

			enum { CoarseSize = 8, CoarseBits = 3 };

			GLRenderer& renderer;
			IGLDevice& device;
			client::GameMap* map;
			IGLDevice::UInteger texture;
			IGLDevice::UInteger coarseTexture;

			int w, h, d;

			/** The projection `texture` and `bitmap` hold. */
			Vector2 shear;
			/** Whether `bitmap` was baked since the map was set. */
			bool baked;

			size_t updateBitmapPitch;
			std::vector<uint32_t> updateBitmap;

			std::vector<uint32_t> bitmap;
			std::vector<uint32_t> coarseBitmap;

			/** The bake of the whole map for a new shear, running in the background. */
			std::unique_ptr<Bake> rebake;
			/** Voxels changed since `rebake` copied the map, to bake again once it is in. */
			std::vector<IntVector3> changedSinceRebake;

			uint32_t GeneratePixel(int x, int y);
			void MarkUpdate(int x, int y);
			/** Marks the texels whose sunlight passes through the voxel. */
			void MarkVoxelUpdate(int x, int y, int z);

			template <class SolidColumn>
			static Hit TraceSunlight(const SolidColumn& solidColumn, int x, int y, Vector2 shear);

			/**
			 * Starts a rebake when the sun has left the projection behind, and swaps a
			 * finished one in. Returns whether it swapped one in.
			 */
			bool FollowSun();
			void CompleteRebake();

			/** Takes a finished bake into `bitmap` and its shear. */
			void Install(const Bake&);

			/** The voxel a texel's sunlight met, from the texel and its pixel. */
			IntVector3 GetHitVoxel(int x, int y, uint32_t pixel) const;

		public:
			/** The face of a voxel the sunlight through a texel met. */
			enum class Face { Top, SideX, SideY };

			/**
			 * The shear for sunlight from `sunDirection`, the unit vector toward the sun.
			 * The sun is kept at least as high as the longest shadow the bake supports.
			 */
			static Vector2 ShearForSun(Vector3 sunDirection);

			/** The unit vector toward the sun whose light the shear projects along. */
			static Vector3 SunDirectionForShear(Vector2 shear);

			GLMapShadowRenderer(GLRenderer& renderer, client::GameMap* map);
			~GLMapShadowRenderer();

			void GameMapChanged(int x, int y, int z, client::GameMap*);

			void Update();

			IGLDevice::UInteger GetTexture() { return texture; }
			IGLDevice::UInteger GetCoarseTexture() { return coarseTexture; }

			/** The projection the textures hold; shaders project with it. */
			Vector2 GetShear() const { return shear; }

			/** The unit vector toward the sun the textures were baked for. */
			Vector3 GetSunDirection() const { return SunDirectionForShear(shear); }
		};
	} // namespace draw
} // namespace spades
