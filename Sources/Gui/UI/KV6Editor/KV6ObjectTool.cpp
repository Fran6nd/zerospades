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
#include "KV6Gizmo.h"

#include <cmath>
#include <string>

namespace spades {
	namespace gui {
		void ObjectNewTool::OnActivate(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;
			ctx.CreateSceneObject("");
			ctx.SetStatus("Created new object");
		}

		void ObjectDeleteTool::OnActivate(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;
			if (ctx.DeleteActiveSceneObject()) {
				ctx.SetStatus("Deleted object");
			}
		}

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

		// --- ObjectMoveSubTool ---
		class ObjectMoveSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Move"; }
			void OnActivate(IEditorContext& ctx) override;
			void OnPointer(IEditorContext& ctx, const PointerInput& input) override;
			bool OnEscape(IEditorContext& ctx) override;
			void DrawScene(IEditorContext& ctx) override;
			void DrawOverlay(IEditorContext& ctx) override;
			void SetObjectTool(ObjectTool* tool) { objectTool = tool; }

		private:
			ObjectTool* objectTool = nullptr;
			std::unique_ptr<Gizmo> gizmo;
			int gizmoAxis = -1;
			Vector2 gizmoDragStartCursor;
			Vector3 gizmoDragStartOrigin;

			int HitAxis(IEditorContext& ctx, const Vector3& origin) const;
			float OffsetAlong(IEditorContext& ctx, const Vector3& origin, int axis) const;
		};

		void ObjectMoveSubTool::OnActivate(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			gizmoAxis = -1;
		}

		int ObjectMoveSubTool::HitAxis(IEditorContext& ctx, const Vector3& origin) const {
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

		float ObjectMoveSubTool::OffsetAlong(IEditorContext& ctx, const Vector3& origin, int axis) const {
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
			return std::round(raw * 10.0F) / 10.0F;
		}

		void ObjectMoveSubTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			if (!ctx.IsScene2KV6())
				return;

			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();

			Vector3 selectedObjectOrigin = ctx.GetPivot();

			if (!input.IsLeft())
				return;

			if (input.IsDown()) {
				int best = HitAxis(ctx, selectedObjectOrigin);
				if (best >= 0) {
					gizmoAxis = best;
					gizmoDragStartCursor = ctx.CursorPos();
					gizmoDragStartOrigin = selectedObjectOrigin;
					ctx.SetStatus("Moving object axis " + std::to_string(gizmoAxis));
				} else {
					// No gizmo hit - try to select an object by clicking on it
					if (ctx.SelectObjectAtCursor()) {
						ctx.SetStatus("Selected object");
					}
				}
			} else if (input.IsDrag()) {
				if (gizmoAxis < 0)
					return;

				float offset = OffsetAlong(ctx, gizmoDragStartOrigin, gizmoAxis);

				// Get the axis direction based on global/local space
				Vector3 axis = AxisUnit(gizmoAxis);
				if (!objectTool->GetGlobalTransformSpace()) {
					// Local space: rotate axis by object rotation
					Quaternion rot(ctx.GetObjectRotation());
					axis = rot.Apply(axis);
				}

				Vector3 newOrigin = gizmoDragStartOrigin + axis * offset;
				ctx.PreviewPivot(newOrigin);
				ctx.SetStatus("Move axis " + std::to_string(gizmoAxis) + " offset: " + std::to_string(offset));
			} else if (input.IsUp()) {
				if (gizmoAxis >= 0) {
					float offset = OffsetAlong(ctx, gizmoDragStartOrigin, gizmoAxis);
					if (offset != 0.0F) {
						// Get the axis direction based on global/local space
						Vector3 axis = AxisUnit(gizmoAxis);
						if (!objectTool->GetGlobalTransformSpace()) {
							// Local space: rotate axis by object rotation
							Quaternion rot(ctx.GetObjectRotation());
							axis = rot.Apply(axis);
						}

						Vector3 newOrigin = gizmoDragStartOrigin + axis * offset;
						ctx.PreviewPivot(gizmoDragStartOrigin);
						ctx.SetPivot(newOrigin);
					}
					ctx.SetStatus("");
				}
				gizmoAxis = -1;
			}
		}

		bool ObjectMoveSubTool::OnEscape(IEditorContext& ctx) {
			if (gizmoAxis < 0)
				return false;
			gizmoAxis = -1;
			return true;
		}

		void ObjectMoveSubTool::DrawScene(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();

			// Draw object outline
			Vector4 outlineColor = MakeVector4(1.0F, 1.0F, 0.3F, 1.0F);
			ctx.DrawObjectOutline(ctx.GetActiveObjectIndex(), outlineColor);

			// Draw axis lines
			const Vector4 kAxisCol[3] = {
				MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
				MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
				MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)
			};

