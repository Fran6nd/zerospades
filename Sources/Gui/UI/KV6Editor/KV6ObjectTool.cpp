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

namespace spades {
	namespace gui {
		void ObjectTool::OnActivate(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			gizmoMode = Gizmo::Move;
			gizmoAxis = -1;
		}

		void ObjectTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			// Object tool is only functional in .2kv6 mode (checked by toolbar at render time)
			// Drag gizmo to transform active object
			if (input.IsDrag()) {
				if (gizmoAxis >= 0) {
					// Drag in progress: apply transformation
					// Future: apply the input.delta to transform active object
				}
			} else if (input.IsDown() && input.IsLeft()) {
				// Click: start drag if over gizmo
				gizmoDragStart = input.pos;
				// gizmoAxis = gizmo->HitTest(ctx, objectOrigin, (Gizmo::Mode)gizmoMode);
			} else if (input.IsUp() && input.IsLeft()) {
				// Release: end drag
				gizmoAxis = -1;
			}
		}

		void ObjectTool::OnKey(IEditorContext& ctx, const KeyInput& input) {
			// Keyboard shortcuts for gizmo mode selection (industry standard)
			if (!input.IsDown())
				return;
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
		}

		bool ObjectTool::OnEscape(IEditorContext& ctx) {
			gizmoAxis = -1;
			return false;
		}

		void ObjectTool::DrawScene(IEditorContext& ctx) {
			if (!gizmo)
				gizmo = std::make_unique<Gizmo>();
			// Draw gizmo at active object's origin (future: get from scene)
			// gizmo->Draw(ctx, activeObjectOrigin, gizmoMode);
		}

		void ObjectTool::DrawOverlay(IEditorContext& ctx) {
			// Show status and hint text
		}
	} // namespace gui
} // namespace spades
