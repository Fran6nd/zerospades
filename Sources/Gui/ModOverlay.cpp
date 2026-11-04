/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#include "Main.h"
#include "ModOverlay.h"
#include "ModsScreenHelper.h"
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/ResourceChanges.h>
#include <Core/TMPUtils.h>
#include <Core/ZipFileSystem.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			struct MountedPak {
				std::string id;                  // path and checksum: equal ids, equal files
				std::unique_ptr<IStream> stream; // the ZipFileSystem doesn't own it
				ZipFileSystem* fs;               // owned by FileManager
			};

			std::vector<MountedPak> g_mountedPaks;
			std::vector<std::string> g_mountedMods;

			// The id of the pak each mounted file is read from: the topmost one.
			std::unordered_map<std::string, std::string> GetWinners() {
				std::unordered_map<std::string, std::string> winners;
				for (const MountedPak& pak : g_mountedPaks) {
					for (const std::string& file : pak.fs->GetAllFiles())
						winners[file] = pak.id;
				}
				return winners;
			}

			// Mounts `mods` in place of what is mounted, the last one on top.
			void MountSet(const std::vector<std::string>& mods) {
				for (MountedPak& pak : g_mountedPaks)
					FileManager::RemoveFileSystem(pak.fs);
				g_mountedPaks.clear();
				g_mountedMods = mods;

				for (const std::string& path : ModsScreenHelper::GetModPakPaths(mods)) {
					try {
						// Read the whole pak into memory and mount from there, so the
						// file on disk is never held open. That lets the mod manager
						// overwrite or delete an enabled pak (for example re-downloading
						// it) while the game is running.
						std::string bytes = FileManager::ReadAllBytes(path.c_str());
						auto stream = stmp::make_unique<DynamicMemoryStream>();
						stream->Write(bytes.data(), bytes.size());
						stream->SetPosition(0);

						auto crc = crc32(0, reinterpret_cast<const Bytef*>(bytes.data()),
						                 static_cast<uInt>(bytes.size()));
						std::string id = path + "#" + std::to_string(crc);

						auto* fs = new ZipFileSystem(stream.get());
						FileManager::PrependFileSystem(fs);
						g_mountedPaks.push_back({std::move(id), std::move(stream), fs});
						SPLog("Mod pak mounted (in memory): %s", path.c_str());
					} catch (const std::exception& ex) {
						SPLog("Mod pak failed to mount: %s: %s", path.c_str(), ex.what());
					}
				}
			}
		} // namespace

		void ModOverlay::Mount() {
			SPADES_MARK_FUNCTION();

			// A --try-mod run applies the mod under test on top of the base paks only.
			if (g_tryMod)
				return;

			MountSet(Handle<ModsScreenHelper>::New()->GetEnabledMods());
		}

		ModApplyResult ModOverlay::Apply(client::IRenderer& renderer, client::IAudioDevice& audio,
		                                 std::string& error) {
			SPADES_MARK_FUNCTION();

			// A file changed if it is now read from another pak, or from none.
			std::vector<std::string> previous = g_mountedMods;
			auto before = GetWinners();
			Mount();
			auto after = GetWinners();

			ResourceChanges changes;
			for (const auto& file : before) {
				auto it = after.find(file.first);
				if (it == after.end() || it->second != file.second)
					changes.Add(file.first);
			}
			for (const auto& file : after) {
				if (before.find(file.first) == before.end())
					changes.Add(file.first);
			}

			bool live = changes.GetReloadTime() == ResourceChanges::ReloadTime::Live;

			// Build the new scripts first. Ones that don't build would also stop the
			// next start, so go back to the set that was mounted, on disk too.
			if (changes.ContainsUnder("Scripts")) {
				ScriptManager& scripts = *ScriptManager::GetInstance();
				error = live ? scripts.ReloadScripts() : scripts.CheckScripts();
				if (!error.empty()) {
					SPLog("Mods not applied: %s", error.c_str());
					Handle<ModsScreenHelper>::New()->SetEnabledMods(previous);
					MountSet(previous);
					return ModApplyResult::Refused;
				}
			}

			// What is loaded was built from the previous set, so keep that mounted
			// until the restart; the enabled set on disk is the new one.
			if (!live) {
				MountSet(previous);
				return ModApplyResult::NeedsRestart;
			}

			// The players' skins are built again from the new scripts by the game.
			renderer.ReloadResources(changes);
			audio.ReloadResources(changes);
			SPLog("Mods applied in place");
			return ModApplyResult::Applied;
		}
	} // namespace gui
} // namespace spades
