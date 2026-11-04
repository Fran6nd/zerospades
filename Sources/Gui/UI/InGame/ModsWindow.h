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

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		class ModsScreenHelper;
		namespace ui {
			class Label;
			class ListView;
		} // namespace ui
	} // namespace gui
	namespace client {
		class ClientUI;

		/**
		 * The Mods page of the in-game escape menu: the main menu's mod list, and
		 * Apply, which applies the change to the game in progress when it can.
		 * Shown over its owner, which it disables meanwhile.
		 */
		class ModsWindow : public gui::ui::UIElement {
			ClientUI* ui;                 // weak
			gui::ui::UIElement* owner;    // weak
			Handle<gui::ModsScreenHelper> helper;
			gui::ui::ListView* list;      // weak
			gui::ui::Label* statusLabel;  // weak
			bool downloading = false;

			void LoadModList();
			void OnModToggle(const std::string& modName);
			void OnApply(gui::ui::UIElement& sender);
			void OnDisableAll(gui::ui::UIElement& sender);
			void OnDownload(gui::ui::UIElement& sender);
			void OnDownloadConfirmed(gui::ui::UIElement& sender);
			void CheckDownload();

		public:
			gui::ui::EventHandler closed;

			ModsWindow(gui::ui::UIElement* owner, ClientUI* ui);
			void Run();
			void Close();
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
