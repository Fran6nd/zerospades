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

#include <Gui/UI/Editor/Voxel/VoxelSceneHost.h>

#include "Scene.h"

namespace spades {
	namespace gui {
		/**
		 * Whether the scene is a character skin the game would load, checked
		 * with the game's own rules (see CharacterSkin).
		 */
		struct CharacterSkinCheck {
			// The scene holds a 'Player' at its top: it means to be a skin.
			bool isSkin = false;
			bool valid = false;
			std::string problem; // what the game would refuse it for
			// The object the problem is about, when it is one of the scene's.
			SceneObjectId culprit = kNoSceneObject;
		};

		/**
		 * What the scene editor offers the panels showing its scene (the
		 * outliner, the properties): Object mode's operations, plus the scene's
		 * whole shape and the edits only a panel makes. Every change goes
		 * through here, so a change made in a panel is the same journaled step
		 * as anywhere else, and undo takes it back.
		 */
		class ISceneEditContext : public IVoxelSceneHost {
		public:
			/** The scene as it stands: its objects, how they nest and where they sit. */
			virtual const Scene& GetScene() const = 0;
			/** The last one picked: what Edit mode works on, and whose axes the
			 *  gizmo takes. `kNoSceneObject` when nothing is picked. */
			virtual SceneObjectId ActiveObject() const = 0;
			virtual bool IsObjectSelected(SceneObjectId id) const = 0;
			/** Picks `ids` and nothing else, the last being active. One step. */
			virtual void SelectObjects(const std::vector<SceneObjectId>& ids) = 0;
			/** Picks every object, or none when every one is picked already. */
			virtual void SelectAllObjects() = 0;
			/**
			 * Renames `id`, numbered apart (".001") when another object has the
			 * name already. False when refused: no such object, the scene's root,
			 * a locked object, or a name that is blank. One undo step.
			 */
			virtual bool RenameObject(SceneObjectId id, const std::string& name) = 0;
			/**
			 * Moves `ids` to hang under `parent` before its child `before` (see
			 * Scene::Move), each one staying where it is in the world. False
			 * when refused (see Scene::CanMove). One undo step.
			 */
			virtual bool MoveObjects(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
			                         SceneObjectId before) = 0;
			/**
			 * Shows `id` placed at `transform` (in its parent) for an edit in
			 * progress, without an undo step. Like every preview it lasts until
			 * the user action ends or a command runs: commit it with
			 * SetObjectTransform before then, or the object goes back.
			 */
			virtual void PreviewObjectTransform(SceneObjectId id, const ObjectTransform& transform) = 0;
			/** Places `id` at `transform` as one undo step called `label`; nothing
			 *  when it is there already. */
			virtual void SetObjectTransform(SceneObjectId id, const ObjectTransform& transform,
			                                const std::string& label) = 0;
			/**
			 * Hangs `ids` under a new group (an object with no voxels) in the
			 * place of the active one, at the middle of them, each staying where
			 * it is in the world, and picks the group. Returns the group, or
			 * `kNoSceneObject` when there is nothing to group. One undo step.
			 */
			virtual SceneObjectId GroupObjects(const std::vector<SceneObjectId>& ids) = 0;
			/**
			 * Dissolves the groups among `ids`: what hung under each takes its
			 * place, staying where it is in the world, and is picked. False when
			 * none of `ids` is a group. One undo step.
			 */
			virtual bool UngroupObjects(const std::vector<SceneObjectId>& ids) = 0;
			/**
			 * Asks the user whether to lock or unlock `id` (see SceneNode::locked),
			 * saying what it costs, and does it as one undo step if they agree.
			 */
			virtual void AskToSetObjectLocked(SceneObjectId id, bool locked) = 0;
			/** Hides or shows `ids` (see SceneNode::hidden); one undo step. */
			virtual void SetObjectsHidden(const std::vector<SceneObjectId>& ids, bool hidden) = 0;
			/** The scene checked as a character skin; cheap to call every frame. */
			virtual CharacterSkinCheck CheckCharacterSkin() = 0;
		};
	} // namespace gui
} // namespace spades
