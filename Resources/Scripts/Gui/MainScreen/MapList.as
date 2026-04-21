/*
 Copyright (c) 2026 ZeroSpades contributors

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

namespace spades {

	// Format a Unix-epoch timestamp as "YYYY-MM-DD HH:MM" (UTC).
	// Handles dates from 1970 through year 9999.
	string FormatMapModified(int64 unixSeconds) {
		if (unixSeconds <= 0)
			return "";

		// Day count since 1970-01-01
		int64 days = unixSeconds / 86400;
		int64 secOfDay = unixSeconds - days * 86400;

		int hour = int(secOfDay / 3600);
		int minute = int((secOfDay / 60) % 60);

		// Convert day count to civil date using Howard Hinnant's algorithm.
		// Works for the entire proleptic Gregorian calendar range we care about.
		int64 z = days + 719468;
		int64 era = (z >= 0 ? z : z - 146096) / 146097;
		int64 doe = z - era * 146097;                   // [0, 146096]
		int64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
		int64 y = yoe + era * 400;
		int64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // [0, 365]
		int64 mp = (5 * doy + 2) / 153;                                    // [0, 11]
		int day = int(doy - (153 * mp + 2) / 5 + 1);                       // [1, 31]
		int month = int(mp < 10 ? mp + 3 : mp - 9);                        // [1, 12]
		if (month <= 2)
			y += 1;
		int year = int(y);

		string result = "" + year + "-";
		if (month < 10) result += "0";
		result += month;
		result += "-";
		if (day < 10) result += "0";
		result += day;
		result += " ";
		if (hour < 10) result += "0";
		result += hour;
		result += ":";
		if (minute < 10) result += "0";
		result += minute;
		return result;
	}

	string FormatMapFileSize(int64 bytes) {
		if (bytes < 0)
			return "";
		if (bytes < 1024)
			return bytes + " B";
		if (bytes < 1024 * 1024)
			return (bytes / 1024) + " KB";
		return (bytes / (1024 * 1024)) + " MB";
	}

	class MapListItem : spades::ui::ButtonBase {
		MainScreenMapItem@ item;
		float colSizeWidth;
		float colDateWidth;

		MapListItem(spades::ui::UIManager@ manager, MainScreenMapItem@ item,
		            float colSizeWidth, float colDateWidth) {
			super(manager);
			@this.item = item;
			this.colSizeWidth = colSizeWidth;
			this.colDateWidth = colDateWidth;
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos  = ScreenPosition;
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

			// Column order (left to right): Name | Modified | Size
			float x = pos.x + 2.0F;
			Font.Draw(item.DisplayName, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			float dateX = pos.x + size.x - colSizeWidth - colDateWidth;
			Font.Draw(FormatMapModified(item.Modified),
			          Vector2(dateX, pos.y + 2.0F), 1.0F, fgcolor);

			float sizeX = pos.x + size.x - colSizeWidth;
			Font.Draw(FormatMapFileSize(item.SizeBytes),
			          Vector2(sizeX, pos.y + 2.0F), 1.0F, fgcolor);
		}
	}

	funcdef void MapListItemEventHandler(MapListModel@ sender, MainScreenMapItem@ item);

	class MapListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		MainScreenMapItem@[] list;
		MapListItem@[] itemElements;
		float colSizeWidth;
		float colDateWidth;

		MapListItemEventHandler@ ItemActivated;
		MapListItemEventHandler@ ItemDoubleClicked;

		MapListModel(spades::ui::UIManager@ manager, MainScreenMapItem@[]@ list,
		             float colSizeWidth, float colDateWidth) {
			@this.manager     = manager;
			this.list         = list;
			this.colSizeWidth = colSizeWidth;
			this.colDateWidth = colDateWidth;
			itemElements.resize(list.length);
		}

		int NumRows {
			get { return int(list.length); }
		}

		private void OnItemClicked(spades::ui::UIElement@ sender) {
			MapListItem@ item = cast<MapListItem>(sender);
			if (ItemActivated !is null)
				ItemActivated(this, item.item);
		}

		private void OnItemDoubleClicked(spades::ui::UIElement@ sender) {
			MapListItem@ item = cast<MapListItem>(sender);
			if (ItemDoubleClicked !is null)
				ItemDoubleClicked(this, item.item);
		}

		spades::ui::UIElement@ CreateElement(int row) {
			if (itemElements[row] is null) {
				MapListItem i(manager, list[row], colSizeWidth, colDateWidth);
				@i.Activated     = spades::ui::EventHandler(this.OnItemClicked);
				@i.DoubleClicked = spades::ui::EventHandler(this.OnItemDoubleClicked);
				@itemElements[row] = i;
			}
			return itemElements[row];
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	class MapListHeader : spades::ui::UIElement {
		string Text;
		MapListHeader(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos  = ScreenPosition;
			Vector2 size = Size;

			Font.Draw(Text, pos + Vector2(2.0F, (size.y - Font.Measure(Text).y) * 0.5F), 1.0F,
			          Vector4(1.0F, 1.0F, 1.0F, 1.0F));
		}
	}

}
