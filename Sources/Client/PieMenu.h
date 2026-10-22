/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace spades {
	namespace client {

		/**
		 * The content of a pie menu, in the shape the *Pie Menu* protocol extension
		 * (extension id 0x31, packet id 0x71) puts on the wire.
		 *
		 * The client's own rings are written in this same shape, so a menu the server
		 * sends and the one the client ships with are drawn, browsed and sent by the
		 * same code, and differ only in where they came from.
		 */

		constexpr int kPieSliceCount = 6;

		/** What choosing a slice sends. Values are the wire values; an unknown one is
		 * read as `None`. The action is also what a slice is styled by. */
		enum class PieAction : std::uint8_t {
			None = 0,
			Chat = 1,
			Ping = 2,
			Command = 3,
		};

		/** What the crosshair is on when the menu opens, as the bits of a pie's
		 * Contexts mask. A spectator only ever opens on `PieContextSpectator`. */
		enum PieContext : std::uint8_t {
			PieContextWorld = 1 << 0,
			PieContextTeammate = 1 << 1,
			PieContextEnemy = 1 << 2,
			PieContextSpectator = 1 << 3,
		};

		struct PieSlice {
			PieAction action = PieAction::None;
			/** Sent on global chat rather than team chat. Means nothing for a
			 * `Command`, which always goes to the team. */
			bool global = false;
			/** What it sends, before substitution. */
			std::string text;
			/** What it draws. Empty means the Text is drawn. */
			std::string label;

			const std::string& DisplayLabel() const { return label.empty() ? text : label; }
		};

		struct Pie {
			std::uint8_t contexts = 0;
			/** The centre caption. Empty means none. */
			std::string theme;
			/** Slice 0 is at the top and the rest follow clockwise. */
			std::array<PieSlice, kPieSliceCount> slices;
		};

		struct PieMenu {
			std::vector<Pie> pies;
			/** Lets the slices use `%n`, which only the client's own rings may: the
			 * protocol leaves every token but `%p` and `%%` reserved. */
			bool clientTokens = false;

			/** The pies offered in a context, in the order they were defined. */
			std::vector<Pie> PiesFor(PieContext) const;
		};

		/**
		 * Expands a slice's Text, once and without re-scanning the result.
		 *
		 * `%p` becomes the target as a command's player argument, `#` and its id, or
		 * nothing when there is no target. `%%` becomes `%`. With `clientTokens`, `%n`
		 * becomes the target's name. Any other `%` is left as it is.
		 */
		std::string ExpandPieText(const std::string& text, int targetPlayerId,
								  const std::string& targetName, bool clientTokens);

		/** The rings the client offers until a server sends its own. */
		PieMenu BuiltInPieMenu();
	} // namespace client
} // namespace spades
