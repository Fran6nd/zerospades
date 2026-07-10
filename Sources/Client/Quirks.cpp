/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include "Quirks.h"

namespace spades {
	namespace client {

		namespace {
			// Apply one server 2-bit field to a quirk's enabledness. The server may
			// only move a quirk the client declared Either; On enables, Off disables,
			// Unspecified/Reserved leave it unchanged.
			void ApplyServerField(QuirkEnabled& enabled, const QuirkArray& localQuirks,
			                      std::size_t q, std::uint8_t field) {
				if (q >= QuirkLength)
					return;
				if ((localQuirks[q] & 3) != QuirkEither)
					return;

				switch (field & 3) {
					case QuirkOn: enabled.set(q); break;
					case QuirkOff: enabled.reset(q); break;
					default: break; // Unspecified / Reserved: no change
				}
			}
		} // namespace

		std::vector<std::uint8_t> EncodeQuirks(const QuirkArray& quirks) {
			std::vector<std::uint8_t> bytes((QuirkLength + 3) / 4, 0);

			std::size_t used = 0;
			for (std::size_t i = 0; i < QuirkLength; i++) {
				std::uint8_t v = quirks[i] & 3;
				bytes[i / 4] |= static_cast<std::uint8_t>(v << (2 * (i % 4)));

				// Extend the payload only through the last byte carrying a
				// non-unspecified field, chopping trailing all-unspecified bytes.
				if (v != QuirkUnspecified)
					used = i / 4 + 1;
			}

			bytes.resize(used);
			return bytes;
		}

		void ApplyServerQuirks(QuirkEnabled& enabled, const QuirkArray& localQuirks,
		                       const std::uint8_t* data, std::size_t len) {
			// Four quirks per byte. Clamp to the quirks we know so a longer packet's
			// tail is ignored and we never read a field the buffer does not hold.
			std::size_t count = len * 4;
			if (count > QuirkLength)
				count = QuirkLength;

			for (std::size_t i = 0; i < count; i++)
				ApplyServerField(enabled, localQuirks, i, (data[i / 4] >> (2 * (i % 4))) & 3);
		}

		void ApplyServerQuirksOff(QuirkEnabled& enabled, const QuirkArray& localQuirks,
		                          std::uint8_t offset, std::uint8_t quirkByte) {
			for (unsigned j = 0; j < 4; j++) {
				std::size_t q = static_cast<std::size_t>(offset) * 4 + j;
				ApplyServerField(enabled, localQuirks, q, (quirkByte >> (2 * j)) & 3);
			}
		}

		const char* QuirkName(unsigned quirk) {
			switch (quirk) {
				case QuirkInFloor: return "InFloor";
				case QuirkNoShortPlayer: return "NoShortPlayer";
				case QuirkScrewedDisconnectData: return "ScrewedDisconnectData";
				case QuirkOsCp437: return "OsCp437";
				case QuirkUtf8: return "Utf8";
				case QuirkAscii: return "Ascii";
				case QuirkOsBactionCull: return "OsBactionCull";
				case QuirkUtf8ColorImg: return "Utf8ColorImg";
				case QuirkInsky: return "Insky";
				default: return "?";
			}
		}

		const char* QuirkValueName(std::uint8_t field) {
			switch (field & 3) {
				case QuirkUnspecified: return "Unspecified";
				case QuirkEither: return "Either";
				case QuirkOff: return "Off";
				case QuirkOn: return "On";
				default: return "?";
			}
		}

	} // namespace client
} // namespace spades
