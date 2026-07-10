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

#pragma once

#include <array>
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

		// Effective state of a quirk after resolving a client field against a
		// server field (see ResolveQuirk).
		enum QuirkState {
			QuirkStateOff = 0,        // effectively off
			QuirkStateOn,             // effectively on
			QuirkStateHeuristic,      // client Unspecified: server decides on its side
			QuirkStateClientDefault,  // client Either, server has not pinned it
		};

		using QuirkArray = std::array<std::uint8_t, QuirkLength>;

		/// Encode the quirk fields into a packet payload (the quirk bytes only,
		/// without any packet ID). Trailing all-unspecified bytes are chopped off,
		/// so an all-unspecified array yields an empty payload.
		std::vector<std::uint8_t> EncodeQuirks(const QuirkArray& quirks);

		/// Decode a packet payload (quirk bytes only, no packet ID) into `out`.
		/// Every entry is first reset to unspecified; fields for quirk indices past
		/// `QuirkLength` are ignored. Never reads past `len` and never fails.
		void DecodeQuirks(QuirkArray& out, const std::uint8_t* data, std::size_t len);

		/// Resolve one quirk's effective state from the client's field and the
		/// server's field. A server field only moves a quirk the client marked
		/// Either; it never overrides a client Off/On, and for a client Unspecified
		/// the decision is left to the server's own side (QuirkStateHeuristic).
		QuirkState ResolveQuirk(std::uint8_t clientField, std::uint8_t serverField);

		/// Apply one `PacketTypeQuirksOff` entry (a byte offset into the quirk
		/// array and a quirk byte holding up to four 2-bit fields) to an existing
		/// server-field array. Only non-`Unspecified` fields take effect; a `00`
		/// field means "leave unchanged". Out-of-range quirk indices are ignored.
		void ApplyQuirksOffEntry(QuirkArray& serverFields, std::uint8_t offset,
		                         std::uint8_t quirkByte);

		/// Human-readable name of a quirk index (e.g. "Utf8"), or "?" if out of
		/// range. Used for console logging.
		const char* QuirkName(unsigned quirk);
		/// Human-readable name of a raw client field value ("Unspecified", "Either",
		/// "Off", "On").
		const char* QuirkValueName(std::uint8_t field);
		/// Human-readable name of a resolved quirk state.
		const char* QuirkStateName(QuirkState state);

	} // namespace client
} // namespace spades
