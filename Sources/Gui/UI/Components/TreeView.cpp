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

#include "TreeView.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Gui/UI/Framework/TextUtils.h>

namespace spades {
	namespace gui {
		namespace {
			const float kRowHeight = 20.0F;
			const float kPadTop = 4.0F;
			const float kPadX = 6.0F;
			const float kIndent = 14.0F;
			const float kFoldWidth = 12.0F;
			const float kLabelGap = 3.0F;
			const float kScrollBarWidth = 3.0F;
			// How far a press has to travel before it is a drag rather than a click.
			const float kDragThreshold = 4.0F;
			// Two clicks on one row this close together are a double click.
			const float kDoubleClickTime = 0.4F;
			// The top and bottom quarter of a row drop beside it; the middle
			// drops into it.
			const float kDropEdge = 0.25F;
			const float kRenameInset = 4.0F;

			const Vector4 kTextColor = MakeVector4(0.86F, 0.86F, 0.9F, 1.0F);
			const Vector4 kDimTextColor = MakeVector4(0.6F, 0.6F, 0.65F, 1.0F);
			const Vector4 kActiveTextColor = MakeVector4(1.0F, 0.85F, 0.6F, 1.0F);
			const Vector4 kSelectedBg = MakeVector4(0.92F, 0.38F, 0.05F, 0.22F);
			const Vector4 kActiveBg = MakeVector4(1.0F, 0.66F, 0.25F, 0.32F);
			const Vector4 kHoverBg = MakeVector4(1.0F, 1.0F, 1.0F, 0.06F);
			const Vector4 kFoldColor = MakeVector4(0.75F, 0.75F, 0.8F, 1.0F);
			const Vector4 kDropColor = MakeVector4(1.0F, 0.66F, 0.25F, 1.0F);
			const Vector4 kGhostBg = MakeVector4(0.05F, 0.05F, 0.07F, 0.85F);
			const Vector4 kScrollColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.25F);
			const Vector4 kFreshTextBg = MakeVector4(0.3F, 0.5F, 0.9F, 0.45F);
		} // namespace

