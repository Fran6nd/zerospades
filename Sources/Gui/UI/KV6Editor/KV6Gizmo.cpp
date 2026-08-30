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

#include "KV6Gizmo.h"
#include "KV6EditorContext.h"

namespace spades {
	namespace gui {
		const float Gizmo::kGizmoRadius = 3.0F;
		const float Gizmo::kArrowLength = 2.5F;
		const float Gizmo::kArrowHeadSize = 0.3F;

		Gizmo::Gizmo() {}

		void Gizmo::Draw(IEditorContext& ctx, const Vector3& origin, int visibleModes,
		                 const Vector4* localRotation) {
			// Axis colors: X=red, Y=green, Z=blue
			Vector4 colors[3] = {
				MakeVector4(1.0F, 0.2F, 0.2F, 1.0F), // Red (X)
				MakeVector4(0.2F, 1.0F, 0.2F, 1.0F), // Green (Y)
				MakeVector4(0.2F, 0.2F, 1.0F, 1.0F), // Blue (Z)
			};

			// Draw move gizmo (arrow lines along each axis)
			if (visibleModes & Move) {
				for (int i = 0; i < 3; i++) {
					Vector3 axis = MakeVector3(i == 0 ? 1.0F : 0.0F, i == 1 ? 1.0F : 0.0F,
					                           i == 2 ? 1.0F : 0.0F);
					Vector3 end = origin + axis * kArrowLength;
					ctx.DrawLine3D(origin, end, colors[i]);

					// Arrow head (simple: two short lines forming a V)
					Vector3 headBase = end - axis * kArrowHeadSize;
					Vector3 headOffset = (i == 0) ? MakeVector3(0, kArrowHeadSize * 0.5F, 0)
					                                 : (i == 1) ? MakeVector3(kArrowHeadSize * 0.5F, 0, 0)
					                                           : MakeVector3(0, kArrowHeadSize * 0.5F, 0);
					ctx.DrawLine3D(headBase, end + headOffset, colors[i]);
					ctx.DrawLine3D(headBase, end - headOffset, colors[i]);
				}
			}

			// Draw rotate gizmo (circular rings around each axis)
			if (visibleModes & Rotate) {
				const int segments = 16;
				for (int axis = 0; axis < 3; axis++) {
					Vector4 color = colors[axis];
					color.w = 0.6F; // Semi-transparent for rotate
					for (int i = 0; i < segments; i++) {
						float angle1 = (float(i) / segments) * 6.28318F;
						float angle2 = (float(i + 1) / segments) * 6.28318F;

						Vector3 p1, p2;
						if (axis == 0) { // Rotate around X
							p1 = origin + MakeVector3(0, std::cos(angle1), std::sin(angle1)) * kGizmoRadius;
							p2 = origin + MakeVector3(0, std::cos(angle2), std::sin(angle2)) * kGizmoRadius;
						} else if (axis == 1) { // Rotate around Y
							p1 = origin + MakeVector3(std::cos(angle1), 0, std::sin(angle1)) * kGizmoRadius;
							p2 = origin + MakeVector3(std::cos(angle2), 0, std::sin(angle2)) * kGizmoRadius;
						} else { // Rotate around Z
							p1 = origin + MakeVector3(std::cos(angle1), std::sin(angle1), 0) * kGizmoRadius;
							p2 = origin + MakeVector3(std::cos(angle2), std::sin(angle2), 0) * kGizmoRadius;
						}
						ctx.DrawLine3D(p1, p2, color);
					}
				}
			}

			// Draw scale gizmo (boxes at the end of each axis)
			if (visibleModes & Scale) {
				for (int i = 0; i < 3; i++) {
					Vector3 axis = MakeVector3(i == 0 ? 1.0F : 0.0F, i == 1 ? 1.0F : 0.0F,
					                           i == 2 ? 1.0F : 0.0F);
					Vector3 pos = origin + axis * kArrowLength;
					Vector4 color = colors[i];
					color.w = 0.7F;

					// Draw a small cube (6 faces as box outline)
					float h = kArrowHeadSize * 0.5F;
					IntVector3 corner = MakeIntVector3(
						(int)(pos.x - h + 0.5F), (int)(pos.y - h + 0.5F), (int)(pos.z - h + 0.5F));
					ctx.DrawBoxOutline(corner,
					                   MakeIntVector3(corner.x + 1, corner.y + 1, corner.z + 1), color);
				}
			}
		}

