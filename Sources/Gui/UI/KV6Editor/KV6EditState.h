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

#include <cstdint>
#include <set>

#include <Core/Math.h>

namespace spades {
	namespace gui {
		/**
		 * Mirror modelling: which axes reflect edits, and the plane each reflects
		 * across. Not saved with the model, but it decides where edits land.
		 */
		struct MirrorSetup {
			bool enabled[3] = {false, false, false};
			Vector3 plane = MakeVector3(0.0F, 0.0F, 0.0F); // voxel coordinates

			bool operator==(const MirrorSetup& o) const {
				return enabled[0] == o.enabled[0] && enabled[1] == o.enabled[1] &&
				       enabled[2] == o.enabled[2] && plane == o.plane;
			}
			bool operator!=(const MirrorSetup& o) const { return !(*this == o); }
		};

		/**
		 * Everything besides the voxels that an undo step restores: what the
		 * user moved, turned or selected along with the model. Each step keeps
		 * this state from before and after it, so every such change undoes and
		 * redoes through the one history, whichever tool made it.
		 */
		struct EditState {
			std::set<int64_t> selection; // packed voxel keys
			MirrorSetup mirror;

			bool operator==(const EditState& o) const {
				return selection == o.selection && mirror == o.mirror;
			}
			bool operator!=(const EditState& o) const { return !(*this == o); }
		};
	} // namespace gui
} // namespace spades