			int hover = (gizmoAxis < 0) ? HitAxis(ctx, origin) : -1;

			// Determine axes based on global/local space
			Vector3 axes[3];
			if (!objectTool->GetGlobalTransformSpace()) {
				// Local space: rotate axes by object rotation
				Quaternion rot(ctx.GetObjectRotation());
				axes[0] = rot.Apply(AxisUnit(0));
				axes[1] = rot.Apply(AxisUnit(1));
				axes[2] = rot.Apply(AxisUnit(2));
			} else {
				// Global space: use world axes
				axes[0] = AxisUnit(0);
				axes[1] = AxisUnit(1);
				axes[2] = AxisUnit(2);
			}

			for (int a = 0; a < 3; a++) {
				bool active = (gizmoAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 axisEnd = origin + axes[a] * kGizLen;
				ctx.DrawLine3D(origin, axisEnd, col);
			}
		}

		void ObjectMoveSubTool::DrawOverlay(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();

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

		// --- ObjectRotateSubTool ---
		class ObjectRotateSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Rotate"; }
			void OnActivate(IEditorContext& ctx) override;
			void OnPointer(IEditorContext& ctx, const PointerInput& input) override;
			bool OnEscape(IEditorContext& ctx) override;
			void DrawScene(IEditorContext& ctx) override;
			void DrawOverlay(IEditorContext& ctx) override;
			void SetObjectTool(ObjectTool* tool) { objectTool = tool; }

		private:
			ObjectTool* objectTool = nullptr;
			std::unique_ptr<Gizmo> gizmo;
			int gizmoAxis = -1;
			Vector2 gizmoDragStartCursor;
			Vector4 gizmoDragStartRotation;

			int HitAxis(IEditorContext& ctx, const Vector3& origin) const;
		};

		void ObjectRotateSubTool::OnActivate(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			gizmoAxis = -1;
		}

		int ObjectRotateSubTool::HitAxis(IEditorContext& ctx, const Vector3& origin) const {
			bool ok0;
			Vector2 s0 = ctx.WorldToScreen(origin, ok0);
			if (!ok0)
				return -1;
			Vector2 cur = ctx.CursorPos();
			int best = -1;
			float bestDist = kHitDist * 1.5F;
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

		void ObjectRotateSubTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			if (!ctx.IsScene2KV6())
				return;

			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();

			Vector3 origin = ctx.GetPivot();

			if (!input.IsLeft())
				return;

			if (input.IsDown()) {
				int best = HitAxis(ctx, origin);
				if (best >= 0) {
					gizmoAxis = best;
					gizmoDragStartCursor = ctx.CursorPos();
					gizmoDragStartRotation = ctx.GetObjectRotation();
					ctx.SetStatus("Rotating object axis " + std::to_string(gizmoAxis));
				} else {
					// No gizmo hit - try to select an object by clicking on it
					if (ctx.SelectObjectAtCursor()) {
						ctx.SetStatus("Selected object");
					}
				}
			} else if (input.IsDrag()) {
				if (gizmoAxis < 0)
					return;

				// Compute rotation based on cursor movement
				bool ok1, ok2;
				Vector2 s0 = ctx.WorldToScreen(origin, ok1);
				Vector2 sa = ctx.WorldToScreen(origin + AxisUnit(gizmoAxis), ok2);
				if (!ok1 || !ok2)
					return;

				Vector2 da = sa - s0;
				float dl = da.GetLength();
				if (dl < 0.5F)
					return;

				Vector2 m = ctx.CursorPos() - gizmoDragStartCursor;
				float angle = Vector2::Dot(m, da) / (dl * dl) * 0.1F;

				// Get the rotation axis based on global/local space
				Vector3 axis = AxisUnit(gizmoAxis);
				if (!objectTool->GetGlobalTransformSpace()) {
					// Local space: rotate axis by object's current rotation
					Quaternion rot(gizmoDragStartRotation);
					axis = rot.Apply(axis);
				}

				// Create a quaternion rotation around the axis
				float halfAngle = angle * 0.5F;
				Vector4 deltaQuat = MakeVector4(
					axis.x * std::sin(halfAngle),
					axis.y * std::sin(halfAngle),
					axis.z * std::sin(halfAngle),
					std::cos(halfAngle)
				);

				// Multiply quaternions: newQuat = deltaQuat * startQuat
				Quaternion start(gizmoDragStartRotation);
				Quaternion delta(deltaQuat);
				Quaternion result = delta * start;

				ctx.PreviewObjectRotation(result.v);
				ctx.SetStatus("Rotate axis " + std::to_string(gizmoAxis));
			} else if (input.IsUp()) {
				if (gizmoAxis >= 0) {
					Vector4 currentRot = ctx.GetObjectRotation();
					if (currentRot != gizmoDragStartRotation) {
						ctx.SetObjectRotation(currentRot);
					}
					ctx.SetStatus("");
				}
				gizmoAxis = -1;
			}
		}

