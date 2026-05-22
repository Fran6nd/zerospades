/*
 Copyright (c) 2026 ZeroSpades contributors

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

#include "MapRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#include <dirent.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

#include <Core/Debug.h>

namespace spades {
	namespace client {

		namespace {
			std::string s_baseDir;

			constexpr const char* kExtension = ".vxl";
			constexpr size_t kExtensionLen = 4;

			bool HasVxlExtension(const std::string& name) {
				if (name.size() <= kExtensionLen)
					return false;
				const char* tail = name.c_str() + (name.size() - kExtensionLen);
				for (size_t i = 0; i < kExtensionLen; ++i) {
					if (std::tolower(static_cast<unsigned char>(tail[i])) != kExtension[i])
						return false;
				}
				return true;
			}

			std::string StripExtension(const std::string& name) {
				return name.substr(0, name.size() - kExtensionLen);
			}

			int CaseInsensitiveCompare(const std::string& a, const std::string& b) {
				size_t n = std::min(a.size(), b.size());
				for (size_t i = 0; i < n; ++i) {
					int ca = std::tolower(static_cast<unsigned char>(a[i]));
					int cb = std::tolower(static_cast<unsigned char>(b[i]));
					if (ca != cb)
						return ca - cb;
				}
				if (a.size() < b.size())
					return -1;
				if (a.size() > b.size())
					return 1;
				return 0;
			}
		} // namespace

		void MapRegistry::SetBaseDirectory(const std::string& dir) { s_baseDir = dir; }

		std::string MapRegistry::GetMapsDirectory() {
			return s_baseDir.empty() ? "Mapshots" : s_baseDir + "/Mapshots";
		}

		std::vector<MapEntry> MapRegistry::ListMaps() {
			SPADES_MARK_FUNCTION();

			std::string mapsDir = GetMapsDirectory();
			std::vector<MapEntry> entries;

#ifdef _WIN32
			WIN32_FIND_DATAA fd;
			HANDLE hFind = FindFirstFileA((mapsDir + "\\*" + kExtension).c_str(), &fd);
			if (hFind == INVALID_HANDLE_VALUE)
				return entries;
			do {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				std::string name = fd.cFileName;
				if (!HasVxlExtension(name))
					continue;
				MapEntry entry;
				entry.path = mapsDir + "/" + name;
				entry.displayName = StripExtension(name);
				ULARGE_INTEGER size;
				size.LowPart = fd.nFileSizeLow;
				size.HighPart = fd.nFileSizeHigh;
				entry.sizeBytes = static_cast<uint64_t>(size.QuadPart);
				ULARGE_INTEGER ft;
				ft.LowPart = fd.ftLastWriteTime.dwLowDateTime;
				ft.HighPart = fd.ftLastWriteTime.dwHighDateTime;
				// Convert Windows FILETIME (100ns since 1601) to Unix epoch seconds.
				entry.mtime = static_cast<int64_t>((ft.QuadPart - 116444736000000000ULL) / 10000000ULL);
				entries.push_back(std::move(entry));
			} while (FindNextFileA(hFind, &fd));
			FindClose(hFind);
#else
			DIR* dir = opendir(mapsDir.c_str());
			if (!dir)
				return entries;
			struct dirent* ent;
			while ((ent = readdir(dir)) != nullptr) {
				std::string name = ent->d_name;
				if (name == "." || name == "..")
					continue;
				if (!HasVxlExtension(name))
					continue;
				std::string fullPath = mapsDir + "/" + name;
				struct stat st;
				if (stat(fullPath.c_str(), &st) != 0)
					continue;
				if (!S_ISREG(st.st_mode))
					continue;
				MapEntry entry;
				entry.path = std::move(fullPath);
				entry.displayName = StripExtension(name);
				entry.sizeBytes = static_cast<uint64_t>(st.st_size);
				entry.mtime = static_cast<int64_t>(st.st_mtime);
				entries.push_back(std::move(entry));
			}
			closedir(dir);
#endif

			std::sort(entries.begin(), entries.end(),
			          [](const MapEntry& a, const MapEntry& b) {
				          return CaseInsensitiveCompare(a.displayName, b.displayName) < 0;
			          });

			return entries;
		}

	} // namespace client
} // namespace spades
