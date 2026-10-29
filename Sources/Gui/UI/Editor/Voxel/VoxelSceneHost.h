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
#include <memory>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Editor/Shell/EditorMode.h>

namespace spades {
	namespace gui {
		class JournaledHostState;
		struct GizmoTransform;

		/** Names one object of a scene for as long as the document is open. */
		using SceneObjectId = std::uint32_t;
		constexpr SceneObjectId kNoSceneObject = 0;

		/**
		 * A host whose document is a scene of objects, each holding voxels of its
		 * own and its own place in the world, as Blender has them. The editor's
		 * document is then the active object's voxels, which Edit mode changes,
		 * and Object mode picks and places whole objects through this.
		 *
		 * The host keeps the scene, and journals it beside the voxels: the
		 * editor snapshots it into every undo step and gives it back on a
		 * replay, so a change to the scene undoes like any edit. A host change
		 * of it runs inside a step of the editor's history (see
		 * VoxelEditor::History), marking it as a change of the document when
		 * the scene is saved with it.
		 */
		class IVoxelSceneHost {
		public:
			virtual ~IVoxelSceneHost() {}

			// --- What the editor asks -------------------------------------------
			/** Whether Edit mode has an object to edit: the document is then it. */
			virtual bool CanEditActiveObject() const = 0;
			/** The host's journaled state as it stands; cheap, called per step. */
			virtual std::shared_ptr<const JournaledHostState> SnapshotJournal() const = 0;
			/**
			 * An undo or redo brought `state` back: the host takes it over, and the
			 * document follows the object it makes active. The editor is in the
			 * restored mode already; `previousMode` is the one it left. Journals
			 * nothing.
			 */
			virtual void RestoreJournal(const std::shared_ptr<const JournaledHostState>& state,
			                            EditorMode previousMode) = 0;
			/** The mode is about to change: what is being typed into the host's
			 *  own panels for the mode being left is settled now. */
			virtual void ModeChanging() {}
			/** The mode changed from `previous`, inside the step changing it. */
			virtual void ModeChanged(EditorMode previous) = 0;
			/** Puts back whatever the host shows for a preview (objects being
			 *  dragged), when the editor ends its own. */
			virtual void EndPreviews() noexcept = 0;

			// --- What Object mode's tools work through --------------------------
			virtual int ObjectCount() const = 0;
			/** The objects picked, in the order they were picked. */
			virtual std::vector<SceneObjectId> SelectedObjects() const = 0;
			/** The object under the cursor, or `kNoSceneObject`. */
			virtual SceneObjectId ObjectAtCursor() = 0;
			/** Picks `id` and nothing else; `kNoSceneObject` picks nothing. One step. */
			virtual void SelectObject(SceneObjectId id) = 0;
			/** Adds `id` to the picked objects, or drops it if it is among them. */
			virtual void ToggleObjectSelected(SceneObjectId id) = 0;
			/** Adds an object, picks it, and returns it; one undo step. */
			virtual SceneObjectId CreateObject() = 0;
			/** Removes every picked object; false when none is. One undo step. */
			virtual bool DeleteSelectedObjects() = 0;
			/**
			 * Where a gizmo on the picked objects belongs: the middle of them,
			 * turned as the active one is. False when nothing is picked.
			 */
			virtual bool GetSelectionPose(Vector3& position, Quaternion& rotation) const = 0;
			/**
			 * A drag of the picked objects: `BeginObjectDrag` remembers where they
			 * are, `PreviewObjectDrag` shows them moved by `change` from there
			 * (journaling nothing), and the drag ends by keeping that as one undo
			 * step or by putting them back.
			 */
			virtual void BeginObjectDrag() = 0;
			virtual void PreviewObjectDrag(const GizmoTransform& change) = 0;
			virtual void CommitObjectDrag(const std::string& label) = 0;
			virtual void CancelObjectDrag() = 0;
		};
	} // namespace gui
} // namespace spades
