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

#include "KV6TransformTool.h"
#include "KV6EditorContext.h"

#include <cstdio>

namespace spades {
	namespace gui {
		namespace {
			const char* const kPlaceOption = "transform.place";
			const char* const kCancelOption = "transform.cancel";
			const char* const kReadoutOption = "transform.readout";
		} // namespace

		TransformTool::TransformTool() {
			subs.push_back(std::unique_ptr<EditorTool>(new TransformSubTool()));
			options.AddAction(kPlaceOption, "Place");
			options.AddAction(kCancelOption, "Cancel");
			options.AddLabel(kReadoutOption);
		}

		void TransformTool::UpdateOptions(IEditorContext& ed) {
			// Place and Cancel act on pending voxels; a selection that has not
			// moved yet is not pending, so there is nothing for them to do.
			const bool pending = ed.HasPlacement();
			options.SetEnabled(kPlaceOption, pending);
			options.SetEnabled(kCancelOption, pending);

			IntVector3 pivot;
			if (ed.TransformPivot(pivot)) {
				char buf[80];
				std::snprintf(buf, sizeof(buf), "Pivot  %d, %d, %d", pivot.x, pivot.y, pivot.z);
				options.SetLabel(kReadoutOption, buf);
			} else {
				options.SetLabel(kReadoutOption, "Nothing to move");
			}
		}

		void TransformTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kPlaceOption)
				ed.ApplyPlacement();
			else if (id == kCancelOption)
				ed.CancelPlacement();
		}
	} // namespace gui
} // namespace spades