		int Gizmo::HitTest(IEditorContext& ctx, const Vector3& origin, Mode mode) {
			// Screen-space ray casting against gizmo axes
			bool ok;
			Vector2 screenPos = ctx.CursorPos();

			const float hitThreshold = 20.0F; // Screen pixels
			float closestDist = hitThreshold;
			int closestAxis = -1;

			for (int axis = 0; axis < 3; axis++) {
				Vector3 axisDir = MakeVector3(axis == 0 ? 1.0F : 0.0F, axis == 1 ? 1.0F : 0.0F,
				                              axis == 2 ? 1.0F : 0.0F);
				Vector3 axisEnd = origin + axisDir * kArrowLength;

				Vector2 startScreen = ctx.WorldToScreen(origin, ok);
				if (!ok) continue;
				Vector2 endScreen = ctx.WorldToScreen(axisEnd, ok);
				if (!ok) continue;

				Vector2 axisScreenVec = endScreen - startScreen;
				float axisScreenLen = sqrtf(axisScreenVec.x * axisScreenVec.x + axisScreenVec.y * axisScreenVec.y);
				if (axisScreenLen < 0.01F) continue;

				Vector2 toCursor = MakeVector2(screenPos.x - startScreen.x, screenPos.y - startScreen.y);
				float dotProd = toCursor.x * axisScreenVec.x + toCursor.y * axisScreenVec.y;
				float t = dotProd / (axisScreenLen * axisScreenLen);
				t = (t < 0.0F) ? 0.0F : (t > 1.0F) ? 1.0F : t;

				Vector2 closest = MakeVector2(startScreen.x + axisScreenVec.x * t,
				                              startScreen.y + axisScreenVec.y * t);
				Vector2 diff = MakeVector2(screenPos.x - closest.x, screenPos.y - closest.y);
				float dist = sqrtf(diff.x * diff.x + diff.y * diff.y);

				if (dist < closestDist) {
					closestDist = dist;
					closestAxis = axis;
				}
			}

			return closestAxis;
		}

		Vector3 Gizmo::ApplyMove(IEditorContext& ctx, int axis, const Vector2& delta) {
			if (axis < 0 || axis > 2)
				return MakeVector3(0, 0, 0);

			const float moveSpeed = 0.01F;
			float dx = (axis == 0) ? delta.x * moveSpeed : 0.0F;
			float dy = (axis == 1) ? delta.x * moveSpeed : 0.0F;
			float dz = (axis == 2) ? delta.x * moveSpeed : 0.0F;
			return MakeVector3(dx, dy, dz);
		}

		Vector4 Gizmo::ApplyRotate(IEditorContext& ctx, int axis, const Vector2& delta) {
			if (axis < 0 || axis > 2)
				return MakeVector4(0, 0, 0, 1);

			const float rotSpeed = 0.01F;
			float angle = delta.x * rotSpeed;
			float halfAngle = angle * 0.5F;
			float sinHalf = std::sin(halfAngle);
			float cosHalf = std::cos(halfAngle);

			Vector3 axisVec = MakeVector3(axis == 0 ? 1.0F : 0.0F, axis == 1 ? 1.0F : 0.0F,
			                              axis == 2 ? 1.0F : 0.0F);

			return MakeVector4(axisVec.x * sinHalf, axisVec.y * sinHalf, axisVec.z * sinHalf,
			                   cosHalf);
		}

		Vector3 Gizmo::ApplyScale(IEditorContext& ctx, int axis, const Vector2& delta) {
			if (axis < 0 || axis > 2)
				return MakeVector3(1, 1, 1);

			const float scaleSpeed = 0.01F;
			float scaleFactor = 1.0F + (delta.x * scaleSpeed);
			scaleFactor = (scaleFactor < 0.1F) ? 0.1F : (scaleFactor > 10.0F) ? 10.0F : scaleFactor;

			float sx = (axis == 0) ? scaleFactor : 1.0F;
			float sy = (axis == 1) ? scaleFactor : 1.0F;
			float sz = (axis == 2) ? scaleFactor : 1.0F;
			return MakeVector3(sx, sy, sz);
		}
	} // namespace gui
} // namespace spades
