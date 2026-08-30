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

		void Gizmo::Draw(IEditorContext& ctx, const Vector3& origin, int visibleModes) {
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
			// Future: raycast-based hit testing against gizmo geometry
			return -1; // No hit for now
		}

		Vector3 Gizmo::ApplyMove(IEditorContext& ctx, int axis, const Vector2& delta) {
			// Future: compute movement along the constrained axis
			return MakeVector3(0, 0, 0);
		}

		Vector4 Gizmo::ApplyRotate(IEditorContext& ctx, int axis, const Vector2& delta) {
			// Future: compute quaternion rotation
			return MakeVector4(0, 0, 0, 1); // Identity
		}

		Vector3 Gizmo::ApplyScale(IEditorContext& ctx, int axis, const Vector2& delta) {
			// Future: compute scale change
			return MakeVector3(1, 1, 1);
		}
	} // namespace gui
} // namespace spades
