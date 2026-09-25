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
		 * A docked panel with a row of tabs along its top: the frame that
		 * editor side panels share.
		 *
		 * It draws the panel's background and its tabs and says which tab is
		 * showing; what goes in the body is the owner's, drawn into `Body()`
		 * after the frame. Tabs are named by position, and the owner decides
		 * what each one holds.
		 */
		class TabbedPanel {
		public:
			explicit TabbedPanel(client::IAudioDevice* audioDevice = nullptr);

			void SetTabs(std::vector<std::string> labels);
			int ActiveTab() const { return activeTab; }
			/** Shows tab `index`; out of range, it is ignored. Reports nothing. */
			void SetActiveTab(int index);

			/** Where the whole panel sits on screen, tabs included. */
			void SetBounds(const AABB2& bounds);
			const AABB2& Bounds() const { return bounds; }
			/** What is left under the tabs, for the owner to fill. */
			AABB2 Body() const;
			bool Contains(const Vector2& p) const;

			/**
			 * A press at `p`: on a tab, it shows that tab. True when `p` was on
			 * the tab row at all, so the owner knows the press is taken.
			 */
			bool Click(const Vector2& p);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			          const Vector2& cursorPos, bool menuActive);

			/** Called with the tab a click switched to. */
			std::function<void(int index)> OnTabChanged;

			/** Height of the tab row. */
			static constexpr float kTabRowHeight = 26.0F;

		private:
			ui::FeedbackSounds sounds;
			std::vector<std::string> tabs;
			std::vector<bool> previousHover;
			int activeTab = 0;
			AABB2 bounds = AABB2(0.0F, 0.0F, 0.0F, 0.0F);

			// Tab `index`'s rectangle: the row is shared out evenly.
			AABB2 TabRect(int index) const;
		};
	} // namespace gui
} // namespace spades
