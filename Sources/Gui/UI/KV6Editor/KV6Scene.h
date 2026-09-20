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

#include <Core/Math.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace gui {
		/**
		 * Names one object of a scene for as long as the document is open, so
		 * the undo history can refer to an object without holding it: the
		 * history journals the scene's shape, while the voxels each object
		 * holds stay with the editor, exactly as a .kv6's single model does.
		 */
		using SceneObjectId = std::uint32_t;
		constexpr SceneObjectId kNoSceneObject = 0;

		/**
		 * One object of a .2kv6 scene, without its voxels: what it is called,
		 * where it sits (relative to its parent), and what hangs under it.
		 *
		 * `hasModel` says whether the editor holds voxels for this object; a
		 * group node that only positions its children has none.
		 */
		struct SceneNode {
			SceneObjectId id = kNoSceneObject;
			std::string name;
			Vector3 position = MakeVector3(0.0F, 0.0F, 0.0F);
			Vector4 rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F); // quaternion
			Vector3 scale = MakeVector3(1.0F, 1.0F, 1.0F);
			std::vector<TransformKeyframe> keyframes;
			std::vector<SceneNode> children;
			bool hasModel = false;

			bool operator==(const SceneNode& o) const;
			bool operator!=(const SceneNode& o) const { return !(*this == o); }
			/** This node and everything under it. */
			std::size_t CountWithChildren() const;
		};

		/**
		 * The shape of a .2kv6 document: its objects, and the id the next one
		 * to be created takes. Empty for a .kv6, which holds a single model and
		 * no scene at all.
		 *
		 * This is journaled state (it lives in EditState), so every change to
		 * it undoes and redoes with no further code. It is kept small for that
		 * reason: no voxels, only names and transforms.
		 */
		struct Scene {
			std::vector<SceneNode> roots;
			SceneObjectId nextId = 1; // 0 names no object

			bool Empty() const { return roots.empty(); }
			bool operator==(const Scene& o) const;
			bool operator!=(const Scene& o) const { return !(*this == o); }
			/** Heap bytes this holds; the history trims itself by these. */
			std::size_t Bytes() const;

			/** The node with `id`, or null when the scene holds no such object. */
			SceneNode* Find(SceneObjectId id);
			const SceneNode* Find(SceneObjectId id) const;
			/** The parent of `id`, or null for a root (or an unknown id). */
			SceneNode* ParentOf(SceneObjectId id);
			/** Removes `id` and everything under it; false when there is no such object. */
			bool Remove(SceneObjectId id);
			/** Every object, parents before their children. */
			std::vector<SceneObjectId> Ids() const;
			/** The first object holding voxels, or `kNoSceneObject`. */
			SceneObjectId FirstModelObject() const;
			/** Where `id` sits in the world: its transform with its parents' applied. */
			Matrix4 WorldTransform(SceneObjectId id) const;
		};

		/** The local transform of one node (translate, then turn, then scale). */
		Matrix4 LocalTransform(const SceneNode& node);
	} // namespace gui
} // namespace spades
