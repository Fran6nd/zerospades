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

	class ExtensionRow {
		int id;
		string name;        // empty if unknown to this client
		int clientVersion;  // -1 if not implemented client-side
		int serverVersion;  // -1 if not advertised by the server

		ExtensionRow(int id, string name, int clientVersion, int serverVersion) {
			this.id = id;
			this.name = name;
			this.clientVersion = clientVersion;
			this.serverVersion = serverVersion;
		}
	}

	class DisconnectScreen : spades::ui::UIElement {
		private float ContentsTop, ContentsHeight, ContentsLeft, ContentsWidth;
		private float TableTop, TableBottom;
		private float ColNameX, ColClientX, ColServerX;
		private float ColNameW, ColClientW, ColServerW;
		private float HeaderRowH;

		private spades::ui::UIElement@ owner;

		DisconnectScreen(spades::ui::UIElement@ owner, string headerText, ExtensionRow@[]@ rows) {
			super(owner.Manager);
			@this.owner = owner;
			@Font = Manager.RootElement.Font;
			this.Bounds = owner.Bounds;

			HeaderRowH = 26.0F;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			ContentsWidth = 640.0F;
			if (ContentsWidth > sw - 16.0F)
				ContentsWidth = sw - 16.0F;
			ContentsLeft = (sw - ContentsWidth) * 0.5F;

			// Pre-measure header height by counting hard line breaks.
			int headerLines = int(headerText.split("\n").length);
			float headerH = float(headerLines) * 20.0F + 12.0F;

			float rowH = 24.0F;
			float bottomPad = 50.0F;   // for OK button
			float gap = 16.0F;          // gap between header and table

			float tableH = HeaderRowH + float(rows.length) * rowH;
			ContentsHeight = headerH + gap + tableH + bottomPad;

			float maxH = sh - 80.0F;
			if (ContentsHeight > maxH)
				ContentsHeight = maxH;
			ContentsTop = (sh - ContentsHeight) * 0.5F;

			// Background panel.
			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				label.Bounds = AABB2(0.0F, ContentsTop - 13.0F, Size.x, ContentsHeight + 27.0F);
				AddChild(label);
			}

			// Header — TextViewer so longer prose wraps cleanly.
			{
				spades::ui::TextViewer viewer(Manager);
				viewer.Bounds = AABB2(ContentsLeft, ContentsTop, ContentsWidth, headerH);
				viewer.Text = headerText;
				AddChild(viewer);
			}

			// Column geometry.
			ColNameW = ContentsWidth * 0.56F;
			ColClientW = ContentsWidth * 0.22F;
			ColServerW = ContentsWidth - ColNameW - ColClientW;
			ColNameX = ContentsLeft;
			ColClientX = ColNameX + ColNameW;
			ColServerX = ColClientX + ColClientW;

			TableTop = ContentsTop + headerH + gap;
			TableBottom = TableTop + tableH;

			// Table header labels.
			AddHeaderLabel(_Tr("DisconnectScreen", "Extension"),
			               ColNameX + 6.0F, TableTop, ColNameW - 6.0F, HeaderRowH,
			               Vector2(0.0F, 0.5F));
			AddHeaderLabel(_Tr("DisconnectScreen", "Client"),
			               ColClientX, TableTop, ColClientW, HeaderRowH,
			               Vector2(0.5F, 0.5F));
			AddHeaderLabel(_Tr("DisconnectScreen", "Server"),
			               ColServerX, TableTop, ColServerW, HeaderRowH,
			               Vector2(0.5F, 0.5F));

			// Rows.
			Vector4 nameColor = Vector4(0.95F, 0.95F, 0.95F, 1.0F);
			Vector4 unknownColor = Vector4(0.95F, 0.95F, 0.95F, 0.55F);
			for (uint i = 0; i < rows.length; i++) {
				ExtensionRow@ row = rows[i];
				float y = TableTop + HeaderRowH + float(i) * rowH;

				string idText = "id " + ToString(row.id);
				string nameText;
				Vector4 thisNameColor;
				if (row.name.length > 0) {
					nameText = row.name + "   (" + idText + ")";
					thisNameColor = nameColor;
				} else {
					nameText = _Tr("DisconnectScreen", "Unknown") + "   (" + idText + ")";
					thisNameColor = unknownColor;
				}
				AddCellLabel(nameText, ColNameX + 6.0F, y, ColNameW - 6.0F, rowH,
				             Vector2(0.0F, 0.5F), thisNameColor);
				AddCellLabel(VersionCellText(row.clientVersion),
				             ColClientX, y, ColClientW, rowH,
				             Vector2(0.5F, 0.5F), VersionCellColor(row.clientVersion));
				AddCellLabel(VersionCellText(row.serverVersion),
				             ColServerX, y, ColServerW, rowH,
				             Vector2(0.5F, 0.5F), VersionCellColor(row.serverVersion));
			}

			// OK button.
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MessageBox", "OK");
				button.Bounds = AABB2(ContentsLeft + ContentsWidth - 150.0F,
				                      ContentsTop + ContentsHeight - 30.0F,
				                      150.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnOk);
				AddChild(button);
			}
		}

		private string VersionCellText(int ver) {
			if (ver < 0)
				return "\xe2\x9c\x97";  // ✗
			return "\xe2\x9c\x93 v" + ToString(ver);  // ✓ v<n>
		}

		private Vector4 VersionCellColor(int ver) {
			if (ver < 0)
				return Vector4(0.95F, 0.55F, 0.55F, 1.0F);
			return Vector4(0.65F, 0.95F, 0.65F, 1.0F);
		}

		private void AddHeaderLabel(string text, float x, float y, float w, float h,
		                            Vector2 alignment) {
			spades::ui::Label label(Manager);
			label.Text = text;
			label.Bounds = AABB2(x, y, w, h);
			label.Alignment = alignment;
			label.TextColor = Vector4(0.72F, 0.88F, 1.0F, 1.0F);
			AddChild(label);
		}

		private void AddCellLabel(string text, float x, float y, float w, float h,
		                          Vector2 alignment, Vector4 color) {
			spades::ui::Label label(Manager);
			label.Text = text;
			label.Bounds = AABB2(x, y, w, h);
			label.Alignment = alignment;
			label.TextColor = color;
			AddChild(label);
		}

		void Close() {
			owner.Enable = true;
			owner.Parent.RemoveChild(this);
		}

		void Run() {
			owner.Enable = false;
			owner.Parent.AddChild(this);
		}

		void HotKey(string key) {
			if (IsEnabled and (key == "Enter" or key == "Escape"))
				Close();
			else
				UIElement::HotKey(key);
		}

		private void OnOk(spades::ui::UIElement@ sender) { Close(); }

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);
			r.DrawImage(null, AABB2(pos.x, pos.y + ContentsTop - 14.0F, size.x, 1.0F));
			r.DrawImage(null, AABB2(pos.x, pos.y + ContentsTop + ContentsHeight + 14.0F, size.x, 1.0F));

			// Separator under the table header.
			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.18F);
			r.DrawImage(null, AABB2(pos.x + ContentsLeft,
			                        pos.y + TableTop + HeaderRowH,
			                        ContentsWidth, 1.0F));

			UIElement::Render();
		}
	}

	// Minimal signed-decimal parser. Stops at the first non-digit; returns 0
	// for empty input. Accepts a leading '-'.
	int ParseSignedDecimal(const string&in s) {
		int len = int(s.length);
		if (len == 0)
			return 0;
		int i = 0;
		int sign = 1;
		if (s[0] == 0x2d) { // '-'
			sign = -1;
			i = 1;
		}
		int val = 0;
		for (; i < len; i++) {
			int c = int(s[i]);
			if (c < 0x30 or c > 0x39)
				break;
			val = val * 10 + (c - 0x30);
		}
		return sign * val;
	}

	class ExtensionTableParseResult {
		string prose;
		ExtensionRow@[]@ rows; // null if no table block was present
	}

	// Parses the structured handshake block emitted by the C++ side.
	// `result.rows` is null when no block is present; `result.prose` is then
	// the original text. Otherwise `prose` holds everything before the block.
	ExtensionTableParseResult@ ParseExtensionTable(const string&in text) {
		ExtensionTableParseResult result;
		string startMarker = "\n<<EXTHS>>\n";
		string endMarker = "<<EXTHS_END>>";

		int start = text.findFirst(startMarker);
		if (start < 0) {
			result.prose = text;
			return result;
		}
		int payloadStart = start + int(startMarker.length);
		int end = text.findFirst(endMarker, payloadStart);
		if (end < 0) {
			result.prose = text;
			return result;
		}

		result.prose = text.substr(0, start);
		string payload = text.substr(payloadStart, end - payloadStart);

		ExtensionRow@[] rows;
		string[]@ lines = payload.split("\n");
		for (uint i = 0; i < lines.length; i++) {
			string line = lines[i];
			if (line.length == 0)
				continue;
			string[]@ fields = line.split("|");
			if (fields.length < 4)
				continue;
			int id = ParseSignedDecimal(fields[0]);
			string name = fields[1];
			int clientVer = ParseSignedDecimal(fields[2]);
			int serverVer = ParseSignedDecimal(fields[3]);
			rows.insertLast(ExtensionRow(id, name, clientVer, serverVer));
		}

		if (rows.length > 0)
			@result.rows = rows;
		return result;
	}
}
