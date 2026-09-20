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

			/**
			 * Places the active object with the gizmo: its arrows and squares move
			 * it, its rings turn it, and its cubes scale it.
			 *
			 * The object follows the drag as a preview and the release keeps it,
			 * so a drag is one undo step however far it wandered, and Escape puts
			 * the object back where it started. A press away from every handle
			 * picks whatever object is under it instead.
			 */
			class ObjectGizmoSubTool : public GizmoSubTool {
			public:
				ObjectGizmoSubTool() : GizmoSubTool(GizmoSnap(), GizmoHandleSet::All()) {}

				const char* Label() const override { return "Place"; }
				std::string Hint(IEditorContext&) override {
					return "[LMB] pick an object  |  drag a handle to move, turn or scale it"
					       "  |  [Esc] cancel the drag";
				}

			protected:
				bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override {
					Vector3 position, scale;
					Quaternion rotation;
					if (!ed.HasScene() || !ed.GetObjectTransform(position, rotation, scale))
						return false; // no object to handle
					// The gizmo sits on the object and turns with it, so its arrows
					// point along the object's own axes as Blender's local gizmo does.
					pose.position = position;
					pose.axes[0] = rotation.Apply(MakeVector3(1.0F, 0.0F, 0.0F));
					pose.axes[1] = rotation.Apply(MakeVector3(0.0F, 1.0F, 0.0F));
					pose.axes[2] = rotation.Apply(MakeVector3(0.0F, 0.0F, 1.0F));
					return true;
				}

				void OnGizmoBegin(IEditorContext& ed) override {
					ed.GetObjectTransform(startPosition, startRotation, startScale);
				}

				void OnGizmoDrag(IEditorContext& ed) override { Apply(ed, gizmo.Total()); }

				void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override {
					if (total.IsIdentity()) {
						ed.CancelObjectTransform();
						return;
					}
					Apply(ed, total);
					ed.CommitObjectTransform(StepLabel(total));
				}

				void OnGizmoCancel(IEditorContext& ed, const GizmoTransform&) override {
					ed.CancelObjectTransform(); // back to where the drag began
				}

				// A press away from the handles picks the object under it, which is
				// how an object is chosen in the first place.
				void OnClickAway(IEditorContext& ed) override {
					const SceneObjectId under = ed.ObjectAtCursor();
					if (under != kNoSceneObject && under != ed.ActiveObject())
						ed.SetActiveObject(under);
				}

			private:
				Vector3 startPosition = MakeVector3(0.0F, 0.0F, 0.0F);
				Quaternion startRotation = Quaternion(0.0F, 0.0F, 0.0F, 1.0F);
				Vector3 startScale = MakeVector3(1.0F, 1.0F, 1.0F);

				// Where the drag so far puts the object, from where it started.
				void Apply(IEditorContext& ed, const GizmoTransform& change) {
					const Vector3 position = startPosition + change.translation;
					const Quaternion rotation = (change.rotation * startRotation).Normalize();
					const Vector3 scale =
					  MakeVector3(startScale.x * change.scale.x, startScale.y * change.scale.y,
					              startScale.z * change.scale.z);
					ed.PreviewObjectTransform(position, rotation, scale);
				}

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
			options.SetEnabled(kNewOption, ed.HasScene());
			// The scene keeps at least one object: with none there is nothing to
			// edit, and nothing worth saving either.
			options.SetEnabled(kDeleteOption,
			                   ed.ActiveObject() != kNoSceneObject && ed.ObjectCount() > 1);
			const int count = ed.ObjectCount();
			options.SetLabel(kCountOption,
			                 std::to_string(count) + (count == 1 ? " object" : " objects"));
		}

		void ObjectSelectTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kNewOption)
				ed.CreateObject();
			else if (id == kDeleteOption)
				ed.DeleteActiveObject();
			else
				GizmoTool::OnAction(ed, id);
		}

		std::string ObjectSelectTool::Hint(IEditorContext& ed) {
			if (ed.ObjectCount() == 0)
				return "[New Object] adds the first object";
			if (ed.ActiveObject() == kNoSceneObject)
				return "[LMB] pick an object";
			return std::string("[Tab] edit its voxels  |  ") +
			       "[LMB] pick an object  |  drag a handle to move, turn or scale it";
		}
	} // namespace gui
} // namespace spades