		bool ObjectRotateSubTool::OnEscape(IEditorContext& ctx) {
			if (gizmoAxis < 0)
				return false;
			// Revert rotation to start value
			ctx.PreviewObjectRotation(gizmoDragStartRotation);
			gizmoAxis = -1;
			return true;
		}

		void ObjectRotateSubTool::DrawScene(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();

			// Draw object outline
			Vector4 outlineColor = MakeVector4(1.0F, 1.0F, 0.3F, 1.0F);
			ctx.DrawObjectOutline(ctx.GetActiveObjectIndex(), outlineColor);

			// Draw gizmo with rotation
			Vector4 rotation = ctx.GetObjectRotation();
			if (gizmo)
				gizmo->Draw(ctx, origin, Gizmo::Rotate, &rotation);
		}

		void ObjectRotateSubTool::DrawOverlay(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();
			int hover = (gizmoAxis < 0) ? HitAxis(ctx, origin) : -1;

			const Vector4 kAxisCol[3] = {
				MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
				MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
				MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)
			};

			for (int a = 0; a < 3; a++) {
				bool active = (gizmoAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 handle = origin + AxisUnit(a) * kGizLen;
				float size = active ? 1.05F : 0.8F;
				ctx.DrawSolidCube(handle, size, col);
			}
		}

		// --- ObjectScaleSubTool ---
		class ObjectScaleSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Scale"; }
			void OnActivate(IEditorContext& ctx) override;
			void OnPointer(IEditorContext& ctx, const PointerInput& input) override;
			bool OnEscape(IEditorContext& ctx) override;
			void DrawScene(IEditorContext& ctx) override;
			void DrawOverlay(IEditorContext& ctx) override;
			void SetObjectTool(ObjectTool* tool) { objectTool = tool; }

		private:
			ObjectTool* objectTool = nullptr;
			std::unique_ptr<Gizmo> gizmo;
			int gizmoAxis = -1;
			Vector2 gizmoDragStartCursor;
			Vector3 gizmoDragStartScale;

