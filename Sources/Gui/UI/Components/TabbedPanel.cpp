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

#include "TabbedPanel.h"

#include <algorithm>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Framework/TextUtils.h>

namespace spades {
	namespace gui {
		namespace {
			const Vector4 kPanelColor = MakeVector4(0.08F, 0.08F, 0.10F, 0.96F);
			const Vector4 kTabRowColor = MakeVector4(0.05F, 0.05F, 0.065F, 1.0F);
			const Vector4 kTabHoverColor = MakeVector4(0.11F, 0.11F, 0.14F, 1.0F);
			const Vector4 kActiveAccent = MakeVector4(1.0F, 0.66F, 0.25F, 1.0F);
			const Vector4 kActiveText = MakeVector4(0.95F, 0.95F, 0.97F, 1.0F);
			const Vector4 kIdleText = MakeVector4(0.62F, 0.62F, 0.66F, 1.0F);
			const Vector4 kEdgeColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.6F);
			const float kTabTextInset = 8.0F;
			const float kAccentHeight = 2.0F;
		} // namespace

		TabbedPanel::TabbedPanel(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void TabbedPanel::SetTabs(std::vector<std::string> labels) {
			tabs = std::move(labels);
			previousHover.assign(tabs.size(), false);
			activeTab = std::max(0, std::min(activeTab, int(tabs.size()) - 1));
		}

		void TabbedPanel::SetActiveTab(int index) {
			if (index >= 0 && index < int(tabs.size()))
				activeTab = index;
		}

		void TabbedPanel::SetBounds(const AABB2& b) { bounds = b; }

		AABB2 TabbedPanel::Body() const {
			return AABB2(bounds.GetMinX(), bounds.GetMinY() + kTabRowHeight, bounds.GetWidth(),
			             std::max(0.0F, bounds.GetHeight() - kTabRowHeight));
		}

		bool TabbedPanel::Contains(const Vector2& p) const {
			return OverlayInRect(p, bounds.GetMinX(), bounds.GetMinY(), bounds.GetWidth(),
			                     bounds.GetHeight());
		}

		AABB2 TabbedPanel::TabRect(int index) const {
			const float width = tabs.empty() ? 0.0F : bounds.GetWidth() / float(tabs.size());
			return AABB2(bounds.GetMinX() + width * float(index), bounds.GetMinY(), width,
			             kTabRowHeight);
		}

		bool TabbedPanel::Click(const Vector2& p) {
			if (!OverlayInRect(p, bounds.GetMinX(), bounds.GetMinY(), bounds.GetWidth(),
			                   kTabRowHeight))
				return false;
			for (int i = 0; i < int(tabs.size()); i++) {
				const AABB2 r = TabRect(i);
				if (!OverlayInRect(p, r.GetMinX(), r.GetMinY(), r.GetWidth(), r.GetHeight()))
					continue;
				if (i != activeTab) {
					activeTab = i;
					sounds.Activate();
					if (OnTabChanged)
						OnTabChanged(i);
				}
				break;
			}
			return true;
		}

		void TabbedPanel::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                       const Vector2& cursorPos, bool menuActive) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const float x = bounds.GetMinX(), y = bounds.GetMinY();
			const float w = bounds.GetWidth(), h = bounds.GetHeight();

			OverlayColorNP(renderer, kPanelColor);
			OverlayFillRect(renderer, x, y, w, h);
			OverlayColorNP(renderer, kTabRowColor);
			OverlayFillRect(renderer, x, y, w, kTabRowHeight);
			// A hairline down the edge facing the viewport parts the two.
			OverlayColorNP(renderer, kEdgeColor);
			OverlayFillRect(renderer, x + w - 1.0F, y, 1.0F, h);

			for (int i = 0; i < int(tabs.size()); i++) {
				const AABB2 r = TabRect(i);
				const bool active = i == activeTab;
				const bool hover =
				  !menuActive &&
				  OverlayInRect(cursorPos, r.GetMinX(), r.GetMinY(), r.GetWidth(), r.GetHeight());
				previousHover[std::size_t(i)] =
				  sounds.HoverEdge(hover && !active, previousHover[std::size_t(i)]);
				if (active) {
					// The active tab opens into the body: same colour, and an
					// accent along its top.
					OverlayColorNP(renderer, kPanelColor);
					OverlayFillRect(renderer, r.GetMinX(), r.GetMinY(), r.GetWidth(), r.GetHeight());
					OverlayColorNP(renderer, kActiveAccent);
					OverlayFillRect(renderer, r.GetMinX(), r.GetMinY(), r.GetWidth(), kAccentHeight);
				} else if (hover) {
					OverlayColorNP(renderer, kTabHoverColor);
					OverlayFillRect(renderer, r.GetMinX(), r.GetMinY(), r.GetWidth(), r.GetHeight());
				}
				const std::string label = ui::ElideText(font, tabs[std::size_t(i)],
				                                        r.GetWidth() - kTabTextInset * 2.0F);
				const Vector2 size = font.Measure(label);
				font.Draw(label,
				          MakeVector2(r.GetMinX() + (r.GetWidth() - size.x) * 0.5F,
				                      r.GetMinY() + (r.GetHeight() - size.y) * 0.5F),
				          1.0F, active ? kActiveText : kIdleText);
			}
		}
	} // namespace gui
} // namespace spades
