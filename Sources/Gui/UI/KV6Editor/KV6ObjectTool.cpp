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

#include <string>

namespace spades {
	namespace gui {
		ObjectTool::ObjectTool() {
			// Transform space toggle: global (world-aligned) vs local (object-aligned)
			options.AddBool("space.global", "Global", "Transform Space");
		}

		void ObjectTool::OnActivate(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			gizmoMode = Gizmo::Move;
			gizmoAxis = -1;
			// Read the space toggle from options (default: global)
			useGlobalSpace = options.Count() > 0 ? !options.At(0).bvalue : true;
		}

		void ObjectTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			if (!ctx.IsScene2KV6())
				return;

			if (input.IsDrag()) {
				if (gizmoAxis >= 0) {
					// Drag in progress: apply transformation
					Vector2 delta = input.pos - gizmoDragStart;

					if (gizmoMode == Gizmo::Move) {
						Vector3 movement = gizmo->ApplyMove(ctx, gizmoAxis, delta);
						// Future: apply movement to active object
						ctx.SetStatus("Move: " + std::to_string(gizmoAxis));
					} else if (gizmoMode == Gizmo::Rotate) {
						Vector4 rotation = gizmo->ApplyRotate(ctx, gizmoAxis, delta);
						// Future: apply rotation to active object
						ctx.SetStatus("Rotate: " + std::to_string(gizmoAxis));
					} else if (gizmoMode == Gizmo::Scale) {
						Vector3 scale = gizmo->ApplyScale(ctx, gizmoAxis, delta);
						// Future: apply scale to active object
						ctx.SetStatus("Scale: " + std::to_string(gizmoAxis));
					}

					gizmoDragStart = input.pos;
				}
			} else if (input.IsDown() && input.IsLeft()) {
				// Click: start drag if over gizmo
				gizmoDragStart = input.pos;
				gizmoAxis = gizmo->HitTest(ctx, MakeVector3(0, 0, 0), (Gizmo::Mode)gizmoMode);
				if (gizmoAxis >= 0) {
					ctx.SetStatus("Gizmo axis " + std::to_string(gizmoAxis) + " selected");
				}
			} else if (input.IsUp() && input.IsLeft()) {
				// Release: end drag
				if (gizmoAxis >= 0) {
					ctx.SetStatus("");
				}
				gizmoAxis = -1;
			}
		}

		void ObjectTool::OnKey(IEditorContext& ctx, const KeyInput& input) {
			if (!input.IsDown())
				return;

			if (!ctx.IsScene2KV6())
				return;

			// Gizmo mode selection (industry standard)
			if (input.key == "g") {
				gizmoMode = Gizmo::Move;
				ctx.SetStatus("Move mode (G)");
			} else if (input.key == "r") {
				gizmoMode = Gizmo::Rotate;
				ctx.SetStatus("Rotate mode (R)");
			} else if (input.key == "s") {
				gizmoMode = Gizmo::Scale;
				ctx.SetStatus("Scale mode (S)");
			}
			// Object creation / deletion
			else if (input.key == "n" && input.shift) {
				CreateNewObject(ctx, "");
			} else if (input.key == "Delete" || input.key == "Backspace") {
				DeleteActiveObject(ctx);
			}
			// Object selection / editing
			else if (input.key == "Tab" && input.shift) {
				SelectPreviousObject(ctx);
			} else if (input.key == "Tab") {
				SelectNextObject(ctx);
			} else if (input.key == "e" || input.key == "Return") {
				EnterEditMode(ctx);
			}
		}

		bool ObjectTool::OnEscape(IEditorContext& ctx) {
			gizmoAxis = -1;
			return false;
		}

		void ObjectTool::DrawScene(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();

			// Read current space mode from options
			useGlobalSpace = options.Count() > 0 ? !options.At(0).bvalue : true;

			// Draw gizmo at selected object's origin
			// Future: get actual object origin from scene[selectedObjectIndex]
			Vector3 selectedObjectOrigin = MakeVector3(0, 0, 0);
			Vector4 selectedObjectRotation = MakeVector4(0, 0, 0, 1); // Identity

			// Draw the gizmo with highlights for the selected object
			Vector4 highlightColor = MakeVector4(1.0F, 1.0F, 0.3F, 1.0F); // Yellow highlight
			ctx.DrawCellOutline(0, 0, 0, highlightColor);

			gizmo->Draw(ctx, selectedObjectOrigin, gizmoMode,
			            useGlobalSpace ? nullptr : &selectedObjectRotation);

			// Status: show selected object info
			ctx.SetStatus("Object 0 selected | Tab=cycle, E=edit, Shift+N=new, Del=delete");
		}

		void ObjectTool::DrawOverlay(IEditorContext& ctx) {
			// Show status and hint text
		}

		void ObjectTool::CreateNewObject(IEditorContext& ctx, const std::string& name) {
			if (!ctx.IsScene2KV6()) {
				ctx.SetStatus("Not in .2kv6 mode");
				return;
			}
			if (ctx.CreateSceneObject(name)) {
				ctx.SetStatus("Created object: " + (name.empty() ? "(unnamed)" : name));
			}
		}

		void ObjectTool::DeleteActiveObject(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6()) {
				ctx.SetStatus("Not in .2kv6 mode");
				return;
			}
			if (ctx.DeleteActiveSceneObject()) {
				ctx.SetStatus("Deleted object");
			}
		}

		void ObjectTool::SelectNextObject(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			// Future: cycle through root objects
			// selectedObjectIndex = (selectedObjectIndex + 1) % scene.size();
			ctx.SetStatus("Select next object (Tab)");
		}

		void ObjectTool::SelectPreviousObject(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			// Future: cycle backward through root objects
			// selectedObjectIndex = (selectedObjectIndex == 0) ? scene.size() - 1 : selectedObjectIndex - 1;
			ctx.SetStatus("Select previous object (Shift+Tab)");
		}

		void ObjectTool::EnterEditMode(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6()) {
				ctx.SetStatus("Not in .2kv6 mode");
				return;
			}

			// Future: switch to Draw mode to edit the selected object's voxels
			// The Draw tool will operate on scene[selectedObjectIndex].model
			ctx.SetStatus("Enter edit mode (E) - switch to Draw tool");
		}
	} // namespace gui
} // namespace spades
