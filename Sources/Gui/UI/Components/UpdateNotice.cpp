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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "UpdateNotice.h"

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/ShellApi.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UpdateChecker.h>

namespace spades {
	namespace gui {
		namespace {
			// Warm accent so the notice reads apart from the grey menu chrome.
			const Vector3 kAccent = MakeVector3(1.0F, 0.72F, 0.2F);

			// Quiet confirmation colour; it should not compete with the menu.
			const Vector4 kUpToDateColor = MakeVector4(0.45F, 0.9F, 0.45F, 0.85F);
			constexpr float kCheckMarkSize = 10.0F;
			constexpr float kCheckMarkThickness = 2.0F;
			constexpr float kCheckMarkGap = 6.0F;

			constexpr float kPromptHeight = 170.0F;

			void OpenReleasePage(const std::string& url) {
				if (!OpenURLInBrowser(url))
					SPLog("[!] Could not open the release page: %s", url.c_str());
			}
		} // namespace

		// -- UpdateNotice --

		UpdateNotice::UpdateNotice(ui::UIManager* manager, bool showUpToDate)
		    : ButtonBase(manager), showUpToDate(showUpToDate) {
			isMouseInteractive = false;
			Refresh();
		}

		void UpdateNotice::Refresh() {
			// A found update is final for this process; resolve it only once.
			if (isMouseInteractive)
				return;

			UpdateChecker::Result result = UpdateChecker::Get().GetResult();
			upToDate = result.state == UpdateChecker::State::UpToDate;
			if (result.state != UpdateChecker::State::UpdateAvailable)
				return;

			isMouseInteractive = true;
			releaseUrl = result.releaseUrl;
			caption = _Tr("MainScreen",
			              "ZeroSpades {0} is available (you have {1}). Click here to download it.",
			              result.latestVersion.ToString(),
			              ReleaseVersion::Current().ToString());
		}

		void UpdateNotice::OnActivated() {
			if (!releaseUrl.empty())
				OpenReleasePage(releaseUrl);
			ButtonBase::OnActivated();
		}

		void UpdateNotice::RenderUpToDate() {
			client::IFont* font = GetFont();
			if (!font)
				return;

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();

			std::string text =
			  _Tr("MainScreen", "Up to date ({0})", ReleaseVersion::Current().ToString());
			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos =
			  MakeVector2(pos.x + size.x - txtSize.x, pos.y + (size.y - txtSize.y) * 0.5F);
			font->DrawShadow(text, txtPos, 1.0F, kUpToDateColor,
			                 MakeVector4(0.0F, 0.0F, 0.0F, 0.4F));

			// A check mark drawn as two strokes, so it does not depend on the font
			// carrying a check glyph.
			const float s = kCheckMarkSize;
			const float x = txtPos.x - kCheckMarkGap - s;
			const float y = pos.y + (size.y - s) * 0.5F;
			const Vector2 start = MakeVector2(x, y + s * 0.55F);
			const Vector2 corner = MakeVector2(x + s * 0.38F, y + s * 0.9F);
			const Vector2 end = MakeVector2(x + s, y + s * 0.1F);
			ui::SetColorNP(r, kUpToDateColor);
			r.DrawLine(start, corner, kCheckMarkThickness);
			r.DrawLine(corner, end, kCheckMarkThickness);
		}

		void UpdateNotice::Render() {
			Refresh();
			if (!isMouseInteractive) {
				if (showUpToDate && upToDate)
					RenderUpToDate();
				return;
			}

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			float fill = 0.25F;
			if (pressed && hover)
				fill = 0.55F;
			else if (hover)
				fill = 0.4F;

			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, fill));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, 0.7F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			client::IFont* font = GetFont();
			if (!font)
				return;

			Vector2 txtSize = font->Measure(caption);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;
			font->DrawShadow(caption, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F),
			                 MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
		}

		// -- UpdatePromptScreen --

		void UpdatePromptScreen::ShowIfPending(ui::UIElement* owner) {
			// Wait for the owner to be free, so the prompt never stacks on top of
			// another dialog (e.g. the first-launch profile prompt).
			if (!owner->IsEnabled())
				return;
			if (!UpdateChecker::Get().ClaimStartupPrompt())
				return;

			UpdateChecker::Result result = UpdateChecker::Get().GetResult();
			std::string text =
			  _Tr("MainScreen",
			      "A new version of ZeroSpades is available!\n\n"
			      "Installed version: {0}\nLatest version: {1}\n\n"
			      "Download it to get the latest features and fixes.",
			      ReleaseVersion::Current().ToString(), result.latestVersion.ToString());

			Handle<UpdatePromptScreen> prompt =
			  Handle<UpdatePromptScreen>::New(owner, text, result.releaseUrl);
			prompt->Run();
		}

		UpdatePromptScreen::UpdatePromptScreen(ui::UIElement* owner, const std::string& text,
		                                       const std::string& releaseUrl)
		    : MessageBoxScreen(owner, text,
		                       {_Tr("MainScreen", "Download"), _Tr("MainScreen", "Later")},
		                       kPromptHeight, true) {
			closed = [releaseUrl](ui::UIElement& sender) {
				if (static_cast<UpdatePromptScreen&>(sender).resultIndex == 0)
					OpenReleasePage(releaseUrl);
			};
		}

		void UpdatePromptScreen::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				EndDialog(0);
			} else if (IsEnabled() && key == "Escape") {
				EndDialog(1);
			} else {
				MessageBoxScreen::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
