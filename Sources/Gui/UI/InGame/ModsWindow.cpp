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

#include <algorithm>
#include <vector>

#include "ModsWindow.h"
#include <Client/ClientUI.h>
#include <Core/Strings.h>
#include <Gui/ModOverlay.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/MainScreen/ModListView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/MessageBox.h>

namespace spades {
	namespace client {
		using gui::ConfirmScreen;
		using gui::ModApplyResult;
		using gui::ModListHeader;
		using gui::ModListModel;
		using gui::ModOverlay;
		using gui::ModsScreenHelper;
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::ListView;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			// The main menu's Mods tab columns.
			const float kColumns[] = {60.0F, 90.0F, 56.0F, 200.0F, 150.0F, 100.0F};
		} // namespace

		ModsWindow::ModsWindow(UIElement* owner, ClientUI* ui)
		    : UIElement(&owner->GetManager()), ui(ui), owner(owner) {
			helper = Handle<ModsScreenHelper>::New();

			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float winW = std::min(sw - 16.0F, 680.0F);
			float winH = std::min(sh - 16.0F, 480.0F);
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F));
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("Client", "Mods");
				label->SetBounds(AABB2(winX, winY, winW, 30.0F));
				AddChild(label.GetPointerOrNull());
			}

			float headerY = winY + 40.0F;
			{
				const std::string titles[] = {_Tr("MainScreen", "Status"),
				                              _Tr("MainScreen", "Apply Order"),
				                              _Tr("MainScreen", "Type"), _Tr("MainScreen", "Mod"),
				                              _Tr("MainScreen", "Author"),
				                              _Tr("MainScreen", "Size")};
				float x = winX;
				for (int i = 0; i < 6; i++) {
					Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
					header->SetBounds(AABB2(x, headerY, kColumns[i], 24.0F));
					header->text = titles[i];
					AddChild(header.GetPointerOrNull());
					x += kColumns[i];
				}
			}

			float footerY = winY + winH - 30.0F;
			{
				Handle<ListView> view = Handle<ListView>::New(manager);
				list = view.GetPointerOrNull();
				list->SetBounds(
				  AABB2(winX, headerY + 26.0F, winW, footerY - 40.0F - (headerY + 26.0F)));
				AddChild(list);
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				statusLabel = label.GetPointerOrNull();
				statusLabel->SetBounds(AABB2(winX, footerY - 34.0F, winW, 24.0F));
				AddChild(statusLabel);
			}
			{
				// For players who never downloaded them from the main menu.
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Download official mods...");
				button->SetBounds(AABB2(winX, footerY, 240.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnDownload(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Disable all");
				button->SetBounds(AABB2(winX + winW - 410.0F, footerY, 130.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnDisableAll(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Apply changes");
				button->SetBounds(AABB2(winX + winW - 270.0F, footerY, 130.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnApply(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->SetBounds(AABB2(winX + winW - 130.0F, footerY, 130.0F, 30.0F));
				button->activated = [this](UIElement&) { Close(); };
				AddChild(button.GetPointerOrNull());
			}

			LoadModList();
		}

		void ModsWindow::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void ModsWindow::Close() {
			// The parent holds the only strong reference: keep this alive until
			// `closed` has run.
			Handle<ModsWindow> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void ModsWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Close();
			else
				UIElement::HotKey(key);
		}

		// The same rows as the main menu: every mod on disk, then enabled mods that
		// are gone, so they can be disabled.
		void ModsWindow::LoadModList() {
			std::vector<std::string> names = helper->GetModNames();
			std::vector<std::string> enabled = helper->GetEnabledMods();

			std::vector<std::string> rows;
			std::vector<int> orders;
			std::vector<bool> exists;
			for (const std::string& name : names) {
				auto it = std::find(enabled.begin(), enabled.end(), name);
				rows.push_back(name);
				orders.push_back(it == enabled.end() ? 0
				                                     : static_cast<int>(it - enabled.begin()) + 1);
				exists.push_back(true);
			}
			for (std::size_t i = 0; i < enabled.size(); i++) {
				if (std::find(names.begin(), names.end(), enabled[i]) == names.end()) {
					rows.push_back(enabled[i]);
					orders.push_back(static_cast<int>(i) + 1);
					exists.push_back(false);
				}
			}

			Handle<ModListModel> model = Handle<ModListModel>::New(
			  &GetManager(), helper.GetPointerOrNull(), std::move(rows), std::move(orders),
			  std::move(exists), kColumns[0], kColumns[1], kColumns[2], kColumns[3], kColumns[4],
			  kColumns[5]);
			list->SetModel(model.GetPointerOrNull());
			model->itemActivated = [this](const std::string& name) { OnModToggle(name); };
		}

		void ModsWindow::Render() {
			CheckDownload();
			UIElement::Render();
		}

		void ModsWindow::OnDownload(UIElement&) {
			if (downloading)
				return;
			std::string body =
			  _Tr("MainScreen", "Download the official ZeroSpades mod pack?") + "\n\n";
			body +=
			  _Tr("MainScreen", "These are mods reviewed and hosted by the ZeroSpades team.") +
			  "\n\n";
			body += _Tr("MainScreen", "Source: ") + helper->GetIndexUrl() + "\n";
			body += _Tr("MainScreen", "Files will be saved into your Mods/ folder, overwriting "
			                          "any existing copies.");
			Handle<ConfirmScreen> cs = Handle<ConfirmScreen>::New(
			  this, body, std::min(500.0F, GetManager().screenHeight - 100.0F), true);
			cs->closed = [this](UIElement& s) { OnDownloadConfirmed(s); };
			cs->Run();
		}

		void ModsWindow::OnDownloadConfirmed(UIElement& sender) {
			ConfirmScreen* cs = dynamic_cast<ConfirmScreen*>(&sender);
			if (cs == nullptr || !cs->GetResult() || downloading)
				return;
			downloading = true;
			helper->StartRefresh();
			statusLabel->text = _Tr("MainScreen", "Fetching mod list...");
		}

		void ModsWindow::CheckDownload() {
			if (!downloading)
				return;
			if (helper->PollRefreshState()) {
				downloading = false;
				LoadModList();
				std::string err = helper->GetRefreshMessage();
				statusLabel->text = err.empty()
				                      ? _Tr("Client", "Mods downloaded. Pick some, then press "
				                                      "'Apply changes'.")
				                      : _Tr("MainScreen", "Mod download failed:") + " " + err;
				return;
			}
			int total = helper->GetRefreshTotal();
			if (total <= 0) {
				statusLabel->text = _Tr("MainScreen", "Fetching mod list...");
				return;
			}
			std::string label = std::to_string(helper->GetRefreshDone()) + " / " +
			                    std::to_string(total);
			std::string item = helper->GetRefreshCurrentItem();
			if (!item.empty())
				label += " - " + item;
			statusLabel->text = label;
		}

		void ModsWindow::OnModToggle(const std::string& modName) {
			if (downloading)
				return;
			std::vector<std::string> enabled = helper->GetEnabledMods();
			if (std::find(enabled.begin(), enabled.end(), modName) != enabled.end())
				helper->DisableMod(modName);
			else
				helper->EnableMod(modName);
			statusLabel->text = _Tr("Client", "Press 'Apply changes' to apply your mods.");
			LoadModList();
		}

		void ModsWindow::OnDisableAll(UIElement&) {
			if (downloading)
				return;
			helper->ClearEnabledMods();
			statusLabel->text = _Tr("Client", "Press 'Apply changes' to apply your mods.");
			LoadModList();
		}

		void ModsWindow::OnApply(UIElement&) {
			if (downloading)
				return;
			std::string error;
			switch (ModOverlay::Apply(*ui->GetRenderer(), *ui->GetAudioDevice(), error)) {
				case ModApplyResult::Applied:
					statusLabel->text = _Tr("Client", "Mods applied.");
					break;
				case ModApplyResult::NeedsRestart:
					statusLabel->text =
					  _Tr("Client", "Saved. This change takes effect the next time the game starts.");
					break;
				case ModApplyResult::Refused:
					statusLabel->text =
					  _Tr("Client", "Not applied, their scripts don't build: {0}", error);
					break;
			}
			// A refused set is enabled again as it was.
			LoadModList();
		}
	} // namespace client
} // namespace spades
