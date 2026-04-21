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

#include <cstdio>

#include "CTFGameMode.h"
#include "Client.h"
#include "GameMap.h"
#include "GameProperties.h"
#include "LocalMapNetClient.h"
#include "NetProtocol.h"
#include "Player.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/StdStream.h>
#include <Core/Strings.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {

		LocalMapNetClient::LocalMapNetClient(Client* c)
		    : client(c), status(NetClientStatusNotConnected), worldInitialized(false) {
			SPADES_MARK_FUNCTION();
			properties.reset(new GameProperties(ProtocolVersion::v075));
			statusString = _Tr("NetClient", "Not connected");
		}

		LocalMapNetClient::~LocalMapNetClient() { SPADES_MARK_FUNCTION(); }

		bool LocalMapNetClient::OpenMap(const std::string& filename) {
			SPADES_MARK_FUNCTION();

			// Verify the file is readable before committing to the loading state.
			FILE* f = std::fopen(filename.c_str(), "rb");
			if (!f) {
				statusString = _Tr("NetClient", "Failed to open map file");
				return false;
			}
			std::fclose(f);

			mapFilePath = filename;
			status = NetClientStatusReceivingMap;
			statusString = _Tr("NetClient", "Loading map");
			SPLog("LocalMapNetClient: opening map '%s'", filename.c_str());
			return true;
		}

		void LocalMapNetClient::DoEvents(float /*dt*/) {
			SPADES_MARK_FUNCTION();

			if (status != NetClientStatusReceivingMap || worldInitialized)
				return;

			try {
				InitializeWorld();
				worldInitialized = true;
				status = NetClientStatusConnected;
				statusString = _Tr("NetClient", "Local map");
			} catch (const std::exception& ex) {
				SPLog("LocalMapNetClient: map load failed: %s", ex.what());
				status = NetClientStatusNotConnected;
				statusString = _Tr("NetClient", "Error loading map");
				throw;
			}
		}

		void LocalMapNetClient::InitializeWorld() {
			SPADES_MARK_FUNCTION();

			// Decode the .vxl file synchronously from disk.
			FILE* f = std::fopen(mapFilePath.c_str(), "rb");
			if (!f)
				SPRaise("Failed to open map file: %s", mapFilePath.c_str());

			StdStream stream(f, true);
			GameMap* rawMap = GameMap::Load(&stream);
			Handle<GameMap> map(rawMap, false);
			SPLog("LocalMapNetClient: map decoded successfully");

			// Build a minimal world: one spectator local player on a neutral CTF layout.
			World* w = new World(properties);
			w->SetMap(map);

			World::Team& t1 = w->GetTeam(0);
			World::Team& t2 = w->GetTeam(1);
			t1.color = MakeIntVector3(0, 0, 200);
			t2.color = MakeIntVector3(0, 200, 0);
			t1.name = "Blue";
			t2.name = "Green";
			w->SetFogColor(MakeIntVector3(128, 180, 220));

			auto ctf = stmp::make_unique<CTFGameMode>();
			Vector3 center =
			  MakeVector3(static_cast<float>(w->GetMap()->Width()) * 0.5f,
			              static_cast<float>(w->GetMap()->Height()) * 0.5f, 0.0f);
			ctf->GetTeam(0).basePos = center;
			ctf->GetTeam(1).basePos = center;
			ctf->GetTeam(0).flagPos = center;
			ctf->GetTeam(1).flagPos = center;
			w->SetMode(std::move(ctf));

			// Hand ownership to the client. SetWorld wires listeners and per-slot
			// ClientPlayer objects based on the existing player roster.
			client->SetWorld(w);

			// Add the local spectator and position the free camera at map center.
			auto p = stmp::make_unique<Player>(*w, 0, RIFLE_WEAPON, 2);
			w->SetPlayer(0, std::move(p));
			w->SetLocalPlayerIndex(0);
			w->GetPlayerPersistent(0).name = "Player";

			client->JoinedGame();
		}

	} // namespace client
} // namespace spades
