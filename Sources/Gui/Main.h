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

#include <string>

namespace spades {
	class ServerAddress;

	/** The path to the user resource directory. Can be empty. */
	extern std::string g_userResourceDirectory;

	/** argv[0] captured at startup, used to relaunch the program. */
	extern std::string g_executablePath;

	/** Set by --open-mods. Skips the startup window and selects the Mods tab. */
	extern bool g_openModsTab;

	void StartClient(const ServerAddress&);
	void StartMainScreen();
	void StartDemoReplay(const std::string& demoPath);

	/** Relaunch the program with --open-mods, then exit the current process. */
	void RelaunchForMods();
} // namespace spades