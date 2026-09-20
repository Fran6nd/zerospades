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

#include "KV6ToolRegistry.h"

#include "KV6DrawTool.h"
#include "KV6MirrorTool.h"
#include "KV6PaintTool.h"
#include "KV6PivotTool.h"
#include "KV6SelectTool.h"
#include "KV6ObjectTool.h"
#include "KV6TransformTool.h"

namespace spades {
	namespace gui {
		namespace {
			// Tools that edit the voxels or the selection, then tools that set
			// the model up (its mirror planes, its pivot).
			constexpr int kEditingGroup = 0;
			constexpr int kSetupGroup = 1;

			template <class T> ToolRegistry::Factory Make() {
				return [] { return std::unique_ptr<EditorTool>(new T()); };
			}
		} // namespace

		ToolRegistry& ToolRegistry::Instance() {
			static ToolRegistry registry;
			static bool seeded = false;
			if (!seeded) {
				seeded = true;
				// Built-in tools, in toolbar order: the everyday ones first, the
				// set-up ones last. Seeding here (rather than via static
				// initialisers in each tool's TU) keeps the order deterministic.
				// The keys lean on common editor conventions (Q select, B brush,
				// T transform) and keep clear of the default WASD movement keys.
				registry.Register(Make<SelectTool>(), "select", EditorMode::Edit, kEditingGroup, "Q");
				registry.Register(Make<DrawTool>(), "draw", EditorMode::Edit, kEditingGroup, "B");
				registry.Register(Make<PaintTool>(), "paint", EditorMode::Edit, kEditingGroup, "P");
				registry.Register(Make<TransformTool>(), "transform", EditorMode::Edit, kEditingGroup,
				                  "T");
				registry.Register(Make<MirrorTool>(), "mirror", EditorMode::Edit, kSetupGroup, "M");
				registry.Register(Make<PivotTool>(), "pivot", EditorMode::Edit, kSetupGroup, "O");
				// Object mode has the one tool: it picks an object of a .2kv6 scene
				// and places it, with every gizmo handle at once.
				registry.Register(Make<ObjectSelectTool>(), "objectSelect", EditorMode::Object,
				                  kEditingGroup, "Q");
			}
			return registry;
		}

		void ToolRegistry::Register(Factory f, const std::string& id, EditorMode mode, int group,
		                            const std::string& hotKey) {
			entries.push_back({std::move(f), id, mode, group, hotKey});
		}

		void ToolRegistry::BuildAll(std::vector<ToolSlot>& out) const {
			out.clear();
			out.reserve(entries.size());
			for (const Entry& entry : entries) {
				ToolSlot slot;
				slot.tool = entry.make();
				slot.id = entry.id;
				slot.mode = entry.mode;
				slot.group = entry.group;
				slot.hotKey = entry.hotKey;
				out.push_back(std::move(slot));
			}
		}
	} // namespace gui
} // namespace spades
