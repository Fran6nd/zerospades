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

// Client Quirks negotiation. A quirk is a single, independently negotiated
// 2-bit field describing one difference in protocol/gameplay/presentation
// handling from the Voxlap baseline. Both peers exchange the same packet: a
// packet ID followed by zero or more bytes, four quirk fields per byte
// (least-significant pair first). Fields for quirks a peer does not know are
// ignored, and a packet can never be malformed.
//
// The client keeps two things: the fixed declaration it sends (`QuirkArray`),
// and a running enabledness state (`QuirkEnabled`) seeded from its own defaults.
// The server may only flip a quirk's enabledness if the client declared it
// `Either`; a fixed On/Off declaration is authoritative and cannot be changed.

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spades {
	namespace client {

		// Wire position of each quirk. The index IS the wire position, so entries
		// are only ever appended, never renumbered. This list is provisional and
		// mirrors the reference registry in the aosprotocol spec.
		enum Quirk {
			QuirkInFloor = 0,
			QuirkNoShortPlayer,
			QuirkScrewedDisconnectData,
			QuirkOsCp437,
			QuirkUtf8,
			QuirkAscii,
			QuirkOsBactionCull,
			QuirkUtf8ColorImg,
			QuirkInsky,

			QuirkLength
		};

		// The value carried by each 2-bit field. Value 1 means "Either" when a
		// client sends it and "Reserved" when a server sends it.
		enum QuirkValue : std::uint8_t {
			QuirkUnspecified = 0,
			QuirkEither = 1,   // client direction
			QuirkReserved = 1, // server direction
			QuirkOff = 2,
			QuirkOn = 3,
		};

		// A client's fixed declarations, indexed by Quirk.
		using QuirkArray = std::array<std::uint8_t, QuirkLength>;
		// The running enabledness state, indexed by Quirk.
		using QuirkEnabled = std::bitset<QuirkLength>;

		/// Encode the quirk fields into a packet payload (the quirk bytes only,
		/// without any packet ID). Trailing all-unspecified bytes are chopped off,
		/// so an all-unspecified array yields an empty payload.
		std::vector<std::uint8_t> EncodeQuirks(const QuirkArray& quirks);

		/// Apply a full server quirk packet payload (quirk bytes only, no packet
		/// ID) to `enabled`. For every field the server sends as On/Off, the
		/// matching quirk's enabledness is updated only if the client declared that
		/// quirk `Either`; all other quirks are left untouched. Never reads past
		/// `len` and ignores fields for quirk indices past `QuirkLength`.
		void ApplyServerQuirks(QuirkEnabled& enabled, const QuirkArray& localQuirks,
		                       const std::uint8_t* data, std::size_t len);

		/// Apply one `QuirksOff` entry (a byte offset into the quirk array and a
		/// quirk byte holding up to four 2-bit fields) to `enabled`, with the same
		/// Either-only rule as ApplyServerQuirks.
		void ApplyServerQuirksOff(QuirkEnabled& enabled, const QuirkArray& localQuirks,
		                          std::uint8_t offset, std::uint8_t quirkByte);

		/// Human-readable name of a quirk index (e.g. "Utf8"), or "?" if out of
		/// range. Used for console logging.
		const char* QuirkName(unsigned quirk);
		/// Human-readable name of a raw client field value ("Unspecified", "Either",
		/// "Off", "On").
		const char* QuirkValueName(std::uint8_t field);

	} // namespace client
} // namespace spades
