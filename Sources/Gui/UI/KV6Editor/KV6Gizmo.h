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

#include <Core/Math.h>

namespace spades {
	namespace gui {
		class IEditorContext;

		// Industry-standard 3D transformation gizmos (move, rotate, scale).
		// Renders all three simultaneously; user selects which to interact with.
		class Gizmo {
		public:
			enum Mode {
				None = 0,
				Move = 1,
				Rotate = 2,
				Scale = 4,
				All = Move | Rotate | Scale,
			};

			Gizmo();

			// Draw gizmos at `origin` (world position).
			void Draw(IEditorContext& ctx, const Vector3& origin, int visibleModes = All);

			// Check if cursor is over a gizmo axis/component; returns axis index (0=X, 1=Y, 2=Z, -1=none)
			int HitTest(IEditorContext& ctx, const Vector3& origin, Mode mode);

			// Apply transformation: returns the delta to add to the object's transform.
			// `axis`: 0=X, 1=Y, 2=Z; `delta`: mouse movement amount.
			Vector3 ApplyMove(IEditorContext& ctx, int axis, const Vector2& delta);
			Vector4 ApplyRotate(IEditorContext& ctx, int axis, const Vector2& delta);
			Vector3 ApplyScale(IEditorContext& ctx, int axis, const Vector2& delta);

		private:
			static const float kGizmoRadius; // 3D size of gizmo
			static const float kArrowLength;
			static const float kArrowHeadSize;
		};
	} // namespace gui
} // namespace spades
