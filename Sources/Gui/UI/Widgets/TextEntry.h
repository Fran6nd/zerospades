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

#include <cstddef>
#include <string>

namespace spades {
	namespace gui {
		/**
		 * A line of text being typed, the free-text sibling of NumberEntry.
		 *
		 * It holds the text as it stands and where the caret is, and nothing
		 * else: whoever shows one places it, paints it and tells it what the
		 * user did. That is what lets an immediate-mode control, which has no
		 * retained widget to take keyboard focus, still be typed into.
		 *
		 * The text is UTF-8 and the caret only ever sits between whole code
		 * points, so moving or deleting never splits a character.
		 */
		class TextEntry {
		public:
			/**
			 * Starts editing `text`. The caret sits at the end and the first
			 * character typed replaces the whole of it, the way focusing a field
			 * that selected its text would. `maxBytes` bounds what can be typed.
			 */
			void Begin(const std::string& text, std::size_t maxBytes);
			void End();
			bool IsEditing() const { return editing; }
			const std::string& Text() const { return text; }
			/** Byte index of the caret within `Text()`. */
			int Caret() const { return caret; }
			/** Whether the whole text is still marked to be replaced by typing. */
			bool IsFresh() const { return fresh; }

			/** Takes `s`, dropping control characters and whatever would not fit. */
			void Insert(const std::string& s);
			void Backspace();
			void Delete();
			/** Moves the caret `by` characters, stopping at either end. */
			void MoveCaret(int by);
			void CaretToStart();
			void CaretToEnd();

		private:
			bool editing = false;
			// True until the text is first changed or the caret moved; typing
			// then replaces the text rather than adding to it.
			bool fresh = false;
			std::string text;
			int caret = 0;
			std::size_t maxBytes = 0;
		};
	} // namespace gui
} // namespace spades