		TreeView::TreeView(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void TreeView::SetItems(std::vector<Item> newItems) {
			items = std::move(newItems);
			// Whatever a gesture was about may have gone with the change.
			if (IsRenaming() && IndexOf(renaming) < 0)
				EndRename(false);
			if (press.active && IndexOf(press.item) < 0)
				CancelGesture();
			// Forget folds of items no longer there, so the set cannot grow
			// without bound over a long session.
			for (auto it = folded.begin(); it != folded.end();)
				it = (IndexOf(*it) < 0) ? folded.erase(it) : std::next(it);
			RebuildRows();
			ClampScroll();
		}

		void TreeView::SetBounds(const AABB2& b) {
			bounds = b;
			ClampScroll();
		}

		bool TreeView::Contains(const Vector2& p) const {
			return OverlayInRect(p, bounds.GetMinX(), bounds.GetMinY(), bounds.GetWidth(),
			                     bounds.GetHeight());
		}

		int TreeView::IndexOf(ItemId id) const {
			if (id == kNoItem)
				return -1;
			for (std::size_t i = 0; i < items.size(); i++) {
				if (items[i].id == id)
					return int(i);
			}
			return -1;
		}

		void TreeView::RebuildRows() {
			rows.clear();
			// Everything deeper than a folded item is hidden until the list
			// comes back up to its depth.
			int hideBelow = INT_MAX;
			for (std::size_t i = 0; i < items.size(); i++) {
				const Item& item = items[i];
				if (item.depth > hideBelow)
					continue;
				hideBelow = INT_MAX;
				const bool hasChildren = i + 1 < items.size() && items[i + 1].depth > item.depth;
				rows.push_back({i, hasChildren});
				if (hasChildren && folded.count(item.id))
					hideBelow = item.depth;
			}
		}

		int TreeView::VisibleRowCount() const {
			return std::max(0, int(std::floor((bounds.GetHeight() - kPadTop) / kRowHeight)));
		}

		void TreeView::ClampScroll() {
			scroll = std::max(0, std::min(scroll, int(rows.size()) - VisibleRowCount()));
		}

		float TreeView::RowTop(int row) const {
			return bounds.GetMinY() + kPadTop + float(row - scroll) * kRowHeight;
		}

		int TreeView::RowAt(float y) const {
			if (y < bounds.GetMinY() + kPadTop)
				return -1;
			const int row = scroll + int(std::floor((y - bounds.GetMinY() - kPadTop) / kRowHeight));
			const int end = std::min(int(rows.size()), scroll + VisibleRowCount());
			return (row >= scroll && row < end) ? row : -1;
		}

		float TreeView::LabelX(const Item& item) const {
			return bounds.GetMinX() + kPadX + float(item.depth) * kIndent + kFoldWidth + kLabelGap;
		}

		void TreeView::Reveal(ItemId id) {
			const int index = IndexOf(id);
			if (index < 0)
				return;
			// Walk back up the list: each shallower item on the way is a parent.
			int depth = items[std::size_t(index)].depth;
			for (int i = index - 1; i >= 0 && depth > 0; i--) {
				if (items[std::size_t(i)].depth < depth) {
					folded.erase(items[std::size_t(i)].id);
					depth = items[std::size_t(i)].depth;
				}
			}
			RebuildRows();
			for (int r = 0; r < int(rows.size()); r++) {
				if (rows[std::size_t(r)].item != std::size_t(index))
					continue;
				if (r < scroll)
					scroll = r;
				else if (r >= scroll + VisibleRowCount())
					scroll = r - VisibleRowCount() + 1;
				break;
			}
			ClampScroll();
		}

		TreeView::ItemId TreeView::ItemAt(const Vector2& p) const {
			if (!Contains(p))
				return kNoItem;
			const int row = RowAt(p.y);
			return row < 0 ? kNoItem : items[rows[std::size_t(row)].item].id;
		}

		bool TreeView::MouseDown(const Vector2& p, bool extend, float time) {
			if (!Contains(p))
				return false;
			if (IsRenaming()) {
				const AABB2 field = RenameRect();
				if (field.GetWidth() > 0.0F && field.Contains(p))
					return true; // a press in the box being typed into keeps typing
				EndRename(true);
			}
			const int row = RowAt(p.y);
			if (row < 0) {
				// Blank space: clicking it picks nothing, as in any outliner.
				if (!extend && OnEmptyClicked)
					OnEmptyClicked();
				lastClickItem = kNoItem;
				return true;
			}
			const Row& r = rows[std::size_t(row)];
			const Item& item = items[r.item];

			const float foldX = LabelX(item) - kLabelGap - kFoldWidth;
			if (r.hasChildren && p.x >= foldX && p.x < foldX + kFoldWidth) {
				if (!folded.erase(item.id))
					folded.insert(item.id);
				RebuildRows();
				ClampScroll();
				sounds.Activate();
				return true;
			}

			if (!extend && item.id == lastClickItem && time - lastClickTime <= kDoubleClickTime) {
				lastClickItem = kNoItem;
				BeginRename(item.id);
				return true;
			}
			lastClickItem = item.id;
			lastClickTime = time;

			press = Press();
			press.active = true;
			press.item = item.id;
			press.at = p;
			press.clickOnRelease = item.selected && !extend;
			sounds.Activate();
			if (!press.clickOnRelease && OnItemClicked)
				OnItemClicked(item.id, extend);
			return true;
		}

		void TreeView::MouseMove(const Vector2& p) {
			if (!press.active)
				return;
			if (!press.dragging) {
				const Vector2 moved = p - press.at;
				if (moved.x * moved.x + moved.y * moved.y < kDragThreshold * kDragThreshold)
					return;
				press.dragging = true;
				press.clickOnRelease = false;
				lastClickItem = kNoItem; // a drag is no half of a double click
			}
			haveDrop = TargetAt(p, drop) && CanDrop && CanDrop(press.item, drop);
		}

		void TreeView::MouseUp(const Vector2& p) {
			if (!press.active)
				return;
			MouseMove(p);
			const Press ended = press;
			const bool dropping = haveDrop;
			const DropTarget target = drop;
			press = Press();
			haveDrop = false;
			// Reported last: the handler may change the host's items.
			if (ended.dragging) {
				if (dropping && OnDropped)
					OnDropped(ended.item, target);
			} else if (ended.clickOnRelease && OnItemClicked) {
				OnItemClicked(ended.item, false);
			}
		}

		bool TreeView::Wheel(const Vector2& p, float by) {
			if (!Contains(p))
				return false;
			// Whole rows only: nothing is drawn clipped, so a row is either
			// shown in full or not at all.
			const int step = (by > 0.0F) ? int(std::ceil(by)) : int(std::floor(by));
			scroll += step;
			ClampScroll();
			if (press.dragging)
				MouseMove(p); // the row under the cursor changed
			return true;
		}

		void TreeView::CancelGesture() {
			press = Press();
			haveDrop = false;
		}

		bool TreeView::TargetAt(const Vector2& p, DropTarget& out) const {
			if (!Contains(p))
				return false;
			const int row = RowAt(p.y);
			if (row < 0) {
				// Under the last row: the end of the top level. Only when the
				// last row is in sight, or the place shown would be a lie.
				if (p.y < bounds.GetMinY() + kPadTop ||
				    scroll + VisibleRowCount() < int(rows.size()))
					return false;
				out = DropTarget();
				return true;
			}
			const Row& r = rows[std::size_t(row)];
			const Item& item = items[r.item];
			const float within = (p.y - RowTop(row)) / kRowHeight;
			out.item = item.id;
			out.place = within < kDropEdge ? DropPlace::Before
			            : within > 1.0F - kDropEdge ? DropPlace::After
			                                        : DropPlace::Inside;
			// Just under an unfolded parent is just above its first child: the
			// line drawn there says so, so that is where it lands.
			if (out.place == DropPlace::After && r.hasChildren && !folded.count(item.id) &&
			    std::size_t(row) + 1 < rows.size()) {
				out.item = items[rows[std::size_t(row) + 1].item].id;
				out.place = DropPlace::Before;
			}
			return out.item != press.item;
		}

		// --- Renaming -------------------------------------------------------

		void TreeView::BeginRename(ItemId id) {
			const int index = IndexOf(id);
			if (index < 0)
				return;
			if (IsRenaming() && renaming != id)
				EndRename(true);
			CancelGesture();
			Reveal(id);
			renaming = id;
			renameFrom = items[std::size_t(index)].label;
			entry.Begin(renameFrom, kMaxNameBytes);
		}

		void TreeView::EndRename(bool commit) {
			if (!IsRenaming())
				return;
			const ItemId id = renaming;
			const std::string name = entry.Text();
			const std::string from = renameFrom;
			entry.End();
			renaming = kNoItem;
			renameFrom.clear();
			// Reported last: the handler may change the host's items.
			if (commit && name != from && OnRenamed)
				OnRenamed(id, name);
		}

		bool TreeView::KeyEvent(const std::string& key) {
			if (!IsRenaming())
				return false;
			if (key == "Enter")
				EndRename(true);
			else if (key == "Escape")
				EndRename(false);
			else if (key == "BackSpace")
				entry.Backspace();
			else if (key == "Delete")
				entry.Delete();
			else if (key == "Left")
				entry.MoveCaret(-1);
			else if (key == "Right")
				entry.MoveCaret(1);
			else if (key == "Home")
				entry.CaretToStart();
			else if (key == "End")
				entry.CaretToEnd();
			// Every other key is swallowed while a name is typed, so no editor
			// shortcut fires on a letter meant for the text.
			return true;
		}

		void TreeView::TextInputEvent(const std::string& text) {
			if (IsRenaming())
				entry.Insert(text);
		}

		AABB2 TreeView::RenameRect() const {
			const int index = IndexOf(renaming);
			if (!IsRenaming() || index < 0)
				return AABB2(0.0F, 0.0F, 0.0F, 0.0F);
			for (int r = scroll; r < std::min(int(rows.size()), scroll + VisibleRowCount()); r++) {
				if (rows[std::size_t(r)].item != std::size_t(index))
					continue;
				const float x = LabelX(items[std::size_t(index)]) - kRenameInset;
				return AABB2(x, RowTop(r) + 1.0F,
				             std::max(0.0F, bounds.GetMaxX() - kPadX - x), kRowHeight - 2.0F);
			}
			return AABB2(0.0F, 0.0F, 0.0F, 0.0F);
		}

		// --- Drawing --------------------------------------------------------

		void TreeView::DrawRenameField(client::IRenderer& renderer, client::IFont& font,
		                               float x, float y, float width) {
			widgets::PaintField(renderer, MakeVector2(x, y), MakeVector2(width, kRowHeight - 2.0F),
			                    true, false);
			const std::string& text = entry.Text();
			const int caret = entry.Caret();
			const float room = std::max(0.0F, width - kRenameInset * 2.0F);
			// Nothing is clipped, so the text is shown from the first character
			// that keeps the caret inside the box.
			int start = 0;
			while (start < caret &&
			       font.Measure(text.substr(std::size_t(start), std::size_t(caret - start))).x >
			         room)
				start = ui::GetByteIndexForString(text, 1, start);
			const std::string shown = ui::ElideText(font, text.substr(std::size_t(start)), room);
			const Vector2 size = font.Measure(shown);
			const float textX = x + kRenameInset;
			const float textY = y + (kRowHeight - 2.0F - size.y) * 0.5F;
			if (entry.IsFresh() && !shown.empty()) {
				// All of it is marked: the first key typed replaces it.
				OverlayColorNP(renderer, kFreshTextBg);
				OverlayFillRect(renderer, textX, y + 3.0F, size.x, kRowHeight - 8.0F);
			}
			font.Draw(shown, MakeVector2(textX, textY), 1.0F, kTextColor);
			const float caretX =
			  textX +
			  font.Measure(text.substr(std::size_t(start), std::size_t(caret - start))).x;
			OverlayColorNP(renderer, kTextColor);
			OverlayFillRect(renderer, caretX, y + 3.0F, 1.0F, kRowHeight - 8.0F);
		}

		void TreeView::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                    const Vector2& cursorPos, bool menuActive) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const float left = bounds.GetMinX();
			const float right = bounds.GetMaxX() - kPadX;

