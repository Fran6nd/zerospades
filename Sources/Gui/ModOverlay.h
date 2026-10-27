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

namespace spades {
	namespace client {
		class IAudioDevice;
		class IRenderer;
	}
	namespace gui {
		/** What applying the enabled mods took. */
		enum class ModApplyResult {
			Applied,      // in place, no restart
			NeedsRestart, // a changed file can't be reloaded yet; the enabled set waits for it
			Refused,      // the new set's scripts don't build; the previous set is back
		};

		/**
		 * The enabled mods' paks, mounted over the base paks: the last enabled mod is
		 * on top and wins conflicts. A --try-mod run mounts none of them.
		 */
		class ModOverlay {
		public:
			/** Mounts the enabled set. Called once, at startup. */
			static void Mount();

			/**
			 * Mounts the enabled set in place of the mounted one and reloads, in
			 * place, what that changed. Runs on the main thread, between frames.
			 *
			 * Unless that returns `Applied`, the previous set stays mounted. On
			 * `Refused` it is also enabled again, and `error` says why.
			 */
			static ModApplyResult Apply(client::IRenderer&, client::IAudioDevice&,
			                            std::string& error);
		};
	} // namespace gui
} // namespace spades
