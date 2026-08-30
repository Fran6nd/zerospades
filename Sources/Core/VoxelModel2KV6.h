/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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
#include <memory>

#include "Math.h"
#include "VoxelModel.h"

namespace spades {
	/**
	 * .2kv6 format: Next-generation KV6 with support for multiple named objects,
	 * each with their own transform (position, rotation, scale) and optional
	 * animation keyframes. This enables:
	 * - Scene graphs of named, transformed KV6 models
	 * - Per-object transform animation (no bones/skeleton)
	 * - Compatibility with single-object KV6 models
	 */

	/**
	 * Represents a single keyframe of transform animation.
	 * Animates position, rotation (quaternion), and scale independently.
	 */
	struct TransformKeyframe {
		float time; ///< Keyframe time in seconds
		Vector3 position;
		Vector4 rotation; ///< Quaternion (x, y, z, w)
		Vector3 scale;
	};

	/**
	 * Represents a named voxel object within a .2kv6 scene.
	 * Each object contains a KV6 voxel model with optional transform animation.
	 */
	struct VoxelObject {
		std::string name; ///< Object name (empty for unnamed objects)
		Handle<VoxelModel> model; ///< The KV6 voxel model data

		// Static transform (used when there are no keyframes)
		Vector3 position;
		Vector4 rotation; ///< Quaternion (x, y, z, w); identity = (0, 0, 0, 1)
		Vector3 scale;

		// Animation keyframes (if empty, object is static)
		std::vector<TransformKeyframe> keyframes;

		VoxelObject() : position(0, 0, 0), rotation(0, 0, 0, 1), scale(1, 1, 1) {}
	};

	/**
	 * .2kv6 file format handler for scenes containing multiple named voxel objects.
	 * Layout:
	 *   Magic: "2kv6" (4 bytes)
	 *   Version: uint16 (current: 1)
	 *   NumObjects: uint16
	 *   For each object:
	 *     NameLength: uint16
	 *     Name: char[NameLength]
	 *     Position: float[3]
	 *     Rotation: float[4] (quaternion)
	 *     Scale: float[3]
	 *     NumKeyframes: uint16
	 *     For each keyframe:
	 *       Time: float
	 *       Position: float[3]
	 *       Rotation: float[4]
	 *       Scale: float[3]
	 *     KV6Data: embedded KV6 (header + blocks + indices, no magic)
	 */
	class VoxelModel2KV6 {
	public:
		VoxelModel2KV6() = delete;
		VoxelModel2KV6(const VoxelModel2KV6&) = delete;
		void operator=(const VoxelModel2KV6&) = delete;

		static constexpr uint16_t FORMAT_VERSION = 1;

		/**
		 * Load a .2kv6 scene from a stream.
		 * Returns a vector of VoxelObject, one per object in the scene.
		 */
		static std::vector<VoxelObject> Load(IStream& stream);

		/**
		 * Save a .2kv6 scene to a stream.
		 * Objects should contain valid models with optional animation keyframes.
		 */
		static void Save(IStream& stream, const std::vector<VoxelObject>& objects);

	private:
		static constexpr const char* MAGIC = "2kv6";
	};

} // namespace spades
