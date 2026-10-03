/*
 Copyright (c) 2026 ZeroSpades developers.

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

#pragma once

#include <cstddef>
#include <cstdint>

namespace spades {
	/**
	 * Constants of the HELLO / HELLOLAN datagram protocol, shared by `PingTester` (known
	 * addresses) and `LanDiscovery` (unknown addresses).
	 */
	namespace hello {
		/** Plain ping request. The server answers "HI" and nothing else. */
		constexpr char kPingRequest[] = "HELLO";
		constexpr std::size_t kPingRequestLength = sizeof(kPingRequest) - 1;

		/**
		 * Info request. The server answers a JSON object (name, map, game mode, players,
		 * extensions), which doubles as a ping reply.
		 */
		constexpr char kInfoRequest[] = "HELLOLAN";
		constexpr std::size_t kInfoRequestLength = sizeof(kInfoRequest) - 1;

		/** Large enough for a HELLOLAN reply (a typical one is about 180 bytes). */
		constexpr std::size_t kMaxPacketSize = 1024;
	} // namespace hello
} // namespace spades
