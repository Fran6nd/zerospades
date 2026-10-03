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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Gui/Utils/PingTester.h>

namespace spades {
	/** A server that answered a LAN discovery request. */
	struct LanDiscoveryEntry {
		/** Connectable address */
		std::string address;

		/** Rough round-trip time in milliseconds (measured at frame granularity). */
		int ping = 0;

		std::uint16_t port = 0;

		PingTesterServerInfo info;
	};

	/**
	 * Finds servers on the local network, this machine included, by sending HELLOLAN
	 * datagrams to the broadcast address, on every port of the range set by
	 * `cl_lanPortMin` and `cl_lanPortMax`.
	 *
	 * Replies are accepted from any address, so servers do not need to be known in
	 * advance. Everything runs on the calling thread: create it, call `Scan()` and
	 * then `Update()` periodically.
	 */
	class LanDiscovery {
		struct Private;
		std::unique_ptr<Private> priv;

	public:
		LanDiscovery();
		~LanDiscovery();

		/** Sends a new discovery request. Servers that already answered are kept. */
		void Scan();

		/** Reads the pending replies. Call this periodically from the main thread. */
		void Update();

		/** Servers found so far, in the order they answered. */
		const std::vector<LanDiscoveryEntry>& GetServers() const;
	};
}
