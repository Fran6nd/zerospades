/*
 Copyright (c) 2026 ZeroSpades developers

 This file is part of ZeroSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Core/RefCountedObject.h>
#include <Core/TMPUtils.h>
#include <ScriptBindings/ScriptManager.h>

#include <AngelScript/addons/scriptarray.h>

namespace spades {
	namespace gui {

		class ModsScreenHelper : public RefCountedObject {
		public:
			ModsScreenHelper();

			// Async fetch of the official mod repo listing + download of every
			// listed pak into <userdata>/Resources/Mods/<modname>/.
			void StartRefresh();
			bool PollRefreshState();
			std::string GetRefreshMessage();

			// Live progress while a refresh is running.
			int GetRefreshTotal();
			int GetRefreshDone();
			std::string GetRefreshCurrentItem();

			CScriptArray* GetModNames();
			int GetModPakCount(std::string modName);
			int64_t GetModTotalSize(std::string modName);
			CScriptArray* GetModContents(std::string modName);

			// Returns "" on success, otherwise an error message.
			std::string MergeMod(std::string modName);
			std::string ResetUserMods();

		protected:
			~ModsScreenHelper();

		private:
			class RefreshQuery;

			struct ModEntry {
				std::string name;
				bool isFolder = false; // true: Mods/<name>/*.pak; false: Mods/<name> (single pak)
				std::vector<std::string> paks;
				std::int64_t totalSize = 0;
			};

			stmp::atomic_unique_ptr<std::string> resultCell;
			RefreshQuery* query;
			std::string lastMessage;
			std::vector<ModEntry> mods;
			bool modsCached;

			std::atomic<int> progressTotal;
			std::atomic<int> progressDone;
			std::mutex progressMutex;
			std::string progressItem;

			void RebuildModsCache();
			const ModEntry* FindMod(const std::string& name) const;
		};

	} // namespace gui
} // namespace spades
