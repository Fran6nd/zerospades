/*
 Copyright (c) 2026 ZeroSpades developers

 This file is part of ZeroSpades.

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

	string FormatModSize(int64 bytes) {
		if (bytes < 1024)
			return "" + bytes + " B";
		if (bytes < 1024 * 1024)
			return "" + (bytes / 1024) + " KB";
		return "" + (bytes / (1024 * 1024)) + " MB";
	}

	class ModListItem : spades::ui::ButtonBase {
		string modName;
		int pakCount;
		int64 totalSize;
		float nameColWidth;
		float countColWidth;
		float sizeColWidth;

		ModListItem(spades::ui::UIManager@ manager, string modName, int pakCount, int64 totalSize,
		            float nameColWidth, float countColWidth, float sizeColWidth) {
			super(manager);
			this.modName = modName;
			this.pakCount = pakCount;
			this.totalSize = totalSize;
			this.nameColWidth = nameColWidth;
			this.countColWidth = countColWidth;
			this.sizeColWidth = sizeColWidth;
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			Vector4 bgcolor = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
			Vector4 fgcolor = Vector4(1.0F, 1.0F, 1.0F, 1.0F);

			if (Pressed and Hover) {
				bgcolor.w = 0.3F;
			} else if (Hover) {
				bgcolor.w = 0.15F;
			}

			r.ColorNP = bgcolor;
			r.DrawImage(null, AABB2(pos.x + 1.0F, pos.y + 1.0F, size.x, size.y));

			float x = pos.x + 2.0F;
			Font.Draw(modName, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			x = pos.x + nameColWidth + 2.0F;
			Font.Draw("" + pakCount, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			x += countColWidth;
			Font.Draw(FormatModSize(totalSize), Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
		}
	}

	class ModListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		ModsScreenHelper@ helper;
		string[] list;
		float nameColWidth;
		float countColWidth;
		float sizeColWidth;
		ModListItem@[] itemElements;

		ModListItemEventHandler@ ItemActivated;

		ModListModel(spades::ui::UIManager@ manager, ModsScreenHelper@ helper,
		             string[]@ list, float nameColWidth, float countColWidth, float sizeColWidth) {
			@this.manager = manager;
			@this.helper = helper;
			this.list = list;
			this.nameColWidth = nameColWidth;
			this.countColWidth = countColWidth;
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
				int count = helper.GetModPakCount(name);
				int64 size = helper.GetModTotalSize(name);
				ModListItem item(manager, name, count, size, nameColWidth, countColWidth, sizeColWidth);
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
}
