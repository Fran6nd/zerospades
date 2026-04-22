/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>
#include <string>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class IFont;
		class IRenderer;

		class PieMenuView {
		public:
			enum class Variant { World, Player };
			enum Slice { Top = 0, Right = 1, Bottom = 2, Left = 3, None = -1 };

		private:
			IRenderer& renderer;
			IFont* font;

			bool open = false;
			Variant variant = Variant::World;
			Vector2 cursor = {0.0F, 0.0F};
			int selection = None;

			float openPhase = 0.0F;
			std::array<float, 4> highlight = {0.0F, 0.0F, 0.0F, 0.0F};

			std::array<std::string, 4> worldLabels;
			std::array<std::string, 4> playerLabels;

		public:
			PieMenuView(Client*, IFont* font);
			~PieMenuView();

			void Open(Variant v);
			int Close();

			bool IsOpen() const { return open; }
			Variant GetVariant() const { return variant; }
			int GetSelection() const { return selection; }
			const std::string& GetSelectionLabel() const;

			void HandleMouseDelta(float dx, float dy);
			void Update(float dt);
			void Draw();
		};
	} // namespace client
} // namespace spades
