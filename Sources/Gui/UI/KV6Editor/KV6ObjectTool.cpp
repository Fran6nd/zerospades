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
			// Future: initialize drag state
		}

		void ObjectTool::OnPointer(IEditorContext& ctx, const PointerInput& input) {
			// Object tool is only functional in .2kv6 mode
			// For now, acts as a no-op when not in .2kv6 mode
		}

		void ObjectTool::OnKey(IEditorContext& ctx, const KeyInput& input) {
			// Future: keyboard shortcuts for object creation, deletion, etc.
		}

		bool ObjectTool::OnEscape(IEditorContext& ctx) { return false; }

		void ObjectTool::DrawScene(IEditorContext& ctx) {
			// Draw bounding box of active object (future implementation)
		}

		void ObjectTool::DrawOverlay(IEditorContext& ctx) {
			// Draw object selector UI (future implementation)
		}
	} // namespace gui
} // namespace spades
