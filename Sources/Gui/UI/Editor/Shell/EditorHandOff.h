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

namespace spades {
	namespace gui {
		/**
		 * An editor view that may hand over to the editor of another kind of
		 * document as it closes: opening a scene in the model editor closes it
		 * and opens the scene editor on that file, as it would have been opened
		 * from the main screen. The host asks once the view wants to be closed.
		 */
		class IEditorHandOff {
		public:
			virtual ~IEditorHandOff() {}
			/** The document to open in its own editor next, or empty for none. */
			virtual std::string DocumentToOpenNext() const = 0;
		};
	} // namespace gui
} // namespace spades
