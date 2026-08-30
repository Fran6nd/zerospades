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

#include <cmath>
#include <string>

namespace spades {
	namespace gui {
		namespace {
			Vector3 AxisUnit(int a) {
				return MakeVector3(a == 0 ? 1.0F : 0.0F, a == 1 ? 1.0F : 0.0F, a == 2 ? 1.0F : 0.0F);
			}
			float DistToSeg(const Vector2& p, const Vector2& a, const Vector2& b) {
				Vector2 ab = b - a;
				float l2 = Vector2::Dot(ab, ab);
				float t = (l2 < 1.0e-6F) ? 0.0F : Vector2::Dot(p - a, ab) / l2;
				t = std::max(0.0F, std::min(1.0F, t));
				return (p - (a + ab * t)).GetLength();
			}
			const float kGizLen = 12.0F;
			const float kHitDist = 14.0F;
		}

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
			useGlobalSpace = options.Count() > 0 ? options.At(0).bvalue : true;
		}

		int ObjectTool::HitAxis(IEditorContext& ctx, const Vector3& origin) const {
			bool ok0;
			Vector2 s0 = ctx.WorldToScreen(origin, ok0);
			if (!ok0)
				return -1;
			Vector2 cur = ctx.CursorPos();
			int best = -1;
			float bestDist = kHitDist;
			for (int a = 0; a < 3; a++) {
				bool ok1;
				Vector2 tip = ctx.WorldToScreen(origin + AxisUnit(a) * kGizLen, ok1);
				if (!ok1)
					continue;
				float d = std::min(DistToSeg(cur, s0, tip), (cur - tip).GetLength());
				if (d < bestDist) {
					bestDist = d;
					best = a;
				}
			}
			return best;
		}

		float ObjectTool::OffsetAlong(IEditorContext& ctx, const Vector3& origin, int axis) const {
			bool ok1, ok2;
			Vector2 s0 = ctx.WorldToScreen(origin, ok1);
			Vector2 sa = ctx.WorldToScreen(origin + AxisUnit(axis), ok2);
			if (!ok1 || !ok2)
				return 0.0F;
			Vector2 da = sa - s0;
			float dl = da.GetLength();
			if (dl < 0.5F)
				return 0.0F;
			Vector2 m = ctx.CursorPos() - gizmoDragStartCursor;
			float raw = Vector2::Dot(m, da) / (dl * dl);
			return std::round(raw * 10.0F) / 10.0F; // snap to 0.1
		}

		void ObjectTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			if (!ctx.IsScene2KV6())
				return;

			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();

			Vector3 selectedObjectOrigin = ctx.GetPivot();

			if (!input.IsLeft())
				return;

			if (input.IsDown()) {
				// Click: check if over gizmo axis
				int best = HitAxis(ctx, selectedObjectOrigin);
				if (best >= 0) {
					gizmoAxis = best;
					gizmoDragStartCursor = ctx.CursorPos();
					gizmoDragStartOrigin = selectedObjectOrigin;
					ctx.SetStatus("Moving object axis " + std::to_string(gizmoAxis));
				}
			} else if (input.IsDrag()) {
				if (gizmoAxis < 0)
					return;

				// Drag: compute movement along the selected axis
				float offset = OffsetAlong(ctx, gizmoDragStartOrigin, gizmoAxis);

				if (gizmoMode == Gizmo::Move) {
					// Apply movement to object by updating pivot (live preview like PivotGizmoSubTool)
					Vector3 newOrigin = gizmoDragStartOrigin + AxisUnit(gizmoAxis) * offset;
					ctx.PreviewPivot(newOrigin);
					ctx.SetStatus("Move axis " + std::to_string(gizmoAxis) + " offset: " + std::to_string(offset));
				} else if (gizmoMode == Gizmo::Rotate) {
					ctx.SetStatus("Rotate axis " + std::to_string(gizmoAxis));
				} else if (gizmoMode == Gizmo::Scale) {
					ctx.SetStatus("Scale axis " + std::to_string(gizmoAxis));
				}
			} else if (input.IsUp()) {
				// Release: finish drag
				if (gizmoAxis >= 0) {
					float offset = OffsetAlong(ctx, gizmoDragStartOrigin, gizmoAxis);
					if (gizmoMode == Gizmo::Move && offset != 0.0F) {
						// Finalize the move
						Vector3 newOrigin = gizmoDragStartOrigin + AxisUnit(gizmoAxis) * offset;
						ctx.PreviewPivot(gizmoDragStartOrigin); // rewind preview
						ctx.SetPivot(newOrigin); // apply as single undo step
					}
					ctx.SetStatus("");
				}
				gizmoAxis = -1;
			}
		}

		bool ObjectTool::OnEscape(IEditorContext& ctx) {
			if (gizmoAxis < 0)
				return false;
			gizmoAxis = -1;
			return true;
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

		void ObjectTool::DrawScene(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			// Read current space mode from options
			useGlobalSpace = options.Count() > 0 ? options.At(0).bvalue : true;

			Vector3 origin = ctx.GetPivot();

			// Highlight the origin cell
			Vector4 highlightColor = MakeVector4(1.0F, 1.0F, 0.3F, 1.0F);
			ctx.DrawCellOutline(int(origin.x), int(origin.y), int(origin.z), highlightColor);

			// Draw axis lines (like PivotGizmoSubTool)
			const Vector4 kAxisCol[3] = {
				MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),  // X - red
				MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),    // Y - green
				MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)    // Z - blue
			};

			// Check which axis is hovered
			int hover = (gizmoAxis < 0) ? HitAxis(ctx, origin) : -1;

			// Draw each axis
			for (int a = 0; a < 3; a++) {
				bool active = (gizmoAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 axisEnd = origin + AxisUnit(a) * kGizLen;
				ctx.DrawLine3D(origin, axisEnd, col);
			}
		}

		void ObjectTool::DrawOverlay(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();

			// Draw solid cube handles at axis tips (like PivotGizmoSubTool)
			const Vector4 kAxisCol[3] = {
				MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
				MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
				MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)
			};

			int hover = (gizmoAxis < 0) ? HitAxis(ctx, origin) : -1;

			for (int a = 0; a < 3; a++) {
				bool active = (gizmoAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 handle = origin + AxisUnit(a) * kGizLen;
				float size = active ? 1.05F : 0.8F;
				ctx.DrawSolidCube(handle, size, col);
			}
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
