/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <unordered_set>

namespace spades {
	/**
	 * The resource files whose content may have changed, for example because the
	 * mounted mods changed. Paths compare the way zip entries do: case-insensitive,
	 * with '/' as the separator.
	 */
	class ResourceChanges {
		std::unordered_set<std::string> paths;

	public:
		/** When the holders of a resource can pick up its new content. */
		enum class ReloadTime {
			Live,    // in place, between two frames
			Restart, // only by relaunching the game
		};

		void Add(const std::string& path);
		bool Contains(const std::string& path) const;
		/** Whether a changed path is in `dir` or below it. */
		bool ContainsUnder(const std::string& dir) const;
		bool IsEmpty() const { return paths.empty(); }

		/** The latest time over every changed path. */
		ReloadTime GetReloadTime() const;

		/**
		 * When a change to `path` can be picked up. This is the one place that says
		 * which resource kinds can be reloaded; see MOD-HOT-RELOAD.md.
		 */
		static ReloadTime GetReloadTime(const std::string& path);
	};
} // namespace spades
