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

#include <string>

#include "KV6GizmoTool.h"

namespace spades {
	namespace gui {
		/**
		 * Object mode's one tool: pick an object of a .2kv6 scene and place it.
		 *
		 * Clicking an object makes it the active one, which Edit mode then works
		 * on, and the gizmo on it carries every handle at once -- arrows to move,
		 * rings to turn, cubes to scale -- so a whole object is placed without
		 * changing tools. Its bar adds and removes objects, and each drag is one
		 * undo step, named after what it changed.
		 */
		class ObjectSelectTool : public GizmoTool {
		public:
			ObjectSelectTool();
			const char* Label() const override { return "Select"; }
			EditorRole Role() const override { return EditorRole::Select; }
			void UpdateOptions(IEditorContext& ed) override;
			void OnAction(IEditorContext& ed, const std::string& id) override;
			std::string Hint(IEditorContext&) override;
		};
	} // namespace gui
} // namespace spades
