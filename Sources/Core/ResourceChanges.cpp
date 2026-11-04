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

#include <cctype>
#include <utility>

#include "ResourceChanges.h"

namespace spades {
	namespace {
		std::string Normalize(const std::string& path) {
			std::string out = path;
			for (char& c : out) {
				if (c == '\\')
					c = '/';
				else
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		bool StartsWith(const std::string& s, const char* prefix) {
			return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
		}

		bool EndsWith(const std::string& s, const char* suffix) {
			std::size_t n = std::char_traits<char>::length(suffix);
			return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
		}
	} // namespace

	void ResourceChanges::Add(const std::string& path) {
		std::string p = Normalize(path);
		// Zip files can list their folders too; those hold nothing to reload.
		if (p.empty() || p.back() == '/')
			return;
		// FileManager serves `foo.weak` as `foo` when nothing has `foo` itself.
		if (EndsWith(p, ".weak"))
			p.resize(p.size() - 5);
		paths.insert(std::move(p));
	}

	bool ResourceChanges::Contains(const std::string& path) const {
		return paths.count(Normalize(path)) != 0;
	}

	bool ResourceChanges::ContainsUnder(const std::string& dir) const {
		std::string prefix = Normalize(dir);
		if (!prefix.empty() && prefix.back() != '/')
			prefix += '/';
		for (const std::string& path : paths) {
			if (StartsWith(path, prefix.c_str()))
				return true;
		}
		return false;
	}

	ResourceChanges::ReloadTime ResourceChanges::GetReloadTime() const {
		for (const std::string& path : paths) {
			if (GetReloadTime(path) == ReloadTime::Restart)
				return ReloadTime::Restart;
		}
		return ReloadTime::Live;
	}

	ResourceChanges::ReloadTime ResourceChanges::GetReloadTime(const std::string& path) {
		std::string p = Normalize(path);

		// Fonts measure and lay out text once, so even their atlas images wait.
		if (StartsWith(p, "gfx/fonts/") || StartsWith(p, "fonts/"))
			return ReloadTime::Restart;

		// Voxel models: GLModelManager, SWModelManager.
		if (EndsWith(p, ".kv6") || EndsWith(p, ".meta.json"))
			return ReloadTime::Live;

		// Images: GLImageManager, SWImageManager.
		if (EndsWith(p, ".png") || EndsWith(p, ".jpg") || EndsWith(p, ".jpeg") ||
		    EndsWith(p, ".tga") || EndsWith(p, ".bmp"))
			return ReloadTime::Live;

		// Sounds: ALDevice. NullDevice has nothing to reload.
		if (EndsWith(p, ".opus") || EndsWith(p, ".ogg") || EndsWith(p, ".wav"))
			return ReloadTime::Live;

		// Scripts: ScriptManager replaces the module, and the game builds the
		// players' skins again from it.
		if (StartsWith(p, "scripts/"))
			return ReloadTime::Live;

		// Shaders, translations, the title map and anything else.
		return ReloadTime::Restart;
	}
} // namespace spades
