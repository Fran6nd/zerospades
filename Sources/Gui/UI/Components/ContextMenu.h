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

#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * A right-click menu: a short list of commands popping up at the
		 * cursor.
		 *
		 * While it is open it is modal to the pointer and the keyboard: a
		 * press on an item chooses it, a press anywhere else only closes it,
		 * and Up, Down, Enter and Escape work it from the keyboard. The host
		 * describes the items (a disabled one is shown greyed and cannot be
		 * chosen) and is told the id of the one chosen.
		 *
		 * Its size depends on the text, so it is laid out while drawing, where
		 * the font is at hand, and kept inside the screen there. A press
		 * before the first draw only closes it.
		 */
		class ContextMenu {
		public:
			struct Item {
				std::string id; // reported when chosen
				std::string label;
				std::string hotKey; // drawn on the right; empty for none
				bool enabled = true;
				bool separatorBefore = false; // a rule above it, parting groups
			};

			explicit ContextMenu(client::IAudioDevice* audioDevice = nullptr);

			/** Opens at `at` (its top-left corner, moved to stay on screen). */
			void Open(const Vector2& at, std::vector<Item> items);
			void Close();
			bool IsOpen() const { return open; }

			/**
			 * A press at `p` while open: chooses the item under it, or closes
			 * the menu. True while open, since the press is the menu's either
			 * way; false when it is closed.
			 */
			bool MouseDown(const Vector2& p);
			/** Takes a key down while open; false when closed. */
			bool KeyEvent(const std::string& key);

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			          const Vector2& cursorPos, float screenWidth, float screenHeight);

			std::function<void(const std::string& id)> OnChosen;

		private:
			ui::FeedbackSounds sounds;
			bool open = false;
			std::vector<Item> items;
			Vector2 anchor = MakeVector2(0.0F, 0.0F);
			// As last drawn: the panel, and where each item went. Empty until
			// the first draw after opening.
			bool laidOut = false;
			AABB2 panel = AABB2(0.0F, 0.0F, 0.0F, 0.0F);
			std::vector<AABB2> itemRects;
			int highlighted = -1; // by the keyboard or the pointer
			Vector2 lastCursor = MakeVector2(-1.0F, -1.0F);

			/** The item under `p`, or -1. */
			int ItemAt(const Vector2& p) const;
			void Choose(int index);
			/** Moves the highlight to the next enabled item `direction` away. */
			void Step(int direction);
		};
	} // namespace gui
} // namespace spades
