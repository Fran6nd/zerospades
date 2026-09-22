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

#include "KV6ObjectTool.h"
#include "KV6EditorContext.h"
#include "KV6SubTool.h"

#include <cmath>

namespace spades {
	namespace gui {
		namespace {
			const char* const kNewOption = "object.new";
			const char* const kDeleteOption = "object.delete";
			const char* const kCountOption = "object.count";

			// Below these a drag has not really turned or scaled anything, so the
			// step it commits is named after what it did change.
			constexpr float kTurnedEnough = 1.0e-4F;
			constexpr float kScaledEnough = 1.0e-4F;

			// Every handle but the trackball: free rotation answers to a press
			// anywhere inside the rings, which would swallow the clicks that pick
			// another object. The rings themselves still turn the object.
			GizmoHandleSet ObjectHandles() {
				return GizmoHandleSet::Translation() | GizmoHandleSet::Scaling() |
				       GizmoHandleSet::Of(GizmoHandle::RotateX) |
				       GizmoHandleSet::Of(GizmoHandle::RotateY) |
				       GizmoHandleSet::Of(GizmoHandle::RotateZ) |
				       GizmoHandleSet::Of(GizmoHandle::RotateView);
			}

			/**
			 * Picks objects, and places the picked ones with the gizmo: its arrows
			 * and squares move them, its rings turn them, its cubes scale them.
			 *
			 * They follow a drag as a preview and the release keeps it, so a drag
			 * is one undo step however far it wandered, and Escape puts them back
			 * where they started.
			 */
			class ObjectGizmoSubTool : public GizmoSubTool {
			public:
				ObjectGizmoSubTool() : GizmoSubTool(GizmoSnap(), ObjectHandles()) {}

				const char* Label() const override { return "Place"; }
				std::string Hint(IEditorContext&) override {
					return "[LMB] pick an object  |  drag a handle to move, turn or scale it"
					       "  |  [Esc] cancel the drag";
				}

			protected:
				bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override {
					Vector3 position;
					Quaternion rotation;
					if (!ed.HasScene() || !ed.GetSelectionPose(position, rotation))
						return false; // nothing picked to handle
					// The gizmo sits at the middle of what is picked and turns with
					// the active object, as Blender's local gizmo does.
					pose.position = position;
					for (int k = 0; k < 3; k++) {
						pose.axes[k] = rotation.Apply(MakeVector3(
						  k == 0 ? 1.0F : 0.0F, k == 1 ? 1.0F : 0.0F, k == 2 ? 1.0F : 0.0F));
					}
					return true;
				}

				void OnGizmoBegin(IEditorContext& ed) override { ed.BeginObjectDrag(); }

				void OnGizmoDrag(IEditorContext& ed) override { ed.PreviewObjectDrag(gizmo.Total()); }

				void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override {
					if (total.IsIdentity()) {
						ed.CancelObjectDrag();
						return;
					}
					ed.PreviewObjectDrag(total);
					ed.CommitObjectDrag(StepLabel(total));
				}

				void OnGizmoCancel(IEditorContext& ed, const GizmoTransform&) override {
					ed.CancelObjectDrag(); // back to where the drag began
				}

				// A press away from the handles picks: the object under it alone,
				// added to what is picked with Shift, or nothing at all over empty
				// space.
				void OnClickAway(IEditorContext& ed, const PointerInput& e) override {
					// Anything a drag left pending is kept first, so picking
					// another object never throws a move away.
					ed.CommitObjectDrag("Place Object");
					const SceneObjectId under = ed.ObjectAtCursor();
					if (under == kNoSceneObject) {
						if (!e.shift)
							ed.SelectObject(kNoSceneObject); // empty space picks nothing
						return;
					}
					if (e.shift)
						ed.ToggleObjectSelected(under); // add to, or drop from, the picked
					else
						ed.SelectObject(under);
				}

			private:
				// One gizmo serves all three changes, so the step is named after
				// the one the drag actually made.
				static std::string StepLabel(const GizmoTransform& total) {
					const bool moved = total.translation.GetSquaredLength() > 0.0F;
					const bool turned = std::fabs(total.rotation.v.w) < 1.0F - kTurnedEnough;
					const bool scaled = std::fabs(total.scale.x - 1.0F) > kScaledEnough ||
					                    std::fabs(total.scale.y - 1.0F) > kScaledEnough ||
					                    std::fabs(total.scale.z - 1.0F) > kScaledEnough;
					if (moved && !turned && !scaled)
						return "Move Object";
					if (turned && !moved && !scaled)
						return "Rotate Object";
					if (scaled && !moved && !turned)
						return "Scale Object";
					return "Place Object";
				}
			};
		} // namespace

		// An object may sit anywhere, so its steps go finer than a voxel; it is on
		// no grid of its own, which is what the last argument says.
		ObjectSelectTool::ObjectSelectTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new ObjectGizmoSubTool()), {1.0F, 0.5F, 0.1F},
		                0.0F) {
			options.AddAction(kNewOption, "New Object");
			options.AddAction(kDeleteOption, "Delete Object");
			AddSnapOptions();
			options.AddLabel(kCountOption);
		}

		void ObjectSelectTool::UpdateOptions(IEditorContext& ed) {
			GizmoTool::UpdateOptions(ed);
			const std::size_t picked = ed.SelectedObjects().size();
			options.SetEnabled(kNewOption, ed.HasScene());
			options.SetEnabled(kDeleteOption, picked > 0);
			const int count = ed.ObjectCount();
			std::string readout = std::to_string(count) + (count == 1 ? " object" : " objects");
			if (picked > 1)
				readout += ", " + std::to_string(picked) + " picked";
			options.SetLabel(kCountOption, readout);
		}

		void ObjectSelectTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kNewOption)
				ed.CreateObject();
			else if (id == kDeleteOption)
				ed.DeleteSelectedObjects();
			else
				GizmoTool::OnAction(ed, id);
		}

		std::string ObjectSelectTool::Hint(IEditorContext& ed) {
			if (ed.ObjectCount() == 0)
				return "[New Object] adds the first object";
			const std::size_t picked = ed.SelectedObjects().size();
			if (picked == 0)
				return "[LMB] pick an object  |  [Shift+LMB] pick several";
			std::string hint = "[LMB] pick  |  [Shift+LMB] pick several  |  "
			                   "drag a handle to move, turn or scale";
			// One object at a time is edited, so Tab is only offered for one.
			if (picked == 1)
				hint = "[Tab] edit its voxels  |  " + hint;
			return hint;
		}
	} // namespace gui
} // namespace spades
