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

namespace spades {
	funcdef void ModListItemEventHandler(string modName);

	// Maps an official CATEGORY tag to a weapon icon in the game resources.
	// Returns "" for categories with no weapon icon (FONT, SFX, VFX, ...), which
	// are shown as a text pill instead. Matching is against the uppercase tags
	// used by the official repo.
	string ModCategoryIcon(string category) {
		if (category == "SEMI")    return "Gfx/Hotbar/Rifle.png";
		if (category == "SMG")     return "Gfx/Hotbar/SMG.png";
		if (category == "SHOTGUN") return "Gfx/Hotbar/Shotgun.png";
		if (category == "SPADE")   return "Gfx/Hotbar/Spade.png";
		if (category == "GRENADE") return "Gfx/Hotbar/Grenade.png";
		return "";
	}

	class ModListItem : spades::ui::ButtonBase {
		string modName;
		string category;    // e.g. SEMI/SMG/...; empty when the name is unstructured
		string displayName; // parsed name, or the full filename when unstructured
		string author;      // empty when unstructured
		int64 totalSize;
		bool enabled;   // present in the apply history
		bool exists;    // mod still present on disk
		int orderNum;   // 1-based apply position, 0 when disabled
		float checkColWidth;
		float orderColWidth;
		float tagColWidth;
		float nameColWidth;
		float authorColWidth;
		float sizeColWidth;

		ModListItem(spades::ui::UIManager@ manager, string modName, string category,
		            string displayName, string author, int64 totalSize, bool enabled, bool exists,
		            int orderNum, float checkColWidth, float orderColWidth, float tagColWidth,
		            float nameColWidth, float authorColWidth, float sizeColWidth) {
			super(manager);
			this.modName = modName;
			this.category = category;
			this.displayName = displayName;
			this.author = author;
			this.totalSize = totalSize;
			this.enabled = enabled;
			this.exists = exists;
			this.orderNum = orderNum;
			this.checkColWidth = checkColWidth;
			this.orderColWidth = orderColWidth;
			this.tagColWidth = tagColWidth;
			this.nameColWidth = nameColWidth;
			this.authorColWidth = authorColWidth;
			this.sizeColWidth = sizeColWidth;
		}

		// Draw the tag cell: a weapon icon for weapon categories, a small text
		// pill for other structured categories (FONT/SFX/VFX/...), nothing for an
		// unstructured filename.
		private void RenderTag(Renderer@ r, float cellX, float cellY, float cellH, Vector4 fgcolor) {
			if (category.length == 0)
				return;
			string iconPath = ModCategoryIcon(category);
			if (iconPath.length > 0) {
				Image@ icon = r.RegisterImage(iconPath);
				// Fit within the cell height, preserving aspect ratio.
				float h = cellH - 8.0F;
				float w = h * (float(icon.Width) / float(icon.Height));
				if (w > tagColWidth - 6.0F) {
					w = tagColWidth - 6.0F;
					h = w * (float(icon.Height) / float(icon.Width));
				}
				float ix = cellX + (tagColWidth - w) * 0.5F;
				float iy = cellY + (cellH - h) * 0.5F;
				r.ColorNP = fgcolor;
				r.DrawImage(icon, AABB2(ix, iy, w, h));
			} else {
				// Non-weapon category: draw the tag text, clipped to the cell.
				Font.Draw(category, Vector2(cellX + 2.0F, cellY + 2.0F), 1.0F, fgcolor);
			}
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			Vector4 bgcolor = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
			// White when disabled, green when enabled, orange when enabled but
			// the mod is no longer on disk.
			Vector4 fgcolor = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (enabled)
				fgcolor = exists ? Vector4(0.4F, 1.0F, 0.4F, 1.0F)
				                 : Vector4(1.0F, 0.62F, 0.1F, 1.0F);

			if (Pressed and Hover) {
				bgcolor.w = 0.3F;
			} else if (Hover) {
				bgcolor.w = 0.15F;
			}

			r.ColorNP = bgcolor;
			r.DrawImage(null, AABB2(pos.x + 1.0F, pos.y + 1.0F, size.x, size.y));

			// Checkbox.
			float boxSize = 14.0F;
			float boxX = pos.x + 5.0F;
			float boxY = pos.y + (size.y - boxSize) * 0.5F;
			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.25F);
			r.DrawImage(null, AABB2(boxX, boxY, boxSize, boxSize));
			if (enabled) {
				r.ColorNP = fgcolor;
				r.DrawImage(null, AABB2(boxX + 3.0F, boxY + 3.0F, boxSize - 6.0F, boxSize - 6.0F));
			}

