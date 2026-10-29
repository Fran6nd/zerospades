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
#include <Gui/UI/Editor/Voxel/VoxelSceneHost.h>

namespace spades {
	namespace gui {
		// A SceneObjectId (VoxelSceneHost.h) names an object for as long as the
		// document is open, so the history can refer to an object without
		// holding it: it journals the scene's shape, while the voxels each
		// object holds stay with the editor.

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
			// Held in place by a template (a character skin's joints): it keeps
			// its name, its parent, its turn and its scale, and stays, until it
			// is unlocked on purpose. It may still be moved.
			bool locked = false;
			// Out of sight, with everything under it: not drawn, not picked in
			// the view, and saved that way.
			bool hidden = false;

			bool operator==(const SceneNode& o) const;
			bool operator!=(const SceneNode& o) const { return !(*this == o); }
			/** This node and everything under it. */
			std::size_t CountWithChildren() const;
		};

		/**
		 * The shape of a .2kv6 document: its objects, and the id the next one
		 * to be created takes.
		 *
		 * This is journaled state (the scene editor's, see IVoxelSceneHost), so
		 * every change to it undoes and redoes with no further code. It is kept
		 * small for that reason: no voxels, only names and transforms.
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
			const SceneNode* ParentOf(SceneObjectId id) const;
			/**
			 * The scene's root: the node every object hangs under, holding no
			 * voxels of its own. A scene made here always has one; one loaded
			 * from a file may not, and then objects sit at the top themselves.
			 * A locked node is a template's own object, never a root.
			 */
			SceneNode* Root();
			const SceneNode* Root() const;
			/** Where a new object belongs: under the root, or at the top. */
			std::vector<SceneNode>& ObjectHome();
			/** The objects, which is every node holding voxels (never the root). */
			std::vector<SceneObjectId> ObjectIds() const;
			/** Removes `id` and everything under it; false when there is no such object. */
			bool Remove(SceneObjectId id);
			/** Whether `id` hangs somewhere under `ancestor`. */
			bool IsUnder(SceneObjectId id, SceneObjectId ancestor) const;
			/**
			 * Of `ids`, the ones not under another of them, in tree order:
			 * moving those carries the rest along.
			 */
			std::vector<SceneObjectId> TopMost(const std::vector<SceneObjectId>& ids) const;
			/**
			 * Whether `ids` may be moved under `parent` (`kNoSceneObject`: back
			 * to where new objects go, see ObjectHome). Every one must exist,
			 * be unlocked and not be the root, and no object may go under itself
			 * or under anything of its own, which would cut that branch off the
			 * tree.
			 */
			bool CanMove(const std::vector<SceneObjectId>& ids, SceneObjectId parent) const;
			/**
			 * Moves `ids`, with everything under them, to hang under `parent`
			 * just before its child `before` (at the end when `before` is none,
			 * or not one of its children), keeping their order. Transforms are
			 * left alone: they now read relative to the new parent. False, with
			 * nothing changed, when CanMove refuses.
			 */
			bool Move(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
			          SceneObjectId before);
			/**
			 * As Move, but each object stays where it is in the world: under a
			 * new parent its transform is rewritten to read the same from there.
			 * One only reordered among its siblings keeps its transform as is.
			 */
			bool MoveKeepingWorld(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
			                      SceneObjectId before);
			/** Whether `id` is drawn: neither it nor anything above it is hidden. */
			bool IsShown(SceneObjectId id) const;
			/** Whether any of `ids`, or anything under them, is locked. */
			bool AnyLocked(const std::vector<SceneObjectId>& ids) const;
			/** `ids` and everything under them, in tree order, each once. */
			std::vector<SceneObjectId> WithDescendants(const std::vector<SceneObjectId>& ids) const;
			/** What the objects at the top level hang from: the root's placing, or none. */
			Matrix4 TopLevelTransform() const;
			/**
			 * A scene that had no root of its own can be left by an edit with a
			 * single group at the top, which Root() would then take for the root
			 * and hide. Given whether it had a root before the edit, this wraps
			 * the top level in a new root when that happened, so the group stays
			 * an object like any other.
			 */
			void KeepTopLevel(bool hadRoot);
			/** Every object, parents before their children. */
			std::vector<SceneObjectId> Ids() const;
			/** The first object holding voxels, or `kNoSceneObject`. */
			SceneObjectId FirstModelObject() const;
			/** Where `id` sits in the world: its transform with its parents' applied. */
			Matrix4 WorldTransform(SceneObjectId id) const;
			/** What the parents of `id` impose on it; identity for a root. */
			Matrix4 ParentTransform(SceneObjectId id) const;
		};

		/** The local transform of one node (translate, then turn, then scale). */
		Matrix4 LocalTransform(const SceneNode& node);

		/** Where an object sits, and how it is turned and scaled, in its parent. */
		struct ObjectTransform {
			Vector3 position = MakeVector3(0.0F, 0.0F, 0.0F);
			Vector4 rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F); // quaternion
			Vector3 scale = MakeVector3(1.0F, 1.0F, 1.0F);
		};

		/** What `node` holds, as a transform. */
		ObjectTransform TransformOf(const SceneNode& node);

		/**
		 * What a drag does to the world, as a matrix: scaling by `scale` about
		 * `pivot` along `axes` (the gizmo's axes, which need not be any
		 * object's), turning by `rotation` about it, then shifting.
		 *
		 * Objects dragged together are each carried by this one matrix, so they
		 * keep their places relative to one another, as Blender's median-point
		 * transform does.
		 */
		Matrix4 DragTransform(const Vector3& translation, const Quaternion& rotation,
		                      const Vector3& scale, const Vector3& pivot, const Vector3 axes[3]);

		/**
		 * `matrix` split back into a position, a turn and three scale factors.
		 *
		 * An object holds its placing in those three parts, which cannot express
		 * a shear: scaling a turned object along axes of its own gives one, and
		 * the nearest thing without it is kept instead. Blender stores the same
		 * three parts and makes the same compromise.
		 */
		ObjectTransform Decompose(const Matrix4& matrix);

		/**
		 * A turn as Euler angles in degrees: about X first, then Y, then Z,
		 * each about the fixed axes (Blender's default XYZ order), which is how
		 * a turn is shown and typed. Y stays within [-90, 90]; at either end X
		 * takes the whole of the turn about the vertical.
		 */
		Vector3 EulerDegreesOf(const Vector4& rotation);
		/** The quaternion turning by `degrees`, read as EulerDegreesOf writes them. */
		Vector4 RotationFromEulerDegrees(const Vector3& degrees);
	} // namespace gui
} // namespace spades
