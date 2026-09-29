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

#include <algorithm>
#include <cctype>
#include <list>
#include <mutex>
#include <set>
#include <unordered_set>

#include "Debug.h"
#include "Exception.h"
#include "FileManager.h"
#include "IFileSystem.h"
#include "IStream.h"

namespace spades {
	static std::list<IFileSystem*> g_fileSystems;

	namespace {
		thread_local ResourceLifetime t_lifetime = ResourceLifetime::Process;

		// Normalized paths looked up, and folders listed, while loading for
		// the process lifetime. A listing covers files added to it later.
		std::mutex g_heldMutex;
		std::unordered_set<std::string> g_heldForProcess;
		std::unordered_set<std::string> g_heldDirsForProcess;

		// The form zip archives index their entries in.
		std::string NormalizePath(const std::string& path) {
			std::string out = path;
			for (char& c : out) {
				if (c == '\\')
					c = '/';
				else
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		void NoteLookup(const char* fn) {
			if (t_lifetime != ResourceLifetime::Process)
				return;
			std::string path = NormalizePath(fn);
			std::lock_guard<std::mutex> lock{g_heldMutex};
			g_heldForProcess.insert(std::move(path));
		}

		void NoteListing(const char* dir) {
			if (t_lifetime != ResourceLifetime::Process)
				return;
			std::string path = NormalizePath(dir);
			while (!path.empty() && path.back() == '/')
				path.pop_back();
			std::lock_guard<std::mutex> lock{g_heldMutex};
			g_heldDirsForProcess.insert(std::move(path));
		}
	} // namespace

	FileManager::LifetimeScope::LifetimeScope(ResourceLifetime lifetime) : previous(t_lifetime) {
		t_lifetime = lifetime;
	}

	FileManager::LifetimeScope::~LifetimeScope() { t_lifetime = previous; }

	void FileManager::NoteUse(const char* path) { NoteLookup(path); }

	bool FileManager::IsHeldForProcess(const std::string& path) {
		std::string normalized = NormalizePath(path);
		std::size_t slash = normalized.rfind('/');
		std::string dir = slash == std::string::npos ? std::string() : normalized.substr(0, slash);

		std::lock_guard<std::mutex> lock{g_heldMutex};
		return g_heldForProcess.count(normalized) != 0 || g_heldDirsForProcess.count(dir) != 0;
	}

	std::unique_ptr<IStream> FileManager::OpenForReading(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		if (fn[0] == 0)
			SPFileNotFound(fn);

		NoteLookup(fn);

		// check each file system
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return fs->OpenForReading(fn);
		}

		// check weak files, too
		auto weak_fn = std::string(fn) + ".weak";
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(weak_fn.c_str()))
				return fs->OpenForReading(weak_fn.c_str());
		}

		SPFileNotFound(fn);
	}
	std::unique_ptr<IStream> FileManager::OpenForWriting(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		if (fn[0] == 0)
			SPFileNotFound(fn);
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return fs->OpenForWriting(fn);
		}

		// FIXME: handling of weak files

		// create file
		for (auto* fs : g_fileSystems) {
			try {
				return fs->OpenForWriting(fn);
			} catch (...) {}
		}

		SPRaise("No filesystem is writable");
	}
	bool FileManager::FileExists(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");

		NoteLookup(fn);

		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return true;
		}

		// check weak files, too
		auto weak_fn = std::string(fn) + ".weak";
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(weak_fn.c_str()))
				return true;
		}

		return false;
	}

	bool FileManager::RemoveFile(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return fs->RemoveFile(fn);
		}
		return false;
	}

	bool FileManager::RenameFile(const char* oldName, const char* newName) {
		SPADES_MARK_FUNCTION();
		if (!oldName || !newName)
			SPInvalidArgument("oldName/newName");
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(oldName))
				return fs->RenameFile(oldName, newName);
		}
		return false;
	}

	void FileManager::AddFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		AppendFileSystem(fs);
	}

	void FileManager::AppendFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");
		g_fileSystems.push_back(fs);
	}
	void FileManager::PrependFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");
		g_fileSystems.push_front(fs);
	}

	void FileManager::RemoveFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");

		auto it = std::find(g_fileSystems.begin(), g_fileSystems.end(), fs);
		if (it == g_fileSystems.end())
			return;

		g_fileSystems.erase(it);
		delete fs;
	}

	std::string FileManager::ReadAllBytes(const char* fn) {
		SPADES_MARK_FUNCTION();

		auto stream = OpenForReading(fn);
		SPAssert(stream);

		std::string ret = stream->ReadAllBytes();
		return ret;
	}

	std::vector<std::string> FileManager::EnumFiles(const char* path) {
		std::vector<std::string> list;
		std::set<std::string> set;
		if (!path)
			SPInvalidArgument("path");

		NoteListing(path);

		for (auto* fs : g_fileSystems) {
			std::vector<std::string> l = fs->EnumFiles(path);
			for (size_t i = 0; i < l.size(); i++)
				set.insert(l[i]);
		}

		for (auto& s : set)
			list.push_back(s);

		return list;
	}

	void FileManager::Close() {
		for (auto* fs : g_fileSystems)
			delete fs;
		g_fileSystems.clear();
	}
} // namespace spades