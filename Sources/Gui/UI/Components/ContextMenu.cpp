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

#include "ContextMenu.h"

#include <algorithm>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

namespace spades {
	namespace gui {
		namespace {
			const float kItemHeight = 22.0F;
			const float kSeparatorHeight = 7.0F;
			const float kPadX = 10.0F;
			const float kPadY = 4.0F;
			const float kHotKeyGap = 24.0F;
			const float kMinWidth = 140.0F;
			const float kScreenMargin = 4.0F;

			const Vector4 kPanelColor = MakeVector4(0.07F, 0.07F, 0.09F, 0.97F);
			const Vector4 kBorderColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.12F);
			const Vector4 kHighlightColor = MakeVector4(1.0F, 0.66F, 0.25F, 0.3F);
			const Vector4 kRuleColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.1F);
			const Vector4 kTextColor = MakeVector4(0.9F, 0.9F, 0.94F, 1.0F);
			const Vector4 kHotKeyColor = MakeVector4(0.6F, 0.6F, 0.65F, 1.0F);
			const Vector4 kDisabledColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);
		} // namespace

		ContextMenu::ContextMenu(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void ContextMenu::Open(const Vector2& at, std::vector<Item> newItems) {
			items = std::move(newItems);
			anchor = at;
			open = !items.empty();
			laidOut = false;
			itemRects.clear();
			highlighted = -1;
			lastCursor = at;
		}

		void ContextMenu::Close() {
			open = false;
			laidOut = false;
			items.clear();
			itemRects.clear();
			highlighted = -1;
		}

		int ContextMenu::ItemAt(const Vector2& p) const {
			for (std::size_t i = 0; i < itemRects.size(); i++) {
				if (itemRects[i].Contains(p))
					return int(i);
			}
			return -1;
		}

		void ContextMenu::Choose(int index) {
			if (index < 0 || index >= int(items.size()) || !items[std::size_t(index)].enabled)
				return;
			const std::string id = items[std::size_t(index)].id;
			Close(); // before the handler, which may open another
			sounds.Activate();
			if (OnChosen)
				OnChosen(id);
		}

		void ContextMenu::Step(int direction) {
			const int count = int(items.size());
			// With nothing highlighted, the first step lands on an end.
			int at = highlighted >= 0 ? highlighted : (direction > 0 ? -1 : count);
			for (int n = 0; n < count; n++) {
				at = (at + direction + count) % count;
				if (items[std::size_t(at)].enabled) {
					highlighted = at;
					return;
				}
			}
		}

		bool ContextMenu::MouseDown(const Vector2& p) {
			if (!open)
				return false;
			const int index = laidOut ? ItemAt(p) : -1;
			if (index >= 0) {
				// A press on a greyed item neither chooses it nor closes the
				// menu: it was aimed at the menu, just not at anything it can do.
				Choose(index);
				return true;
			}
			if (!(laidOut && panel.Contains(p)))
				Close();
			return true;
		}

		bool ContextMenu::KeyEvent(const std::string& key) {
			if (!open)
				return false;
			if (key == "Escape")
				Close();
			else if (key == "Up")
				Step(-1);
			else if (key == "Down")
				Step(1);
			else if (key == "Enter")
				Choose(highlighted);
			return true; // nothing else acts while the menu is up
		}

		void ContextMenu::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                       const Vector2& cursorPos, float screenWidth, float screenHeight) {
			if (!open)
				return;
			client::IFont& font = fontManager.GetSmallGuiFont();

			float labelWidth = 0.0F, hotKeyWidth = 0.0F, height = kPadY * 2.0F;
			for (const Item& item : items) {
				labelWidth = std::max(labelWidth, font.Measure(item.label).x);
				if (!item.hotKey.empty())
					hotKeyWidth = std::max(hotKeyWidth, font.Measure(item.hotKey).x);
				height += kItemHeight + (item.separatorBefore ? kSeparatorHeight : 0.0F);
			}
			const float width = std::max(kMinWidth, kPadX * 2.0F + labelWidth +
			                                          (hotKeyWidth > 0.0F ? kHotKeyGap + hotKeyWidth
			                                                              : 0.0F));
			// Kept on screen: pushed back from the right and bottom edges.
			const float x = std::max(kScreenMargin,
			                         std::min(anchor.x, screenWidth - width - kScreenMargin));
			const float y = std::max(kScreenMargin,
			                         std::min(anchor.y, screenHeight - height - kScreenMargin));
			panel = AABB2(x, y, width, height);
			itemRects.clear();
			float at = y + kPadY;
			for (const Item& item : items) {
				if (item.separatorBefore)
					at += kSeparatorHeight;
				itemRects.push_back(AABB2(x, at, width, kItemHeight));
				at += kItemHeight;
			}
			laidOut = true;

			// The pointer takes the highlight when it moves; the keyboard keeps
			// it otherwise.
			if (cursorPos.x != lastCursor.x || cursorPos.y != lastCursor.y) {
				lastCursor = cursorPos;
				const int under = ItemAt(cursorPos);
				if (under >= 0 && items[std::size_t(under)].enabled) {
					if (under != highlighted)
						sounds.Hover();
					highlighted = under;
				} else if (under < 0) {
					highlighted = -1;
				}
			}

			OverlayColorNP(renderer, kPanelColor);
			OverlayFillRect(renderer, x, y, width, height);
			OverlayStrokeRect(renderer, x, y, width, height, 1.0F, kBorderColor);
			for (std::size_t i = 0; i < items.size(); i++) {
				const Item& item = items[i];
				const AABB2& r = itemRects[i];
				if (item.separatorBefore) {
					OverlayColorNP(renderer, kRuleColor);
					OverlayFillRect(renderer, x + kPadX * 0.5F, r.GetMinY() - kSeparatorHeight * 0.5F,
					                width - kPadX, 1.0F);
				}
				if (int(i) == highlighted && item.enabled) {
					OverlayColorNP(renderer, kHighlightColor);
					OverlayFillRect(renderer, r.GetMinX() + 2.0F, r.GetMinY(), r.GetWidth() - 4.0F,
					                r.GetHeight());
				}
				const Vector2 size = font.Measure(item.label);
				const float textY = r.GetMinY() + (kItemHeight - size.y) * 0.5F;
				font.Draw(item.label, MakeVector2(x + kPadX, textY), 1.0F,
				          item.enabled ? kTextColor : kDisabledColor);
				if (!item.hotKey.empty()) {
					const Vector2 keySize = font.Measure(item.hotKey);
					font.Draw(item.hotKey, MakeVector2(x + width - kPadX - keySize.x, textY), 1.0F,
					          item.enabled ? kHotKeyColor : kDisabledColor);
				}
			}
		}
	} // namespace gui
} // namespace spades
