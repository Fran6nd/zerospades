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

#include <memory>
#include <string>

#include "GameConstants.h"
#include "INetClient.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class Grenade;
		struct GameProperties;
		struct PlayerInput;
		struct WeaponInput;

		/**
		 * INetClient implementation for standalone .vxl map viewing.
		 *
		 * Loads a local map file from disk, synthesizes a minimal world with one
		 * spectator local player, and hands control to Client's free-floating
		 * camera. No network traffic, no bots, no game logic beyond what the
		 * world itself drives.
		 */
		class LocalMapNetClient : public INetClient {
			Client* client;
			NetClientStatus status;
			std::shared_ptr<GameProperties> properties;
			std::string mapFilePath;
			std::string statusString;
			bool worldInitialized;

			void InitializeWorld();

		public:
			LocalMapNetClient(Client* client);
			~LocalMapNetClient() override;

			/**
			 * Opens a local .vxl map file for standalone playback.
			 * Returns true on success; the actual map decode happens on the first
			 * DoEvents() call so the loading screen has a chance to render.
			 */
			bool OpenMap(const std::string& filename);

			// INetClient
			void DoEvents(float dt) override;
			NetClientStatus GetStatus() override { return status; }
			std::string GetStatusString() override { return statusString; }
			float GetMapReceivingProgress() override { return worldInitialized ? 1.0f : 0.0f; }
			const std::shared_ptr<GameProperties>& GetGameProperties() override { return properties; }

			void Disconnect() override { status = NetClientStatusNotConnected; }
			int GetPing() override { return 0; }
			float GetPacketLoss() override { return 0.0f; }
			float GetPacketThrottle() override { return 1.0f; }
			double GetDownlinkBps() override { return 0.0; }
			double GetUplinkBps() override { return 0.0; }

			void SendJoin(int, WeaponType, std::string, int) override {}
			void SendPosition(Vector3) override {}
			void SendOrientation(Vector3) override {}
			void SendPlayerInput(PlayerInput) override {}
			void SendWeaponInput(WeaponInput) override {}
			void SendHit(int, HitType) override {}
			void SendGrenade(const Grenade&) override {}
			void SendTool() override {}
			void SendHeldBlockColor() override {}
			void SendBlockAction(IntVector3, BlockActionType) override {}
			void SendBlockLine(IntVector3, IntVector3) override {}
			void SendChat(std::string, bool) override {}
			void SendReload() override {}
			void SendTeamChange(int) override {}
			void SendWeaponChange(WeaponType) override {}
		};
	} // namespace client
} // namespace spades
