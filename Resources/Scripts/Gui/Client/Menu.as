/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "ClientUI.as"
#include "../MessageBox.as"

namespace spades {

	// Confirmation shown when leaving the editor with unsaved map changes.
	// Result indices: 0 = Save, 1 = Don't Save, 2 = Cancel.
	class EditorExitScreen : MessageBoxScreen {
		EditorExitScreen(spades::ui::UIElement@ owner, string text, string[]@ buttons, float height) {
			super(owner, text, buttons, height);
		}

		void HotKey(string key) {
			if (IsEnabled and key == "Escape") {
				EndDialog(2); // Cancel
			} else {
				UIElement::HotKey(key);
			}
		}
	}

	class ClientMenu : spades::ui::UIElement {
		private ClientUI@ ui;
		private ClientUIHelper@ helper;

		ClientMenu(ClientUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@this.helper = ui.helper;

			bool editing = helper.IsLocalMapMode();

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float winW = 180.0F, winH = 32.0F * 4.0F - 2.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) * 0.5F;

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.5F);
				label.Bounds = AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F);
				AddChild(label);
			}

			{
				spades::ui::Button button(Manager);
				button.Caption = editing
					? _Tr("Client", "Back to Editor")
					: _Tr("Client", "Back to Game");
				button.Bounds = AABB2(winX, winY, winW, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnBackToGame);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				if (editing) {
					button.Caption = _Tr("Client", "Save Map");
					@button.Activated = spades::ui::EventHandler(this.OnSaveMap);
				} else {
					button.Caption = _Tr("Client", "Chat Log");
					@button.Activated = spades::ui::EventHandler(this.OnChatLog);
				}
				button.Bounds = AABB2(winX, winY + 32.0F, winW, 30.0F);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Setup");
				button.Bounds = AABB2(winX, winY + 64.0F, winW, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnSetup);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				if (editing)
					button.Caption = _Tr("Client", "Exit Editor");
				else if (helper.IsDemoMode())
					button.Caption = _Tr("Client", "Exit Demo");
				else
					button.Caption = _Tr("Client", "Disconnect");
				button.Bounds = AABB2(winX, winY + 96.0F, winW, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnDisconnect);
				AddChild(button);
			}
		}

		private void OnBackToGame(spades::ui::UIElement@ sender) { @ui.ActiveUI = null; }
		private void OnSetup(spades::ui::UIElement@ sender) {
			PreferenceViewOptions opt;
			opt.GameActive = true;

			PreferenceView al(this, opt, ui.fontManager);
			al.Run();
		}
		private void OnChatLog(spades::ui::UIElement@ sender) {
			@ui.ActiveUI = @ui.chatLogWindow;
			ui.chatLogWindow.ScrollToEnd();
		}
		private void OnSaveMap(spades::ui::UIElement@ sender) {
			helper.SaveMap();
			// Return to the editor so the save confirmation alert is visible.
			@ui.ActiveUI = null;
		}
		private void OnDisconnect(spades::ui::UIElement@ sender) {
			if (helper.IsLocalMapMode() and helper.HasUnsavedMap()) {
				string[] buttons = {
					_Tr("Client", "Save"),
					_Tr("Client", "Don't Save"),
					_Tr("Client", "Cancel")
				};
				EditorExitScreen dialog(this,
					_Tr("Client", "You have unsaved changes to this map. Save before exiting?"),
					buttons, 100.0F);
				@dialog.Closed = spades::ui::EventHandler(this.OnExitConfirmed);
				dialog.Run();
				return;
			}
			ui.shouldExit = true;
		}
		private void OnExitConfirmed(spades::ui::UIElement@ sender) {
			EditorExitScreen@ dialog = cast<EditorExitScreen>(sender);
			if (dialog is null)
				return;
			if (dialog.ResultIndex == 0) { // Save
				if (helper.SaveMap())
					ui.shouldExit = true;
				else
					@ui.ActiveUI = null; // surface the error alert; the map keeps its changes
			} else if (dialog.ResultIndex == 1) { // Don't Save
				ui.shouldExit = true;
			}
			// Cancel (2) or dismissed: stay in the menu.
		}

		void HotKey(string key) {
			if (IsEnabled and key == "Escape") {
				@ui.ActiveUI = null;
			} else {
				UIElement::HotKey(key);
			}
		}
	}
}