			int HitAxis(IEditorContext& ctx, const Vector3& origin) const;
			float OffsetAlong(IEditorContext& ctx, const Vector3& origin, int axis) const;
		};

		void ObjectScaleSubTool::OnActivate(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			gizmoAxis = -1;
		}

		int ObjectScaleSubTool::HitAxis(IEditorContext& ctx, const Vector3& origin) const {
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

		float ObjectScaleSubTool::OffsetAlong(IEditorContext& ctx, const Vector3& origin, int axis) const {
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
			return std::round(raw * 10.0F) / 10.0F;
		}

		void ObjectScaleSubTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			if (!ctx.IsScene2KV6())
				return;

			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();

			Vector3 origin = ctx.GetPivot();

			if (!input.IsLeft())
				return;

			if (input.IsDown()) {
				int best = HitAxis(ctx, origin);
				if (best >= 0) {
					gizmoAxis = best;
					gizmoDragStartCursor = ctx.CursorPos();
					gizmoDragStartScale = ctx.GetObjectScale();
					ctx.SetStatus("Scaling object axis " + std::to_string(gizmoAxis));
				} else {
					// No gizmo hit - try to select an object by clicking on it
					if (ctx.SelectObjectAtCursor()) {
						ctx.SetStatus("Selected object");
					}
				}
			} else if (input.IsDrag()) {
				if (gizmoAxis < 0)
					return;

				float offset = OffsetAlong(ctx, origin, gizmoAxis);

				// For scaling, we need to determine which scale component to modify
				// In local space, the gizmo axis is rotated, but scale is still per-axis
				// So we apply offset to the axis corresponding to the drag direction
				Vector3 newScale = gizmoDragStartScale;
				newScale.x = gizmoAxis == 0 ? gizmoDragStartScale.x + offset : gizmoDragStartScale.x;
				newScale.y = gizmoAxis == 1 ? gizmoDragStartScale.y + offset : gizmoDragStartScale.y;
				newScale.z = gizmoAxis == 2 ? gizmoDragStartScale.z + offset : gizmoDragStartScale.z;

				// Clamp to minimum scale
				newScale.x = std::max(0.1F, newScale.x);
				newScale.y = std::max(0.1F, newScale.y);
				newScale.z = std::max(0.1F, newScale.z);

				ctx.PreviewObjectScale(newScale);
				ctx.SetStatus("Scale axis " + std::to_string(gizmoAxis) + " offset: " + std::to_string(offset));
			} else if (input.IsUp()) {
				if (gizmoAxis >= 0) {
					Vector3 currentScale = ctx.GetObjectScale();
					if (currentScale != gizmoDragStartScale) {
						ctx.SetObjectScale(currentScale);
					}
					ctx.SetStatus("");
				}
				gizmoAxis = -1;
			}
		}

		bool ObjectScaleSubTool::OnEscape(IEditorContext& ctx) {
			if (gizmoAxis < 0)
				return false;
			ctx.PreviewObjectScale(gizmoDragStartScale);
			gizmoAxis = -1;
			return true;
		}

		void ObjectScaleSubTool::DrawScene(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();

			// Draw object outline
			Vector4 outlineColor = MakeVector4(1.0F, 1.0F, 0.3F, 1.0F);
			ctx.DrawObjectOutline(ctx.GetActiveObjectIndex(), outlineColor);

			// Draw gizmo with rotation
			Vector4 rotation = ctx.GetObjectRotation();
			if (gizmo)
				gizmo->Draw(ctx, origin, Gizmo::Scale, &rotation);
		}

		void ObjectScaleSubTool::DrawOverlay(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			Vector3 origin = ctx.GetPivot();
			int hover = (gizmoAxis < 0) ? HitAxis(ctx, origin) : -1;

			const Vector4 kAxisCol[3] = {
				MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
				MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
				MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)
			};

			for (int a = 0; a < 3; a++) {
				bool active = (gizmoAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 handle = origin + AxisUnit(a) * kGizLen;
				float size = active ? 1.05F : 0.8F;
				ctx.DrawSolidCube(handle, size, col);
			}
		}

		// --- ObjectTool (Container) ---
		ObjectTool::ObjectTool() {
			options.AddBool("space.global", "Global", "Transform Space");

			// Create subtools
			auto moveTool = std::make_unique<ObjectMoveSubTool>();
			auto rotateTool = std::make_unique<ObjectRotateSubTool>();
			auto scaleTool = std::make_unique<ObjectScaleSubTool>();

			moveTool->SetObjectTool(this);
			rotateTool->SetObjectTool(this);
			scaleTool->SetObjectTool(this);

			subs.push_back(std::move(moveTool));
			subs.push_back(std::move(rotateTool));
			subs.push_back(std::move(scaleTool));

			active = 0; // Default to Move
		}

		void ObjectTool::OnKey(IEditorContext& ctx, const KeyInput& input) {
			if (!input.IsDown())
				return;

			if (!ctx.IsScene2KV6()) {
				ContainerTool::OnKey(ctx, input);
				return;
			}

			// Gizmo mode selection
			if (input.key == "g") {
				SetSubTool(ctx, 0);
				ctx.SetStatus("Move mode (G)");
			} else if (input.key == "r") {
				SetSubTool(ctx, 1);
				ctx.SetStatus("Rotate mode (R)");
			} else if (input.key == "s") {
				SetSubTool(ctx, 2);
				ctx.SetStatus("Scale mode (S)");
			}
			// Object creation / deletion
			else if (input.key == "n" && input.shift) {
				CreateNewObject(ctx, "");
			} else if (input.key == "Delete" || input.key == "Backspace") {
				DeleteActiveObject(ctx);
			}
			// Object selection
			else if (input.key == "Tab" && input.shift) {
				SelectPreviousObject(ctx);
			} else if (input.key == "Tab") {
				SelectNextObject(ctx);
			}
			// Space toggle for global/local transform
			else if (input.key == " ") {
				useGlobalSpace = !useGlobalSpace;
				ctx.SetStatus(useGlobalSpace ? "Global transform space" : "Local transform space");
				if (options.Count() > 0)
					options.At(0).bvalue = useGlobalSpace;
			} else {
				ContainerTool::OnKey(ctx, input);
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

			size_t currentIndex = ctx.GetActiveObjectIndex();
			size_t count = ctx.GetObjectCount();
			if (count > 0) {
				size_t nextIndex = (currentIndex + 1) % count;
				ctx.SetActiveObjectIndex(nextIndex);
				ctx.SetStatus("Selected object " + std::to_string(nextIndex));
			}
		}

		void ObjectTool::SelectPreviousObject(IEditorContext& ctx) {
			if (!ctx.IsScene2KV6())
				return;

			size_t currentIndex = ctx.GetActiveObjectIndex();
			size_t count = ctx.GetObjectCount();
			if (count > 0) {
				size_t prevIndex = (currentIndex == 0) ? count - 1 : currentIndex - 1;
				ctx.SetActiveObjectIndex(prevIndex);
				ctx.SetStatus("Selected object " + std::to_string(prevIndex));
			}
		}

		// Factory functions for object mode tools
		std::unique_ptr<EditorTool> CreateObjectNewTool() {
			return std::make_unique<ObjectNewTool>();
		}

		std::unique_ptr<EditorTool> CreateObjectDeleteTool() {
			return std::make_unique<ObjectDeleteTool>();
		}
	} // namespace gui
} // namespace spades