			if (rows.empty()) {
				if (!emptyText.empty()) {
					const std::string text =
					  ui::ElideText(font, emptyText, bounds.GetWidth() - kPadX * 2.0F);
					font.Draw(text, MakeVector2(left + kPadX, bounds.GetMinY() + kPadTop + 2.0F),
					          1.0F, kDimTextColor);
				}
				return;
			}

			const int hoverRow = (!menuActive && Contains(cursorPos)) ? RowAt(cursorPos.y) : -1;
			const int end = std::min(int(rows.size()), scroll + VisibleRowCount());
			for (int r = scroll; r < end; r++) {
				const Row& row = rows[std::size_t(r)];
				const Item& item = items[row.item];
				const float y = RowTop(r);
				const float labelX = LabelX(item);

				const bool dropInside =
				  press.dragging && haveDrop && drop.place == DropPlace::Inside && drop.item == item.id;
				if (item.selected || (r == hoverRow && !press.dragging) || dropInside) {
					OverlayColorNP(renderer, item.active     ? kActiveBg
					                         : item.selected ? kSelectedBg
					                                         : kHoverBg);
					OverlayFillRect(renderer, left, y, bounds.GetWidth(), kRowHeight);
				}
				if (dropInside)
					OverlayStrokeRect(renderer, left + 1.0F, y, bounds.GetWidth() - 2.0F, kRowHeight,
					                  1.5F, kDropColor);

				if (row.hasChildren) {
					// Pointing right while folded, down while open.
					const float cx = labelX - kLabelGap - kFoldWidth * 0.5F;
					const float cy = y + kRowHeight * 0.5F;
					const float s = 3.5F;
					Vector2 tri[3];
					if (folded.count(item.id)) {
						tri[0] = MakeVector2(cx - s * 0.6F, cy - s);
						tri[1] = MakeVector2(cx + s * 0.8F, cy);
						tri[2] = MakeVector2(cx - s * 0.6F, cy + s);
					} else {
						tri[0] = MakeVector2(cx - s, cy - s * 0.6F);
						tri[1] = MakeVector2(cx + s, cy - s * 0.6F);
						tri[2] = MakeVector2(cx, cy + s * 0.8F);
					}
					OverlayFillConvexPolygon(renderer, tri, 3, kFoldColor);
				}

				if (IsRenaming() && renaming == item.id) {
					DrawRenameField(renderer, font, labelX - kRenameInset, y + 1.0F,
					                std::max(0.0F, right - (labelX - kRenameInset)));
					continue;
				}
				const std::string label = ui::ElideText(font, item.label, right - labelX);
				const Vector2 size = font.Measure(label);
				font.Draw(label, MakeVector2(labelX, y + (kRowHeight - size.y) * 0.5F), 1.0F,
				          item.active ? kActiveTextColor : item.dimmed ? kDimTextColor : kTextColor);
			}

