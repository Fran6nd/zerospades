/*
 Copyright (c) 2017 yvt

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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <list>
#include <queue>
#include <unordered_map>

#include <enet/enet.h>
#include <json/json.h>

#include "HelloProtocol.h"
#include "PingTester.h"

#include <Core/Debug.h>
#include <Core/ENetUtils.h>
#include <Core/ServerAddress.h>
#include <Core/Settings.h>
#include <Core/Thread.h>

DEFINE_SPADES_SETTING(cl_debugServerPing, "0");

namespace spades {
	namespace {
		using clock = std::chrono::steady_clock;
		using std::chrono::duration_cast;
		using std::chrono::milliseconds;

		struct SafeENetSocket {
			const ENetSocket handle;
			SafeENetSocket(ENetSocket handle) : handle{handle} {
				if (handle == -1)
					SPRaise("Failed to create a socket.");
			}
			SafeENetSocket(const SafeENetSocket&) = delete;
			void operator=(const SafeENetSocket&) = delete;
			~SafeENetSocket() { enet_socket_destroy(handle); }
		};

		struct ResultPacket {
			ENetAddress address;
			clock::time_point receiveTime;
			std::string payload;

			// Singly-linked list
			std::unique_ptr<ResultPacket> next;
		};

		/**
		 * Structure shared by both of the main thread and the measurement thread.
		 *
		 * Contains immutable information shared by both threads as well as being used
		 * to pass the measurement result. (The measurement thread cannot access
		 * `results` directly)
		 */
		struct Shared {
			SafeENetSocket socket{enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM)};

			/**
			 * ENet's socket does not provide `shutdown()`, so instead we
			 * poll this variable
			 */
			std::atomic<bool> shutdown{false};

			/**
			 * Is `MeasureThread` ready to receive packets?
			 *
			 * Do not send a ping packet when this is `false`! If you do so, the retrieval time
			 * would be inaccurate and the result would be bunk!
			 */
			std::atomic<bool> ready{false};

			/** The head of a singly-linked list */
			stmp::atomic_unique_ptr<ResultPacket> firstResultPacket;

			Shared() { enet_socket_set_option(socket.handle, ENET_SOCKOPT_NONBLOCK, 1); }
		};

		/**
		 * The measurement thread handles the incoming packets, records their receival time,
		 * and sends them to the main thread (via `Shared::firstResultPacket`).
		 *
		 * (Outgoing packets are sent by `Update()` in the main thread.)
		 */
		struct MeasureThread : public Thread {
			std::shared_ptr<Shared> shared;

			MeasureThread(const std::shared_ptr<Shared>& shared) : shared{shared} {}

			void Run() override {
				SPADES_MARK_FUNCTION();

				std::array<char, hello::kMaxPacketSize> buffer;
				ENetBuffer enetBuffer;
				enetBuffer.data = buffer.data();
				enetBuffer.dataLength = buffer.size();

				std::unique_ptr<ResultPacket> resultPacket{new ResultPacket()};
				
				bool debugPing = cl_debugServerPing;
				if (debugPing)
					SPLog("Measurement thread started.");

				shared->ready.store(true);

				// Wait for incoming packets while polling the value of `shutdown`
				while (!shared->shutdown.load()) {
					enet_uint32 cond = ENET_SOCKET_WAIT_RECEIVE | ENET_SOCKET_WAIT_INTERRUPT;
					int result = enet_socket_wait(shared->socket.handle, &cond, 200);
					if (result < 0)
						SPRaise("enet_socket_wait failed");

					// A signal (EINTR) interrupted the wait: ENet reports it through
					// `ENET_SOCKET_WAIT_INTERRUPT` instead of failing, so just poll again.
					if (!(cond & ENET_SOCKET_WAIT_RECEIVE))
						continue;
						
					while (true) {
						ENetAddress address;

						result =
						  enet_socket_receive(shared->socket.handle, &address, &enetBuffer, 1);
						if (result < 0) {
							// Maybe too long message etc. Just drop the packet.
							continue;
						}
						if (result == 0) {
							// No more packets in the receive queue
							break;
						}

						// Create and insert a result packet
						resultPacket->address = address;
						resultPacket->receiveTime = clock::now();
						resultPacket->payload.assign(buffer.data(),
						                             static_cast<std::size_t>(result));
						resultPacket->next = shared->firstResultPacket.take();
						shared->firstResultPacket.store(std::move(resultPacket));

						// Allocate the next result packet
						resultPacket.reset(new ResultPacket());
					}
				}

				shared->ready.store(false);

				if (debugPing)
					SPLog("Measurement thread closed.");
			}
		};

		struct QueueItem {
			/**
			 * This field has a different meaning depending on where this is stored:
			 *
			 *  - `queue`: The measurement should take place after this time point, not before.
			 *  - `ongoing`: The ongoing packet was sent at this time point.
			 */
			clock::time_point time;

			/** When this measurement is regarded as timed-out? (Only valid in `ongoing`) */
			clock::time_point deadline;

			ENetAddress address;

			clock::duration retryBackoff;

			int retriesRemaining;

			// `std::priority_queue` is max-heap, so flip the order
			bool operator<(const QueueItem& o) const { return time > o.time; }
		};

		const std::size_t g_maxSimultaneousMeasurement = 16;
		const clock::duration g_initialRetryBackoff =
		  duration_cast<clock::duration>(milliseconds(100));
		const clock::duration g_maximumRetryBackoff =
		  duration_cast<clock::duration>(milliseconds(10000));
		const clock::duration g_timeout = duration_cast<clock::duration>(milliseconds(2000));
		const int g_retryCount = 10;

		/** Removes control characters and truncates without splitting a UTF-8 character. */
		std::string SanitizeText(const std::string& s, std::size_t maxLength) {
			std::string out;
			out.reserve(std::min(s.size(), maxLength));
			for (unsigned char c : s) {
				if (c < 0x20 || c == 0x7F)
					continue; // drop control characters
				out += static_cast<char>(c);
			}
			if (out.size() > maxLength) {
				std::size_t cut = maxLength;
				// move back to the start of a UTF-8 sequence so no character is split
				while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
					--cut;
				out.resize(cut);
			}
			return out;
		}

		/** Parses a HELLOLAN reply. Returns false for anything else (e.g. a plain "HI"). */
		bool ParseServerInfo(const std::string& payload, PingTesterServerInfo& out) {
			if (payload.empty() || payload[0] != '{')
				return false;

			Json::Value root;
			Json::Reader reader;
			if (!reader.parse(payload, root) || !root.isObject())
				return false;

			auto getString = [&](const char* key) {
				const Json::Value& v = root[key];
				return v.isString() ? v.asString() : std::string();
			};
			auto getInt = [&](const char* key) {
				const Json::Value& v = root[key];
				return v.isInt() ? v.asInt() : 0;
			};

			// Everything here comes from the network, so keep it bounded and printable
			out.name = SanitizeText(getString("name"), 64);
			out.mapName = SanitizeText(getString("map"), 64);
			out.gameMode = SanitizeText(getString("game_mode"), 32);
			out.gameVersion = SanitizeText(getString("game_version"), 16);
			out.numPlayers = getInt("players_current");
			out.maxPlayers = getInt("players_max");

			// "extensions" is a list of [id, version] pairs
			out.extensions.clear();
			const Json::Value& exts = root["extensions"];
			if (exts.isArray()) {
				for (const Json::Value& e : exts) {
					if (e.isArray() && e.size() >= 2 && e[0u].isInt() && e[1u].isInt())
						out.extensions.emplace_back(e[0u].asInt(), e[1u].asInt());
				}
			}
			return true;
		}
	}
	
	bool ParseHelloLanReply(const std::string& payload, PingTesterServerInfo& out) {
		return ParseServerInfo(payload, out);
	}

	struct PingTester::Private {
		std::shared_ptr<Shared> shared{new Shared()};
		std::unordered_map<std::string, PingTesterResult> results;

		/** Mapping from `ENetAddress`es to `std::string`s. */
		std::unordered_multimap<ENetAddress, std::string> addressMap;

		std::priority_queue<QueueItem> queue;

		/** Ongoing measurements. Bounded by `g_maxSimultaneousMeasurement`. */
		std::vector<QueueItem> ongoing;

		/** Measurement thread object, can be null. Can be running or not. */
		MeasureThread* thread{nullptr};

		Private() {
			SPADES_MARK_FUNCTION();
			ongoing.reserve(g_maxSimultaneousMeasurement);
		}

		~Private() {
			SPADES_MARK_FUNCTION();

			shared->shutdown.store(true);

			// Detach the thread
			if (thread)
				thread->MarkForAutoDeletion();
		}
	};

	PingTester::PingTester() : priv{new Private()} {}
	PingTester::~PingTester() {}

	void PingTester::AddTarget(const std::string &address) {
		SPADES_MARK_FUNCTION();
		
		// The address is already added to the measurement list.
		if (priv->results.find(address) != priv->results.end())
			return;

		priv->results.emplace(address, PingTesterResult{});

		// Parse the address
		// FIXME: we could reject invalid address here
		//        (We're allowed to hold it indefinitely according to the definition of
		//         `AddTarget()`)
		auto addr = ServerAddress{address, ProtocolVersion::v075}.GetENetAddress();

		bool alreadyInserted = priv->addressMap.find(addr) != priv->addressMap.end();

		priv->addressMap.emplace(addr, address);

		// Another measurement with the same ENet address but a different address string
		// is already inserted.
		if (alreadyInserted)
			return;

		QueueItem item;

		item.time = clock::now();
		item.address = addr;
		item.retryBackoff = g_initialRetryBackoff;
		item.retriesRemaining = g_retryCount;

		priv->queue.push(std::move(item));
	}

	void PingTester::Update() {
		SPADES_MARK_FUNCTION();

		auto now = clock::now();
		
		bool debugPing = cl_debugServerPing;

		// Retrieve the result packets
		auto packet = priv->shared->firstResultPacket.take();
		for (; packet; packet = std::move(packet->next)) {
			if (debugPing)
				SPLog("Received a packet from %s", ToString(packet->address).c_str());

			// Find the matching element from the "ongoing" list
			auto it =
			  std::find_if(priv->ongoing.begin(), priv->ongoing.end(),
			               [&](const QueueItem& item) { return item.address == packet->address; });
			if (it == priv->ongoing.end()) {
				if (debugPing) {
					SPLog("No ongoing item matching to the host %s",
					      ToString(packet->address).c_str());
				}

				continue;
			}

			// Compute the result
			QueueItem& item = *it;
			// Use the time the measurement thread received the packet, not the time of this
			// frame, so the frame rate does not leak into the result
			auto diff = packet->receiveTime - item.time;
			auto ping = std::max(0, static_cast<int>(duration_cast<milliseconds>(diff).count()));

			if (debugPing)
				SPLog("Pong from %s, RTT = %d", ToString(packet->address).c_str(), ping);

			// Store the result
			auto range = priv->addressMap.equal_range(item.address);

			for (auto it = range.first; it != range.second; ++it) {
				PingTesterResult& result = priv->results[it->second];
				result.ping = stmp::optional<int>{ping};

				if (debugPing)
					SPLog("%s matches %s", it->second.c_str(), ToString(packet->address).c_str());
			}

			priv->addressMap.erase(range.first, range.second);

			// Remove it from the "ongoing" list
			// (Actually we don't have to preserve the order... I miss `Vec::swap_remove`)
			priv->ongoing.erase(it);
		}

		// Retry/drop the timed out measurements
		{
			for (auto &item : priv->ongoing) {
				if (!(now > item.deadline))
					continue;

				if (item.retriesRemaining == 0) {
					// Give up
					// (We're allowed to hold it indefinitely according to the definition of
					// `AddTarget()`)

					if (debugPing) {
						SPLog("Host %s didn't respond --- giving up",
						      ToString(item.address).c_str());
					}

					continue;
				}

				if (debugPing) {
					SPLog("Host %s didn't respond --- retrying in %d ms",
					      ToString(item.address).c_str(),
					      int(duration_cast<milliseconds>(item.retryBackoff).count()));
				}

				// Retry the measurement some time later
				item.time = now + item.retryBackoff;
				--item.retriesRemaining;

				item.retryBackoff *= 2;
				if (item.retryBackoff > g_maximumRetryBackoff)
					item.retryBackoff = g_maximumRetryBackoff;

				priv->queue.push(item);
			}

			priv->ongoing.erase(
			  std::remove_if(priv->ongoing.begin(), priv->ongoing.end(),
			                 [&](const QueueItem& item) { return now > item.deadline; }),
			  priv->ongoing.end());
		}

		// Initiate new measurements
		ENetBuffer pingBuffer;
		pingBuffer.data = const_cast<char*>(hello::kPingRequest);
		pingBuffer.dataLength = hello::kPingRequestLength;

		while (priv->ongoing.size() < g_maxSimultaneousMeasurement && !priv->queue.empty() &&
		       priv->shared->ready.load()) {
			QueueItem item = priv->queue.top();

			if (now < item.time)
				break;

			if (debugPing)
				SPLog("Pinging the host %s", ToString(item.address).c_str());
				
			int result =
			  enet_socket_send(priv->shared->socket.handle, &item.address, &pingBuffer, 1);
			if (result == 0) {
				// Hmm? (maybe send buffer is full)
				if (debugPing)
					SPLog("enet_socket_send returned 0.");
				break;
			}
			if (result < 0) {
				// Not fatal: e.g. EHOSTUNREACH when the network is down or when macOS denies
				// local network access. Treat it as a lost packet, so the regular timeout and
				// backoff logic retries it later and eventually gives up.
				if (debugPing)
					SPLog("enet_socket_send failed, treating it as a lost packet.");
			}

			// Record the submission time
			item.time = now;

			// Move it to the "ongoing" list
			priv->queue.pop();
			item.deadline = now + g_timeout;

			priv->ongoing.push_back(std::move(item));
		}

		bool active = !priv->ongoing.empty() || !priv->queue.empty();

		if (active && (!priv->thread || priv->shared->shutdown.load())) {
			// Start a measurement thread.
			if (priv->thread) {
				if (debugPing)
					SPLog("Waiting for the previous measurement thread to finish running...");

				// We must wait until the previous thread is shut down completely
				// because it shares the same `Shared` object.
				// (If we reset `shared->shutdown` prematurely, we might end up in having
				// two `MeasureThread`s running at the same time!)
				priv->thread->Join();
				priv->thread->MarkForAutoDeletion();
			}

			if (debugPing)
				SPLog("Starting a measurement thread.");

			priv->shared->shutdown.store(false);
			priv->thread = new MeasureThread(priv->shared);
			priv->thread->Start();
		}

		if (!active && (priv->thread && !priv->shared->shutdown.load())) {
			if (debugPing)
				SPLog("Shutting the measurement thread down.");

			// Stop the currently running measurement thread.
			priv->shared->shutdown.store(true);
			priv->shared->ready.store(false);
		}
	}

	stmp::optional<PingTesterResult> PingTester::GetTargetResult(const std::string& address) {
		SPADES_MARK_FUNCTION();

		auto it = priv->results.find(address);
		if (it == priv->results.end())
			return {};

		return {it->second};
	}
}
