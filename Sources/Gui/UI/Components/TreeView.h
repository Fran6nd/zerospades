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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>
#include <Gui/UI/Widgets/TextEntry.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * An immediate-mode tree of named items: an outliner.
		 *
		 * The host hands over the whole tree every frame as a flat list in
		 * pre-order (each parent before its children, `depth` saying how far
		 * in each one sits), with what is selected marked. The tree keeps
		 * only what is its own: which branches are folded, how far it is
		 * scrolled, and the gesture in progress.
		 *
		 * It decides nothing about the items. A click, a rename or a drop is
		 * reported to the host, which changes its data (or refuses to) and
		 * hands the result back on the next frame. Where an item may be
		 * dropped is asked of the host too (`CanDrop`) while the drag is on,
		 * so a place the host would refuse is never offered.
		 *
		 * Items are named by id throughout, never by position, so a list that
		 * changes shape under a gesture never moves it onto another item.
		 */
		class TreeView {
		public:
			using ItemId = std::uint64_t;
			/** Names no item: a drop past the last row lands at the end of the top level. */
			static constexpr ItemId kNoItem = 0;

			struct Item {
				ItemId id = kNoItem;
				int depth = 0;
				std::string label;
				bool selected = false;
				// The one selected item the others follow (drawn brighter).
				bool active = false;
				// Drawn fainter: an item that is there but holds less (a group).
				bool dimmed = false;
			};

			/** Where a dragged item would land relative to the item under it. */
			enum class DropPlace { Before, After, Inside };
			struct DropTarget {
				ItemId item = kNoItem; // kNoItem: after everything, at the top level
				DropPlace place = DropPlace::After;
			};

			explicit TreeView(client::IAudioDevice* audioDevice = nullptr);

			/** The tree as it stands, in pre-order. */
			void SetItems(std::vector<Item> items);
			/** Shown in place of the rows when there are none. */
			void SetEmptyText(std::string text) { emptyText = std::move(text); }
			void SetBounds(const AABB2& bounds);
			bool Contains(const Vector2& p) const;

			/** Unfolds every branch above `id` and scrolls it into view. */
			void Reveal(ItemId id);

			// --- Pointer ------------------------------------------------------
			// `time` is in seconds, from any clock that only goes forward: it
			// tells a double click from two single ones.
			/** A press at `p`; false when it is not over the tree. */
			bool MouseDown(const Vector2& p, bool extend, float time);
			void MouseMove(const Vector2& p);
			void MouseUp(const Vector2& p);
			/** Scrolls by `rows`; false when `p` is not over the tree. */
			bool Wheel(const Vector2& p, float rows);
			/** Whether a press on the tree is still held (it may become a drag). */
			bool IsPressed() const { return press.active; }
			/** Drops the press or drag in progress without reporting it. */
			void CancelGesture();

			// --- Renaming -----------------------------------------------------
			// The tree has no retained widget to take keyboard focus, so the
			// host asks whether it is renaming and hands it what it needs.
			/** Starts typing a new name for `id` over its label. */
			void BeginRename(ItemId id);
			bool IsRenaming() const { return entry.IsEditing(); }
			/** Takes a key while renaming; false if it means nothing here. */
			bool KeyEvent(const std::string& key);
			void TextInputEvent(const std::string& text);
			/** The box being typed into, for the IME; empty when none is. */
			AABB2 RenameRect() const;
			/** Stops renaming; `commit` reports the typed name when it changed. */
			void EndRename(bool commit);

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			          const Vector2& cursorPos, bool menuActive);

			// --- What the host is told ----------------------------------------
			/** A row was clicked; `extend` adds to (or drops from) the selection. */
			std::function<void(ItemId id, bool extend)> OnItemClicked;
			/** The blank space under the rows was clicked. */
			std::function<void()> OnEmptyClicked;
			std::function<void(ItemId id, const std::string& name)> OnRenamed;
			/** Whether `dragged` may land at `target`. Unset, nowhere is. */
			std::function<bool(ItemId dragged, const DropTarget& target)> CanDrop;
			std::function<void(ItemId dragged, const DropTarget& target)> OnDropped;

			/** Longest name a rename takes, in bytes. */
			static constexpr std::size_t kMaxNameBytes = 63;

		private:
			// One row on screen: the item it shows, and whether it has children
			// (and so a fold arrow).
			struct Row {
				std::size_t item;
				bool hasChildren;
			};
			// A press on the tree, which becomes a drag once it has moved far
			// enough.
			struct Press {
				bool active = false;
				ItemId item = kNoItem;
				Vector2 at = MakeVector2(0.0F, 0.0F);
				bool dragging = false;
				// A press on an item already selected keeps the selection until
				// the release, so the whole selection can be dragged; the click
				// only picks the one item if no drag came of it.
				bool clickOnRelease = false;
			};

			ui::FeedbackSounds sounds;
			std::vector<Item> items;
			std::vector<Row> rows; // what is unfolded, in order
			std::set<ItemId> folded;
			std::string emptyText;
			AABB2 bounds = AABB2(0.0F, 0.0F, 0.0F, 0.0F);
			int scroll = 0; // the first row shown
			Press press;
			bool haveDrop = false;
			DropTarget drop;
			// The last click on a row, for telling a double click.
			ItemId lastClickItem = kNoItem;
			float lastClickTime = -1.0F;
			// The item being renamed and what it was called, so an unchanged
			// name is not reported.
			ItemId renaming = kNoItem;
			std::string renameFrom;
			TextEntry entry;
			ItemId hovered = kNoItem;

			void RebuildRows();
			int VisibleRowCount() const;
			void ClampScroll();
			/** The row at screen height `y`, or -1 past the last one. */
			int RowAt(float y) const;
			float RowTop(int row) const;
			float LabelX(const Item& item) const;
			/** Index into `items` of `id`, or -1. */
			int IndexOf(ItemId id) const;
			// Where a drag held at `p` would land; false when nowhere.
			bool TargetAt(const Vector2& p, DropTarget& out) const;
			void DrawRenameField(client::IRenderer& renderer, client::IFont& font, float x,
			                     float y, float width);
		};
	} // namespace gui
} // namespace spades