			// Where a drop beside an item lands: a line at the depth it would take.
			if (press.dragging && haveDrop && drop.place != DropPlace::Inside) {
				float lineY = RowTop(end);
				float lineX = left + kPadX;
				for (int r = scroll; r < end; r++) {
					const Item& item = items[rows[std::size_t(r)].item];
					if (item.id != drop.item)
						continue;
					lineY = RowTop(r) + (drop.place == DropPlace::After ? kRowHeight : 0.0F);
					lineX = LabelX(item) - kLabelGap - kFoldWidth;
					break;
				}
				OverlayColorNP(renderer, kDropColor);
				OverlayFillRect(renderer, lineX, lineY - 1.0F, right - lineX, 2.0F);
				OverlayFillCircle(renderer, MakeVector2(lineX, lineY), 3.0F, kDropColor);
			}

			// A thin bar says how much of the list is out of sight.
			const int visible = VisibleRowCount();
			if (int(rows.size()) > visible && visible > 0) {
				const float trackY = bounds.GetMinY() + kPadTop;
				const float trackH = float(visible) * kRowHeight;
				const float thumbH = std::max(8.0F, trackH * float(visible) / float(rows.size()));
				const float thumbY =
				  trackY + (trackH - thumbH) * float(scroll) / float(int(rows.size()) - visible);
				OverlayColorNP(renderer, kScrollColor);
				OverlayFillRect(renderer, bounds.GetMaxX() - kScrollBarWidth - 1.0F, thumbY,
				                kScrollBarWidth, thumbH);
			}

			// What is being dragged follows the cursor.
			if (press.dragging) {
				const int index = IndexOf(press.item);
				if (index >= 0) {
					const std::string& label = items[std::size_t(index)].label;
					const Vector2 size = font.Measure(label);
					const Vector2 at = cursorPos + MakeVector2(14.0F, 6.0F);
					OverlayColorNP(renderer, kGhostBg);
					OverlayFillRect(renderer, at.x - 4.0F, at.y - 2.0F, size.x + 8.0F, size.y + 4.0F);
					font.Draw(label, at, 1.0F,
					          haveDrop ? kTextColor : MakeVector4(1.0F, 0.45F, 0.4F, 1.0F));
				}
			}
		}
	} // namespace gui
} // namespace spades
