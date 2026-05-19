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

	// Renders a single row of the extension table. The column geometry is
	// expressed as fractions of `Size.x` so the same layout matches the
	// column headers drawn statically by `DisconnectScreen`.
	class ExtensionRowItemUI : spades::ui::UIElement {
		private ExtensionRow@ row;
		private float ColFracName, ColFracClient;

		ExtensionRowItemUI(spades::ui::UIManager@ manager, ExtensionRow@ row,
		                   float colFracName, float colFracClient) {
			super(manager);
			@this.row = row;
			ColFracName = colFracName;
			ColFracClient = colFracClient;
			IsMouseInteractive = false;
		}

		private string VersionCellText(int ver) {
			if (ver < 0)
				return "\xe2\x9c\x97"; // ✗
			return "\xe2\x9c\x93 v" + ToString(ver); // ✓ v<n>
		}

		private Vector4 VersionCellColor(int ver) {
			if (ver < 0)
				return Vector4(0.95F, 0.55F, 0.55F, 1.0F);
			return Vector4(0.65F, 0.95F, 0.65F, 1.0F);
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Font@ font = this.Font;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			float padding = 6.0F;
			float colNameW = size.x * ColFracName;
			float colClientW = size.x * ColFracClient;
			float colServerW = size.x - colNameW - colClientW;
			float colNameX = 0.0F;
			float colClientX = colNameW;
			float colServerX = colNameW + colClientW;

			string nameText;
			Vector4 nameColor;
			if (row.name.length > 0) {
				nameText = row.name + "   (id " + ToString(row.id) + ")";
				nameColor = Vector4(0.95F, 0.95F, 0.95F, 1.0F);
			} else {
				nameText = "Unknown   (id " + ToString(row.id) + ")";
				nameColor = Vector4(0.95F, 0.95F, 0.95F, 0.55F);
			}

			DrawCell(r, font, nameText, pos + Vector2(colNameX + padding, 0.0F),
			         Vector2(colNameW - padding, size.y), 0.0F, nameColor);
			DrawCell(r, font, VersionCellText(row.clientVersion),
			         pos + Vector2(colClientX, 0.0F),
			         Vector2(colClientW, size.y), 0.5F, VersionCellColor(row.clientVersion));
			DrawCell(r, font, VersionCellText(row.serverVersion),
			         pos + Vector2(colServerX, 0.0F),
			         Vector2(colServerW, size.y), 0.5F, VersionCellColor(row.serverVersion));
		}

		private void DrawCell(Renderer@ r, Font@ font, string text, Vector2 cellPos,
		                      Vector2 cellSize, float xAlign, Vector4 color) {
			if (text.length == 0 or font is null)
				return;
			Vector2 textSize = font.Measure(text);
			Vector2 textPos = cellPos + Vector2((cellSize.x - textSize.x) * xAlign,
			                                    (cellSize.y - textSize.y) * 0.5F);
			font.Draw(text, textPos, 1.0F, color);
		}
	}

	class ExtensionTableModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		ExtensionRow@[]@ rows;
		float ColFracName;
		float ColFracClient;

		ExtensionTableModel(spades::ui::UIManager@ manager, ExtensionRow@[]@ rows,
		                    float colFracName, float colFracClient) {
			@this.manager = manager;
			@this.rows = rows;
			ColFracName = colFracName;
			ColFracClient = colFracClient;
		}

		int NumRows {
			get { return int(rows.length); }
		}

		spades::ui::UIElement@ CreateElement(int row) {
			return ExtensionRowItemUI(manager, rows[row], ColFracName, ColFracClient);
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	class DisconnectScreen : spades::ui::UIElement {
		private float ContentsTop, ContentsHeight, ContentsLeft, ContentsWidth;
		private float TableHeaderTop;
		private float HeaderRowH;
		private float ColFracName, ColFracClient;

		private spades::ui::UIElement@ owner;
		private spades::ui::ListView@ tableView;

		DisconnectScreen(spades::ui::UIElement@ owner, string introText, string reasonText,
		                 ExtensionRow@[]@ rows, float height = 240.0F) {
			super(owner.Manager);
			@this.owner = owner;
			@Font = Manager.RootElement.Font;
			this.Bounds = owner.Bounds;

			HeaderRowH = 24.0F;
			ColFracName = 0.56F;
			ColFracClient = 0.22F;

			// Match the original AlertScreen geometry so the popup feels
			// identical, just with extra structure inside.
			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			ContentsWidth = sw - 16.0F;
			if (ContentsWidth > 800.0F)
				ContentsWidth = 800.0F;
			ContentsLeft = (sw - ContentsWidth) * 0.5F;
			ContentsHeight = height;
			ContentsTop = (sh - ContentsHeight) * 0.5F;

			// Vertical layout. The two static labels at the top (intro + the
			// actual kick reason) never scroll, so the reason — the part
			// users actually care about — is always visible.
			float introH = 22.0F;
			float reasonH = 26.0F;
			float gap = 8.0F;
			float bottomGap = 10.0F;
			float buttonH = 30.0F;

			float introY = ContentsTop + 4.0F;
			float reasonY = introY + introH + 2.0F;
			float tableHeaderY = reasonY + reasonH + gap;
			float tableBodyY = tableHeaderY + HeaderRowH;
			float tableBodyH = ContentsHeight - (tableBodyY - ContentsTop) - bottomGap - buttonH - gap;
			if (tableBodyH < HeaderRowH)
				tableBodyH = HeaderRowH;
			TableHeaderTop = tableHeaderY;

			// Background panel.
			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				label.Bounds = AABB2(0.0F, ContentsTop - 13.0F, Size.x, ContentsHeight + 27.0F);
				AddChild(label);
			}

			// Intro line (dim).
			{
				spades::ui::Label label(Manager);
				label.Text = introText;
				label.Bounds = AABB2(ContentsLeft, introY, ContentsWidth, introH);
				label.Alignment = Vector2(0.0F, 0.5F);
				label.TextColor = Vector4(1.0F, 1.0F, 1.0F, 0.7F);
				AddChild(label);
			}

			// Kick reason — the line users come here for. Slightly bigger.
			{
				spades::ui::Label label(Manager);
				label.Text = reasonText;
				label.Bounds = AABB2(ContentsLeft, reasonY, ContentsWidth, reasonH);
				label.Alignment = Vector2(0.0F, 0.5F);
				label.TextColor = Vector4(1.0F, 0.85F, 0.55F, 1.0F);
				label.TextScale = 1.1F;
				AddChild(label);
			}

			// Static column header row (drawn as labels, not part of the list
			// so it doesn't scroll out of view).
			float colNameW = ContentsWidth * ColFracName;
			float colClientW = ContentsWidth * ColFracClient;
			float colServerW = ContentsWidth - colNameW - colClientW;
			float colNameX = ContentsLeft;
			float colClientX = colNameX + colNameW;
			float colServerX = colClientX + colClientW;

			AddHeaderLabel(_Tr("DisconnectScreen", "Extension"),
			               colNameX + 6.0F, tableHeaderY, colNameW - 6.0F, HeaderRowH,
			               Vector2(0.0F, 0.5F));
			AddHeaderLabel(_Tr("DisconnectScreen", "Client"),
			               colClientX, tableHeaderY, colClientW, HeaderRowH,
			               Vector2(0.5F, 0.5F));
			AddHeaderLabel(_Tr("DisconnectScreen", "Server"),
			               colServerX, tableHeaderY, colServerW, HeaderRowH,
			               Vector2(0.5F, 0.5F));

			// Scrollable table body.
			{
				spades::ui::ListView lv(Manager);
				lv.RowHeight = 22.0F;
				AddChild(lv);
				lv.Bounds = AABB2(ContentsLeft, tableBodyY, ContentsWidth, tableBodyH);
				@lv.Model = ExtensionTableModel(Manager, rows, ColFracName, ColFracClient);
				@tableView = lv;
			}

			// OK button.
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MessageBox", "OK");
				button.Bounds = AABB2(ContentsLeft + ContentsWidth - 150.0F,
				                      ContentsTop + ContentsHeight - buttonH,
				                      150.0F, buttonH);
				@button.Activated = spades::ui::EventHandler(this.OnOk);
				AddChild(button);
			}
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
			                        pos.y + TableHeaderTop + HeaderRowH,
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
