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

#include "TextEntry.h"

namespace spades {
	namespace gui {
		namespace {
			// A byte that continues a UTF-8 sequence rather than starting one.
			bool IsContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

			// The byte where the character before `at` starts.
			int PreviousBoundary(const std::string& s, int at) {
				if (at <= 0)
					return 0;
				at--;
				while (at > 0 && IsContinuation(s[std::size_t(at)]))
					at--;
				return at;
			}

			// The byte just past the character starting at `at`.
			int NextBoundary(const std::string& s, int at) {
				const int size = int(s.size());
				if (at >= size)
					return size;
				at++;
				while (at < size && IsContinuation(s[std::size_t(at)]))
					at++;
				return at;
			}

			// `s` cut to at most `bytes`, never inside a character.
			std::string FitBytes(const std::string& s, std::size_t bytes) {
				if (s.size() <= bytes)
					return s;
				std::size_t end = bytes;
				while (end > 0 && IsContinuation(s[end]))
					end--;
				return s.substr(0, end);
			}
		} // namespace

		void TextEntry::Begin(const std::string& initial, std::size_t limit) {
			editing = true;
			fresh = true;
			maxBytes = limit;
			text = FitBytes(initial, limit);
			caret = int(text.size());
		}

		void TextEntry::End() {
			editing = false;
			fresh = false;
			text.clear();
			caret = 0;
		}

		void TextEntry::Insert(const std::string& s) {
			if (!editing)
				return;
			std::string accepted;
			accepted.reserve(s.size());
			for (char c : s) {
				// Control characters (a pasted newline, a tab) have no place in
				// a single line of text.
				if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
					accepted += c;
			}
			if (accepted.empty())
				return;
			if (fresh) {
				text.clear();
				caret = 0;
				fresh = false;
			}
			accepted = FitBytes(accepted, maxBytes > text.size() ? maxBytes - text.size() : 0);
			text.insert(std::size_t(caret), accepted);
			caret += int(accepted.size());
		}

		void TextEntry::Backspace() {
			if (!editing)
				return;
			if (fresh) {
				// Like a selected field, the whole text goes at once.
				text.clear();
				caret = 0;
				fresh = false;
				return;
			}
			const int from = PreviousBoundary(text, caret);
			text.erase(std::size_t(from), std::size_t(caret - from));
			caret = from;
		}

		void TextEntry::Delete() {
			if (!editing)
				return;
			if (fresh) {
				text.clear();
				caret = 0;
				fresh = false;
				return;
			}
			const int to = NextBoundary(text, caret);
			text.erase(std::size_t(caret), std::size_t(to - caret));
		}

		void TextEntry::MoveCaret(int by) {
			if (!editing)
				return;
			fresh = false;
			for (; by < 0; by++)
				caret = PreviousBoundary(text, caret);
			for (; by > 0; by--)
				caret = NextBoundary(text, caret);
		}

		void TextEntry::CaretToStart() {
			fresh = false;
			caret = 0;
		}

		void TextEntry::CaretToEnd() {
			fresh = false;
			caret = int(text.size());
		}
	} // namespace gui
} // namespace spades
