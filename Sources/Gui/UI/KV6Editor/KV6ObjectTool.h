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

#include "KV6ContainerTool.h"
#include "KV6ToolOptions.h"
#include <Core/Math.h>
#include <memory>

namespace spades {
	namespace gui {
		// Object mode: create, select, move, rotate, and scale objects in .2kv6 scenes.
		// When not in .2kv6 mode, acts as a no-op (greyed out in toolbar).
		class ObjectTool : public ContainerTool {
		public:
			ObjectTool();
			const char* Label() const override { return "Transform"; }
			ToolOptions* Options() override { return &options; }
			void OnKey(IEditorContext&, const KeyInput&) override;

			bool GetGlobalTransformSpace() const { return useGlobalSpace; }
			void SetGlobalTransformSpace(bool global) { useGlobalSpace = global; }

		private:
			ToolOptions options;
			bool useGlobalSpace = true; // Toggle: true=global, false=local

			void CreateNewObject(IEditorContext& ctx, const std::string& name);
			void DeleteActiveObject(IEditorContext& ctx);
			void SelectNextObject(IEditorContext& ctx);
			void SelectPreviousObject(IEditorContext& ctx);
		};

		// Factory functions for object mode tools
		std::unique_ptr<EditorTool> CreateObjectNewTool();
		std::unique_ptr<EditorTool> CreateObjectDeleteTool();
	} // namespace gui
} // namespace spades
