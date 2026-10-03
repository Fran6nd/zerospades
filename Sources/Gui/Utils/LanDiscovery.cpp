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

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>

#include <enet/enet.h>

#include "LanDiscovery.h"

#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Gui/Utils/HelloProtocol.h>

// First and last port (inclusive) a LAN scan is sent to. A broadcast can only target known
// ports, so a server listening outside of this range is not discovered.
DEFINE_SPADES_SETTING(cl_lanPortMin, "32882");
DEFINE_SPADES_SETTING(cl_lanPortMax, "32892");

namespace spades {
	namespace {
		using clock = std::chrono::steady_clock;
		using std::chrono::duration_cast;
		using std::chrono::milliseconds;

		/** Upper bound of datagrams handled per `Update()` call. */
		const int g_maxPacketsPerUpdate = 64;

		/** Upper bound of ports scanned per address, so a typo cannot flood the network. */
		const int g_maxScanPorts = 256;

		/** Reads the configured scan range, sanitized: valid ports, ordered, and bounded. */
		void GetScanPortRange(int& first, int& last) {
			first = std::max(1, std::min(65535, static_cast<int>(cl_lanPortMin)));
			last = std::max(1, std::min(65535, static_cast<int>(cl_lanPortMax)));
			if (last < first)
				std::swap(first, last);
			last = std::min(last, first + g_maxScanPorts - 1);
		}
	}

	struct LanDiscovery::Private {
		ENetSocket socket = ENET_SOCKET_NULL;
		clock::time_point lastScan = clock::now();
		std::vector<LanDiscoveryEntry> servers;

		Private() {
			// The main screen runs before any NetClient exists, so make sure ENet
			// (and Winsock on Windows) is initialized before creating the first socket.
			if (enet_initialize() != 0) {
				SPLog("LAN discovery: failed to initialize ENet");
				return;
			}
			socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
			if (socket == ENET_SOCKET_NULL) {
				// Discovery is optional, so a failure must not break the main screen
				SPLog("LAN discovery: failed to create a socket");
				return;
			}
			enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
			enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1);
		}

		~Private() {
			if (socket != ENET_SOCKET_NULL)
				enet_socket_destroy(socket);
		}
	};

	LanDiscovery::LanDiscovery() : priv{new Private()} {}
	LanDiscovery::~LanDiscovery() {}

	void LanDiscovery::Scan() {
		if (priv->socket == ENET_SOCKET_NULL)
			return;

		ENetBuffer buffer;
		buffer.data = const_cast<char*>(hello::kInfoRequest);
		buffer.dataLength = hello::kInfoRequestLength;

		// Ask the whole LAN. A server on this machine receives the broadcast too and answers
		// from its LAN address, so a separate loopback request would make it answer twice.
		ENetAddress target;
		target.host = ENET_HOST_BROADCAST;

		int firstPort, lastPort;
		GetScanPortRange(firstPort, lastPort);

		priv->lastScan = clock::now();

		// A server can listen on any port of the scan range
		for (int port = firstPort; port <= lastPort; ++port) {
			target.port = static_cast<enet_uint16>(port);

			// Failures (e.g. no route for the broadcast) are expected on some networks
			enet_socket_send(priv->socket, &target, &buffer, 1);
		}
	}

	void LanDiscovery::Update() {
		if (priv->socket == ENET_SOCKET_NULL)
			return;

		std::array<char, hello::kMaxPacketSize> data;
		ENetBuffer buffer;
		buffer.data = data.data();
		buffer.dataLength = data.size();

		for (int i = 0; i < g_maxPacketsPerUpdate; ++i) {
			ENetAddress from;
			int length = enet_socket_receive(priv->socket, &from, &buffer, 1);
			if (length == 0)
				break; // nothing left to read
			if (length < 0)
				continue; // e.g. a reset reported for an earlier send

			PingTesterServerInfo info;
			if (!ParseHelloLanReply(std::string(data.data(), static_cast<std::size_t>(length)),
			                        info))
				continue;

			char ip[64];
			if (enet_address_get_host_ip(&from, ip, sizeof(ip)) != 0)
				continue;

			LanDiscoveryEntry entry;
			entry.address = "aos://" + std::string(ip) + ":" + std::to_string(from.port);
			entry.port = from.port;
			entry.info = std::move(info);

			auto elapsed = duration_cast<milliseconds>(clock::now() - priv->lastScan).count();
			entry.ping = std::max(0, static_cast<int>(elapsed));
			priv->servers.push_back(std::move(entry));
		}
	}

	const std::vector<LanDiscoveryEntry>& LanDiscovery::GetServers() const { return priv->servers; }
}
