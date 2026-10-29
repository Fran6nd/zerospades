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
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>
#include <Gui/UI/Widgets/NumberEntry.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * An immediate-mode column of labelled number fields: a properties
		 * view.
		 *
		 * The host hands over its rows every frame, each a caption over a row
		 * of number fields (X, Y, Z), optionally with a toggle beside the
		 * caption. A field is dragged sideways to scrub its value, or clicked
		 * to type one, as in Blender.
		 *
		 * Values are reported, never kept: `OnNumberChanged` says what a field
		 * is being set to while the gesture is on (`committed` false) and once
		 * more when it ends (`committed` true), so the host can show a value
		 * live and record only the one that was meant. A gesture that is
		 * abandoned reports the value it started from as settled, which is
		 * what puts a host that has been previewing back.
		 *
		 * The content belongs to a `subject` (what the fields describe). A
		 * gesture belongs to the subject it started on and is dropped, not
		 * reported, when the host moves to another: its values would land on
		 * something they were never about.
		 */
		class PropertyGrid {
		public:
			struct Field {
				std::string id; // reported with its value
				std::string axis; // a letter drawn inside the box
				Vector4 axisColor = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);
				float value = 0.0F;
				float step = 1.0F; // what one scrub step or arrow key adds
				int decimals = 2;
				bool enabled = true;
			};
			/** A two-state button on the right of a row's caption. */
			struct Toggle {
				std::string id; // empty: the row has none
				std::string label;
				bool on = false;
			};
			struct Row {
				std::string caption;
				std::vector<Field> fields; // none: a section heading
				Toggle toggle;             // beside the caption, or the heading
			};

			explicit PropertyGrid(client::IAudioDevice* audioDevice = nullptr);

			void SetContent(std::uint64_t subject, std::vector<Row> rows);
			void SetBounds(const AABB2& bounds);
			bool Contains(const Vector2& p) const;

			// --- Pointer ------------------------------------------------------
			/** A press at `p`; false when it is not over the grid. */
			bool MouseDown(const Vector2& p);
			void MouseMove(const Vector2& p);
			void MouseUp(const Vector2& p);
			/** Whether a press on a field is still held (a scrub, or about to type). */
			bool IsPressed() const { return gesture == Gesture::Pressed || gesture == Gesture::Scrubbing; }

			// --- Typing -------------------------------------------------------
			bool IsEditing() const { return gesture == Gesture::Typing; }
			/**
			 * Takes a key while typing or scrubbing (Escape abandons either);
			 * false if it means nothing here.
			 */
			bool KeyEvent(const std::string& key);
			void TextInputEvent(const std::string& text);
			/** The box being typed into, for the IME; empty when none is. */
			AABB2 EditingRect() const;
			/**
			 * Ends whatever gesture is on. `commit` settles on the value it
			 * reached; otherwise the field goes back to where it started.
			 */
			void EndGesture(bool commit);

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			          const Vector2& cursorPos, bool menuActive);

			std::function<void(const std::string& id, float value, bool committed)> OnNumberChanged;
			std::function<void(const std::string& id)> OnToggled;

		private:
			enum class Gesture { None, Pressed, Scrubbing, Typing };
			// Where a field went, found by its row and place in the row.
			struct FieldPlace {
				std::size_t row, field;
				AABB2 box;
			};
			struct TogglePlace {
				std::size_t row;
				AABB2 box;
			};

			ui::FeedbackSounds sounds;
			std::uint64_t subject = 0;
			std::vector<Row> rows;
			AABB2 bounds = AABB2(0.0F, 0.0F, 0.0F, 0.0F);
			// Laid out whenever the rows or the bounds change, so a press is
			// tested against the same boxes that are drawn.
			std::vector<FieldPlace> fieldPlaces;
			std::vector<TogglePlace> togglePlaces;
			std::vector<float> captionY; // parallel to `rows`

			Gesture gesture = Gesture::None;
			std::string activeId; // the field the gesture is on
			float startValue = 0.0F;
			float lastValue = 0.0F; // the last one reported
			Vector2 pressAt = MakeVector2(0.0F, 0.0F);
			NumberEntry entry;
			// Nothing has been typed into the box yet: while so, it follows a
			// value the host changes under it (a locked scale settling as Tab
			// moves on to the next factor).
			bool untouched = false;

			void Layout();
			/** The field with `id`, or null when the rows no longer hold it. */
			const Field* FindField(const std::string& id) const;
			const FieldPlace* PlaceOf(const std::string& id) const;
			void Report(float value, bool committed);
			void BeginTyping(const Field& field);
			/** Moves typing to the next field along (Tab), settling this one. */
			void TypeNextField(int direction);
			void DrawField(client::IRenderer& renderer, client::IFont& font, const Field& field,
			               const AABB2& box, bool hover);
		};
	} // namespace gui
} // namespace spades