			// Apply-order number (own column, blank when disabled).
			float x = pos.x + checkColWidth;
			if (enabled and orderNum > 0)
				Font.Draw("" + orderNum, Vector2(x + 2.0F, pos.y + 2.0F), 1.0F, fgcolor);

			// Tag column (weapon icon or category pill).
			x = pos.x + checkColWidth + orderColWidth;
			RenderTag(r, x, pos.y, size.y, fgcolor);

			// Mod name. For an unstructured name this holds the full filename and
			// runs into the author column, matching the previous behaviour.
			x = pos.x + checkColWidth + orderColWidth + tagColWidth + 2.0F;
			Font.Draw(displayName, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Author.
			x += nameColWidth;
			if (author.length > 0)
				Font.Draw(author, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Size.
			x += authorColWidth;
			Font.Draw(exists ? FormatFileSize(totalSize) : "-", Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
		}
	}

	class ModListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		ModsScreenHelper@ helper;
		string[] list;
		int[] orders;     // parallel to list: 1-based apply position, 0 if disabled
		bool[] exists;    // parallel to list: mod still present on disk
		float checkColWidth;
		float orderColWidth;
		float tagColWidth;
		float nameColWidth;
		float authorColWidth;
		float sizeColWidth;
		ModListItem@[] itemElements;

		ModListItemEventHandler@ ItemActivated;

		ModListModel(spades::ui::UIManager@ manager, ModsScreenHelper@ helper, string[]@ list,
		             int[]@ orders, bool[]@ exists, float checkColWidth, float orderColWidth,
		             float tagColWidth, float nameColWidth, float authorColWidth, float sizeColWidth) {
			@this.manager = manager;
			@this.helper = helper;
			this.list = list;
			this.orders = orders;
			this.exists = exists;
			this.checkColWidth = checkColWidth;
			this.orderColWidth = orderColWidth;
			this.tagColWidth = tagColWidth;
			this.nameColWidth = nameColWidth;
			this.authorColWidth = authorColWidth;
			this.sizeColWidth = sizeColWidth;

			itemElements.resize(list.length);
		}

		int NumRows { get { return int(list.length); } }

		private void OnItemClicked(spades::ui::UIElement@ sender) {
			ModListItem@ item = cast<ModListItem>(sender);
			if (ItemActivated !is null)
				ItemActivated(item.modName);
		}

		spades::ui::UIElement@ CreateElement(int row) {
			if (itemElements[row] is null) {
				string name = list[row];
				bool ex = exists[row];
				int64 size = ex ? helper.GetModTotalSize(name) : 0;
				string category = helper.GetModCategory(name);
				string displayName = helper.GetModDisplayName(name);
				string author = helper.GetModAuthor(name);
				ModListItem item(manager, name, category, displayName, author, size, orders[row] > 0,
				                 ex, orders[row], checkColWidth, orderColWidth, tagColWidth,
				                 nameColWidth, authorColWidth, sizeColWidth);
				@item.Activated = spades::ui::EventHandler(this.OnItemClicked);
				@itemElements[row] = item;
			}
			return itemElements[row];
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	class ModListHeader : spades::ui::UIElement {
		string Text;
		ModListHeader(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Font.Draw(Text, pos + Vector2(2.0F, (size.y - Font.Measure(Text).y) * 0.5F), 1.0F,
			          Vector4(1.0F, 1.0F, 1.0F, 1.0F));
		}
	}

	// Simple fill-bar progress indicator. Fraction is clamped to [0, 1].
	class ModsProgressBar : spades::ui::UIElement {
		float Fraction = 0.0F;

		ModsProgressBar(spades::ui::UIManager@ manager) { super(manager); }

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			float f = Fraction;
			if (f < 0.0F) f = 0.0F;
			if (f > 1.0F) f = 1.0F;

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.12F);
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.55F);
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x * f, size.y));
		}
	}
}
