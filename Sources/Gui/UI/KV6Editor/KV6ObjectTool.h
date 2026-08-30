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

#include "KV6EditorTool.h"
#include "KV6Gizmo.h"
#include <Core/Math.h>
#include <memory>

namespace spades {
	namespace gui {
		// Object mode: create, select, move, and rotate objects in .2kv6 scenes.
		// When not in .2kv6 mode, acts as a no-op (greyed out in toolbar).
		class ObjectTool : public EditorTool {
		public:
			const char* Label() const override { return "Object"; }
			ToolOptions* Options() override { return nullptr; }
			int SubToolCount() const override { return 0; }
			const char* SubToolLabel(int) const override { return ""; }
			int ActiveSubTool() const override { return 0; }
			void SetSubTool(IEditorContext&, int) override {}

			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;
			void DrawOverlay(IEditorContext&) override;

		private:
			std::unique_ptr<Gizmo> gizmo;
			int gizmoMode = Gizmo::Move; // Current mode displayed
			int gizmoAxis = -1; // Axis being dragged (-1 = none)
			Vector2 gizmoDragStart;

			void CreateNewObject(IEditorContext& ctx, const std::string& name);
			void DeleteActiveObject(IEditorContext& ctx);
		};
	} // namespace gui
} // namespace spades
