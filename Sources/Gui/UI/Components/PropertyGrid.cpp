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

#include "PropertyGrid.h"

#include <algorithm>
#include <cmath>
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
			const float kPad = 8.0F;
			const float kCaptionHeight = 18.0F;
			const float kHeadingHeight = 22.0F;
			const float kFieldHeight = 22.0F;
			const float kFieldGap = 4.0F;
			const float kRowGap = 8.0F;
			const float kToggleWidth = 56.0F;
			const float kTextInset = 5.0F;
			const float kAxisGap = 5.0F;
			// A press that moves this far sideways scrubs rather than types.
			const float kScrubThreshold = 3.0F;
			// How far the cursor travels for one step of a scrub.
			const float kPixelsPerStep = 4.0F;

			const Vector4 kCaptionColor = MakeVector4(0.72F, 0.72F, 0.76F, 1.0F);
			const Vector4 kHeadingColor = MakeVector4(0.95F, 0.95F, 0.97F, 1.0F);
			const Vector4 kRuleColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.08F);
			const Vector4 kValueColor = MakeVector4(0.92F, 0.92F, 0.96F, 1.0F);
			const Vector4 kDisabledColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);

			float RoundTo(float value, int decimals) {
				const float scale = std::pow(10.0F, float(std::max(0, decimals)));
				return std::round(value * scale) / scale;
			}
		} // namespace

		PropertyGrid::PropertyGrid(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void PropertyGrid::SetContent(std::uint64_t newSubject, std::vector<Row> newRows) {
			const bool sameSubject = newSubject == subject;
			subject = newSubject;
			rows = std::move(newRows);
			// A gesture is about one field of one subject; with either gone it
			// has nothing left to report to.
			if (gesture != Gesture::None && (!sameSubject || !FindField(activeId))) {
				gesture = Gesture::None;
				entry.End();
			}
			if (gesture == Gesture::Typing && untouched) {
				const Field* field = FindField(activeId);
				if (field->value != startValue) {
					startValue = lastValue = field->value;
					entry.Begin(field->value, field->decimals);
				}
			}
			Layout();
		}

		void PropertyGrid::SetBounds(const AABB2& b) {
			bounds = b;
			Layout();
		}

		bool PropertyGrid::Contains(const Vector2& p) const {
			return OverlayInRect(p, bounds.GetMinX(), bounds.GetMinY(), bounds.GetWidth(),
			                     bounds.GetHeight());
		}

		void PropertyGrid::Layout() {
			fieldPlaces.clear();
			togglePlaces.clear();
			captionY.assign(rows.size(), 0.0F);
			const float left = bounds.GetMinX() + kPad;
			const float width = std::max(0.0F, bounds.GetWidth() - kPad * 2.0F);
			float y = bounds.GetMinY() + kPad;
			for (std::size_t i = 0; i < rows.size(); i++) {
				const Row& row = rows[i];
				captionY[i] = y;
				if (row.fields.empty()) {
					y += kHeadingHeight;
					continue;
				}
				if (!row.toggle.id.empty())
					togglePlaces.push_back(
					  {i, AABB2(left + width - kToggleWidth, y - 2.0F, kToggleWidth, kCaptionHeight)});
				y += kCaptionHeight;
				const float count = float(row.fields.size());
				const float boxW = std::max(0.0F, (width - kFieldGap * (count - 1.0F)) / count);
				for (std::size_t f = 0; f < row.fields.size(); f++)
					fieldPlaces.push_back(
					  {i, f, AABB2(left + float(f) * (boxW + kFieldGap), y, boxW, kFieldHeight)});
				y += kFieldHeight + kRowGap;
			}
		}

		const PropertyGrid::Field* PropertyGrid::FindField(const std::string& id) const {
			for (const Row& row : rows) {
				for (const Field& field : row.fields) {
					if (field.id == id)
						return &field;
				}
			}
			return nullptr;
		}

		const PropertyGrid::FieldPlace* PropertyGrid::PlaceOf(const std::string& id) const {
			for (const FieldPlace& place : fieldPlaces) {
				if (rows[place.row].fields[place.field].id == id)
					return &place;
			}
			return nullptr;
		}

		void PropertyGrid::Report(float value, bool committed) {
			lastValue = value;
			const std::string id = activeId; // the handler may change the rows
			if (OnNumberChanged)
				OnNumberChanged(id, value, committed);
		}

		bool PropertyGrid::MouseDown(const Vector2& p) {
			if (!Contains(p))
				return false;
			for (const TogglePlace& place : togglePlaces) {
				if (!place.box.Contains(p))
					continue;
				EndGesture(true);
				const std::string id = rows[place.row].toggle.id;
				sounds.Activate();
				if (OnToggled)
					OnToggled(id);
				return true;
			}
			for (const FieldPlace& place : fieldPlaces) {
				const Field& field = rows[place.row].fields[place.field];
				if (!place.box.Contains(p) || !field.enabled)
					continue;
				if (gesture == Gesture::Typing && activeId == field.id)
					return true; // already typing here
				EndGesture(true);
				gesture = Gesture::Pressed;
				activeId = field.id;
				startValue = lastValue = field.value;
				pressAt = p;
				return true;
			}
			// The panel around the fields: a press there settles what was typed.
			EndGesture(true);
			return true;
		}

		void PropertyGrid::MouseMove(const Vector2& p) {
			if (gesture == Gesture::Pressed) {
				if (std::fabs(p.x - pressAt.x) < kScrubThreshold)
					return;
				gesture = Gesture::Scrubbing;
			}
			if (gesture != Gesture::Scrubbing)
				return;
			const Field* field = FindField(activeId);
			if (!field) {
				gesture = Gesture::None;
				return;
			}
			// Measured from where the press began, never accumulated, so the
			// value under the cursor always matches where the cursor is.
			const float steps = std::round((p.x - pressAt.x) / kPixelsPerStep);
			const float value = RoundTo(startValue + steps * field->step, field->decimals);
			if (value != lastValue)
				Report(value, false);
		}

		void PropertyGrid::MouseUp(const Vector2& p) {
			if (gesture == Gesture::Pressed) {
				// A click without a drag types into the field.
				if (const Field* field = FindField(activeId)) {
					sounds.Activate();
					BeginTyping(*field);
				} else {
					gesture = Gesture::None;
				}
				return;
			}
			if (gesture == Gesture::Scrubbing) {
				MouseMove(p);
				EndGesture(true);
			}
		}

		void PropertyGrid::BeginTyping(const Field& field) {
			gesture = Gesture::Typing;
			activeId = field.id;
			startValue = lastValue = field.value;
			entry.Begin(field.value, field.decimals);
			untouched = true;
		}

		void PropertyGrid::EndGesture(bool commit) {
			switch (gesture) {
				case Gesture::None: return;
				case Gesture::Pressed:
					// Nothing was reported yet, so nothing needs settling.
					gesture = Gesture::None;
					return;
				case Gesture::Scrubbing:
					gesture = Gesture::None;
					Report(commit ? lastValue : startValue, true);
					return;
				case Gesture::Typing: {
					float typed = 0.0F;
					// Text that is not a number is no value to settle on.
					const bool settle = commit && entry.Value(typed);
					entry.End();
					gesture = Gesture::None;
					Report(settle ? typed : startValue, true);
					return;
				}
			}
		}

		void PropertyGrid::TypeNextField(int direction) {
			if (fieldPlaces.empty())
				return;
			int at = 0;
			for (int i = 0; i < int(fieldPlaces.size()); i++) {
				const FieldPlace& place = fieldPlaces[std::size_t(i)];
				if (rows[place.row].fields[place.field].id == activeId)
					at = i;
			}
			EndGesture(true);
			// The report may have changed nothing here (the rows come back on
			// the next frame), so the places found above still hold.
			const int count = int(fieldPlaces.size());
			for (int n = 1; n <= count; n++) {
				const int next = ((at + direction * n) % count + count) % count;
				const FieldPlace& place = fieldPlaces[std::size_t(next)];
				const Field& field = rows[place.row].fields[place.field];
				if (field.enabled) {
					BeginTyping(field);
					return;
				}
			}
		}

		bool PropertyGrid::KeyEvent(const std::string& key) {
			if (gesture == Gesture::Pressed || gesture == Gesture::Scrubbing) {
				if (key != "Escape")
					return false;
				EndGesture(false);
				return true;
			}
			if (gesture != Gesture::Typing)
				return false;
			const Field* field = FindField(activeId);
			if (!field) {
				gesture = Gesture::None;
				entry.End();
				return false;
			}
			if (key == "Enter") {
				EndGesture(true);
				return true;
			}
			if (key == "Escape") {
				EndGesture(false);
				return true;
			}
			if (key == "Tab") {
				TypeNextField(1);
				return true;
			}
			untouched = false;
			if (key == "Up" || key == "Down") {
				float from = startValue;
				entry.Value(from);
				const float stepped =
				  RoundTo(from + (key == "Up" ? field->step : -field->step), field->decimals);
				entry.Begin(stepped, field->decimals);
				Report(stepped, false);
				return true;
			}
			if (key == "BackSpace")
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
			else
				return true; // swallowed, so no shortcut fires on typed text
			float value;
			if (entry.Value(value))
				Report(value, false);
			return true;
		}

		void PropertyGrid::TextInputEvent(const std::string& text) {
			if (gesture != Gesture::Typing)
				return;
			untouched = false;
			entry.Insert(text);
			float value;
			if (entry.Value(value))
				Report(value, false);
		}

		AABB2 PropertyGrid::EditingRect() const {
			if (gesture == Gesture::Typing) {
				if (const FieldPlace* place = PlaceOf(activeId))
					return place->box;
			}
			return AABB2(0.0F, 0.0F, 0.0F, 0.0F);
		}

		void PropertyGrid::DrawField(client::IRenderer& renderer, client::IFont& font,
		                             const Field& field, const AABB2& box, bool hover) {
			const bool mine = gesture != Gesture::None && activeId == field.id;
			const bool typing = mine && gesture == Gesture::Typing;
			widgets::PaintField(renderer, MakeVector2(box.GetMinX(), box.GetMinY()),
			                    MakeVector2(box.GetWidth(), box.GetHeight()),
			                    mine && gesture != Gesture::Pressed, hover && field.enabled);

			const Vector2 axisSize = font.Measure(field.axis);
			const float textY = box.GetMinY() + (box.GetHeight() - axisSize.y) * 0.5F;
			font.Draw(field.axis, MakeVector2(box.GetMinX() + kTextInset, textY), 1.0F,
			          field.enabled ? field.axisColor : kDisabledColor);

			const float valueX = box.GetMinX() + kTextInset + axisSize.x + kAxisGap;
			const float room = std::max(0.0F, box.GetMaxX() - kTextInset - valueX);
			const std::string text =
			  typing ? entry.Text() : NumberEntry::Format(field.value, field.decimals);
			const std::string shown = ui::ElideText(font, text, room);
			font.Draw(shown, MakeVector2(valueX, textY), 1.0F,
			          field.enabled ? kValueColor : kDisabledColor);
			if (typing) {
				const float caretX = valueX + std::min(room, font.Measure(text.substr(
				                                         0, std::size_t(entry.Caret())))
				                                         .x);
				OverlayColorNP(renderer, kValueColor);
				OverlayFillRect(renderer, caretX, box.GetMinY() + 4.0F, 1.0F,
				                box.GetHeight() - 8.0F);
			}
		}

		void PropertyGrid::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                        const Vector2& cursorPos, bool menuActive) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const float left = bounds.GetMinX() + kPad;
			const float width = std::max(0.0F, bounds.GetWidth() - kPad * 2.0F);
			const bool pointerFree = !menuActive && gesture != Gesture::Scrubbing;

			for (std::size_t i = 0; i < rows.size(); i++) {
				const Row& row = rows[i];
				const float y = captionY[i];
				if (row.fields.empty()) {
					const std::string text = ui::ElideText(font, row.caption, width);
					font.Draw(text, MakeVector2(left, y + 2.0F), 1.0F, kHeadingColor);
					OverlayColorNP(renderer, kRuleColor);
					OverlayFillRect(renderer, left, y + kHeadingHeight - 4.0F, width, 1.0F);
					continue;
				}
				const float captionRoom = row.toggle.id.empty() ? width : width - kToggleWidth - kPad;
				font.Draw(ui::ElideText(font, row.caption, captionRoom), MakeVector2(left, y), 1.0F,
				          kCaptionColor);
			}

			for (const TogglePlace& place : togglePlaces) {
				const Toggle& toggle = rows[place.row].toggle;
				const bool hover = pointerFree && place.box.Contains(cursorPos);
				widgets::PaintButton(renderer, font, MakeVector2(place.box.GetMinX(), place.box.GetMinY()),
				                     MakeVector2(place.box.GetWidth(), place.box.GetHeight()),
				                     toggle.label, MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), true, hover, false, toggle.on, 1.0F);
			}

			for (const FieldPlace& place : fieldPlaces) {
				const Field& field = rows[place.row].fields[place.field];
				DrawField(renderer, font, field, place.box,
				          pointerFree && place.box.Contains(cursorPos));
			}
		}
	} // namespace gui
} // namespace spades
