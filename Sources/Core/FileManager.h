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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace spades {
	class IStream;
	class IFileSystem;

	/** How long what is built from a file is kept. Remounting mods only
	 * reaches what is loaded again afterwards. */
	enum class ResourceLifetime {
		Process, // until exit: menus, fonts, renderer internals
		Session, // not kept past a mod apply: a game, the title scene, pak reads
	};

	class FileManager {
		FileManager() {}

	public:
		/** Sets the lifetime of what this thread loads while in scope. The
		 * default is `Process`, the safe guess for an unknown holder. */
		class LifetimeScope {
			ResourceLifetime previous;

		public:
			explicit LifetimeScope(ResourceLifetime);
			~LifetimeScope();
			LifetimeScope(const LifetimeScope&) = delete;
			void operator=(const LifetimeScope&) = delete;
		};

		/** Whether `path` was looked up, or its folder listed, for something
		 * kept for the process: changing it then takes a restart. Found or
		 * not, since a fallback is kept too. Case-insensitive. */
		static bool IsHeldForProcess(const std::string& path);

		static std::unique_ptr<IStream> OpenForReading(const char*);
		static std::unique_ptr<IStream> OpenForWriting(const char*);
		static bool FileExists(const char*);
		static bool RemoveFile(const char*);
		static bool RenameFile(const char* oldName, const char* newName);
		static void AddFileSystem(IFileSystem*);
		static void AppendFileSystem(IFileSystem*);
		static void PrependFileSystem(IFileSystem*);

		/** Unmounts and destroys a file system, if mounted. Streams already
		 * opened from it are self-contained and stay valid. */
		static void RemoveFileSystem(IFileSystem*);
		static std::vector<std::string> EnumFiles(const char*);
		static std::string ReadAllBytes(const char*);
		static void Close();
	};
}; // namespace spades