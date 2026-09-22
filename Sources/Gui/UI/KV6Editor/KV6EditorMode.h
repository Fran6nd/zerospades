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

namespace spades {
	namespace gui {
		/**
		 * The editor's modes, as Blender has them: Object mode picks and places
		 * whole objects of a .2kv6 scene, Edit mode changes the voxels of the
		 * active one (the whole file, for a .kv6), and Animation is to come.
		 * Each tool belongs to the mode it makes sense in, and the mode itself
		 * is part of the journaled edit state.
		 */
		enum class EditorMode { Object, Edit, Animation };
	} // namespace gui
} // namespace spades
