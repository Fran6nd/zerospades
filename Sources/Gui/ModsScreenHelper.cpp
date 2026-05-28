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

#include "ModsScreenHelper.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#include <curl/curl.h>
#include <json/json.h>

#include "Main.h"
#include <Core/Debug.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/Thread.h>
#include <Core/ZipFileSystem.h>
#include <Core/ZipWriter.h>
#include <ScriptBindings/ScriptManager.h>
#include <ZeroSpades.h>

DEFINE_SPADES_SETTING(cl_modsIndexUrl,
                      "https://api.github.com/repos/zerospades/zerospades-paks/contents/");

namespace spades {
	namespace gui {

		namespace {
			constexpr const char* kModsDir = "Mods";
			constexpr const char* kUserModsDir = "user_mods";

			struct CURLDeleter {
				void operator()(CURL* p) const {
					if (p)
						curl_easy_cleanup(p);
				}
			};

			std::string EndingLower(const std::string& s) {
				std::string out = s;
				std::transform(out.begin(), out.end(), out.begin(),
				               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return out;
			}

			bool EndsWithPak(const std::string& s) {
				std::string l = EndingLower(s);
				return l.size() >= 4 && (l.compare(l.size() - 4, 4, ".pak") == 0 ||
				                         l.compare(l.size() - 4, 4, ".zip") == 0);
			}

			void MakeDir(const std::string& path) {
#ifdef _WIN32
				_mkdir(path.c_str());
#else
				::mkdir(path.c_str(), 0775);
#endif
			}

			void EnsureDir(const std::string& path) {
				// Iteratively create each prefix.
				for (std::size_t i = 1; i < path.size(); ++i) {
					if (path[i] == '/' || path[i] == '\\')
						MakeDir(path.substr(0, i));
				}
				MakeDir(path);
			}

			std::int64_t FileSizeAbs(const std::string& path) {
				struct stat st;
				if (::stat(path.c_str(), &st) == 0)
					return static_cast<std::int64_t>(st.st_size);
				return -1;
			}

			std::vector<std::string> ListDir(const std::string& path) {
				std::vector<std::string> out;
#ifdef _WIN32
				WIN32_FIND_DATAA fd;
				HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					return out;
				do {
					if (std::strcmp(fd.cFileName, ".") == 0 ||
					    std::strcmp(fd.cFileName, "..") == 0)
						continue;
					out.emplace_back(fd.cFileName);
				} while (FindNextFileA(h, &fd));
				FindClose(h);
#else
				DIR* d = ::opendir(path.c_str());
				if (!d)
					return out;
				while (auto* e = ::readdir(d)) {
					if (e->d_name[0] == '.')
						continue;
					out.emplace_back(e->d_name);
				}
				::closedir(d);
#endif
				return out;
			}

			bool IsDirAbs(const std::string& path) {
				struct stat st;
				if (::stat(path.c_str(), &st) != 0)
					return false;
				return (st.st_mode & S_IFDIR) != 0;
			}

			std::string UserRoot() { return spades::g_userResourceDirectory; }
			std::string ModsRootAbs() { return UserRoot() + "/" + kModsDir; }
			std::string UserModsRootAbs() { return UserRoot() + "/" + kUserModsDir; }

			std::size_t CurlWriteToString(void* ptr, std::size_t size, std::size_t nmemb,
			                              void* userdata) {
				std::string* s = static_cast<std::string*>(userdata);
				std::size_t n = size * nmemb;
				s->append(static_cast<char*>(ptr), n);
				return n;
			}

			std::size_t CurlWriteToFile(void* ptr, std::size_t size, std::size_t nmemb,
			                            void* userdata) {
				std::FILE* f = static_cast<std::FILE*>(userdata);
				return std::fwrite(ptr, size, nmemb, f) * size;
			}

			// HTTP GET into a string. Returns "" on success, otherwise an error
			// description. User-Agent is required by the GitHub API.
			std::string HttpGetText(const std::string& url, std::string& out) {
				std::unique_ptr<CURL, CURLDeleter> h{curl_easy_init()};
				if (!h)
					return "curl_easy_init failed";
				curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
				curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
				curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
				curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToString);
				curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &out);
				curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
				curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
				curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_LIMIT, 15L);
				CURLcode rc = curl_easy_perform(h.get());
				if (rc != CURLE_OK)
					return curl_easy_strerror(rc);
				long code = 0;
				curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
				if (code >= 400)
					return Format("HTTP {0}", code);
				return std::string{};
			}

			std::string HttpDownloadToFile(const std::string& url,
			                               const std::string& destAbs) {
				std::FILE* f = std::fopen(destAbs.c_str(), "wb");
				if (!f)
					return Format("Cannot create '{0}'", destAbs);
				std::unique_ptr<CURL, CURLDeleter> h{curl_easy_init()};
				if (!h) {
					std::fclose(f);
					std::remove(destAbs.c_str());
					return "curl_easy_init failed";
				}
				curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
				curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
				curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
				curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToFile);
				curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, f);
				curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
				CURLcode rc = curl_easy_perform(h.get());
				long code = 0;
				curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
				std::fclose(f);
				if (rc != CURLE_OK) {
					std::remove(destAbs.c_str());
					return curl_easy_strerror(rc);
				}
				if (code >= 400) {
					std::remove(destAbs.c_str());
					return Format("HTTP {0}", code);
				}
				return std::string{};
			}
		} // namespace

		class ModsScreenHelper::RefreshQuery final : public Thread {
			Handle<ModsScreenHelper> owner;

			void Done(const std::string& msg) {
				owner->resultCell.store(std::make_unique<std::string>(msg));
				owner = nullptr;
			}

			// Downloads every .pak entry under the given Contents-API URL into
			// dirAbs. Folders found in the listing recurse. Returns "" on success.
			std::string FetchInto(const std::string& url, const std::string& modName,
			                       const std::string& dirAbs) {
				std::string body;
				std::string err = HttpGetText(url, body);
				if (!err.empty())
					return Format("List '{0}': {1}", url, err);

				Json::Reader reader;
				Json::Value root;
				if (!reader.parse(body, root, false))
					return Format("Parse '{0}': invalid JSON", url);
				if (!root.isArray())
					return Format("List '{0}': expected array", url);

				EnsureDir(dirAbs);

				for (const auto& entry : root) {
					std::string type = entry.get("type", "").asString();
					std::string name = entry.get("name", "").asString();
					if (type == "file" && EndsWithPak(name)) {
						std::string dl = entry.get("download_url", "").asString();
						if (dl.empty())
							continue;
						std::string partial = dirAbs + "/" + name + ".partial";
						std::string final = dirAbs + "/" + name;
						std::string e = HttpDownloadToFile(dl, partial);
						if (!e.empty())
							return Format("Download '{0}': {1}", name, e);
						std::remove(final.c_str());
						if (std::rename(partial.c_str(), final.c_str()) != 0) {
							std::remove(partial.c_str());
							return Format("Rename '{0}' failed", name);
						}
					} else if (type == "dir") {
						// Recurse one level: anything inside this folder is part of `modName`.
						std::string subUrl = entry.get("url", "").asString();
						if (subUrl.empty() || !modName.empty())
							continue;
						std::string e = FetchInto(subUrl, name, ModsRootAbs() + "/" + name);
						if (!e.empty())
							return e;
					}
				}
				return std::string{};
			}

		public:
			explicit RefreshQuery(ModsScreenHelper* o) : owner{o} {}

			void Run() override {
				try {
					EnsureDir(ModsRootAbs());
					std::string err = FetchInto(cl_modsIndexUrl.CString(), "", ModsRootAbs());
					Done(err);
				} catch (const std::exception& ex) {
					Done(ex.what());
				} catch (...) {
					Done("Unknown error");
				}
			}
		};

		ModsScreenHelper::ModsScreenHelper() : query(nullptr), modsCached(false) {
			SPADES_MARK_FUNCTION();
		}

		ModsScreenHelper::~ModsScreenHelper() {
			SPADES_MARK_FUNCTION();
			if (query)
				query->MarkForAutoDeletion();
		}

		void ModsScreenHelper::StartRefresh() {
			SPADES_MARK_FUNCTION();
			if (query)
				return; // already running
			lastMessage.clear();
			query = new RefreshQuery(this);
			query->Start();
		}

		bool ModsScreenHelper::PollRefreshState() {
			SPADES_MARK_FUNCTION();
			auto cell = resultCell.take();
			if (cell) {
				lastMessage = *cell;
				if (query) {
					query->MarkForAutoDeletion();
					query = nullptr;
				}
				modsCached = false; // freshen cache after download
			}
			return query == nullptr;
		}

		std::string ModsScreenHelper::GetRefreshMessage() { return lastMessage; }

		void ModsScreenHelper::RebuildModsCache() {
			mods.clear();
			std::string root = ModsRootAbs();
			if (!IsDirAbs(root)) {
				modsCached = true;
				return;
			}
			for (const std::string& entry : ListDir(root)) {
				std::string abs = root + "/" + entry;
				if (!IsDirAbs(abs))
					continue;
				ModEntry m;
				m.name = entry;
				for (const std::string& f : ListDir(abs)) {
					if (!EndsWithPak(f))
						continue;
					m.paks.push_back(f);
					std::int64_t sz = FileSizeAbs(abs + "/" + f);
					if (sz > 0)
						m.totalSize += sz;
				}
				std::sort(m.paks.begin(), m.paks.end());
				if (!m.paks.empty())
					mods.push_back(std::move(m));
			}
			std::sort(mods.begin(), mods.end(),
			          [](const ModEntry& a, const ModEntry& b) { return a.name < b.name; });
			modsCached = true;
		}

		const ModsScreenHelper::ModEntry*
		ModsScreenHelper::FindMod(const std::string& name) const {
			for (const ModEntry& m : mods)
				if (m.name == name)
					return &m;
			return nullptr;
		}

		CScriptArray* ModsScreenHelper::GetModNames() {
			if (!modsCached)
				RebuildModsCache();
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
			SPAssert(t != nullptr);
			CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(mods.size()));
			for (std::size_t i = 0; i < mods.size(); ++i)
				arr->SetValue(static_cast<asUINT>(i), &mods[i].name);
			return arr;
		}

		int ModsScreenHelper::GetModPakCount(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			const ModEntry* m = FindMod(modName);
			return m ? static_cast<int>(m->paks.size()) : 0;
		}

		std::int64_t ModsScreenHelper::GetModTotalSize(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			const ModEntry* m = FindMod(modName);
			return m ? m->totalSize : 0;
		}

		CScriptArray* ModsScreenHelper::GetModContents(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			std::vector<std::string> out;
			const ModEntry* m = FindMod(modName);
			if (m) {
				std::string dir = ModsRootAbs() + "/" + m->name;
				for (const std::string& pak : m->paks) {
					std::string abs = dir + "/" + pak;
					try {
						auto stream = FileManager::OpenForReading(
						  ("Mods/" + m->name + "/" + pak).c_str());
						ZipFileSystem zfs(stream.release());
						for (const std::string& f : zfs.GetAllFiles())
							out.push_back(pak + ": " + f);
					} catch (const std::exception& ex) {
						out.push_back(pak + ": <unreadable: " + ex.what() + ">");
					}
				}
			}
			std::sort(out.begin(), out.end());
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
			SPAssert(t != nullptr);
			CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(out.size()));
			for (std::size_t i = 0; i < out.size(); ++i)
				arr->SetValue(static_cast<asUINT>(i), &out[i]);
			return arr;
		}

		std::string ModsScreenHelper::MergeMod(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			const ModEntry* m = FindMod(modName);
			if (!m)
				return "Mod not found";

			std::string userModsAbs = UserModsRootAbs();
			EnsureDir(userModsAbs);

			std::string modDirAbs = ModsRootAbs() + "/" + m->name;

			for (const std::string& pak : m->paks) {
				std::string overlayPath = "Mods/" + m->name + "/" + pak;
				std::string targetRel = std::string(kUserModsDir) + "/" + pak;
				std::string targetAbs = userModsAbs + "/" + pak;
				std::string partialAbs = targetAbs + ".partial";

				// Collect overlay entries.
				std::vector<std::string> overlayKeys;
				try {
					auto in = FileManager::OpenForReading(overlayPath.c_str());
					ZipFileSystem zfs(in.release());
					overlayKeys = zfs.GetAllFiles();
				} catch (const std::exception& ex) {
					return Format("Read overlay '{0}': {1}", pak, ex.what());
				}
				std::set<std::string> overlaySet(overlayKeys.begin(), overlayKeys.end());

				try {
					ZipWriter writer(partialAbs);

					// 1. Carry over existing entries from the target pak that the
					//    overlay does not replace.
					if (FileManager::FileExists(targetRel.c_str())) {
						auto stream = FileManager::OpenForReading(targetRel.c_str());
						ZipFileSystem zbase(stream.release());
						for (const std::string& key : zbase.GetAllFiles()) {
							if (overlaySet.count(key))
								continue;
							auto entryStream = zbase.OpenForReading(key.c_str());
							std::string bytes = entryStream->ReadAllBytes();
							writer.AddEntry(key, bytes.data(), bytes.size());
						}
					}

					// 2. Now copy overlay entries (overlay wins on shared paths).
					auto stream2 = FileManager::OpenForReading(overlayPath.c_str());
					ZipFileSystem zover(stream2.release());
					for (const std::string& key : zover.GetAllFiles()) {
						auto entryStream = zover.OpenForReading(key.c_str());
						std::string bytes = entryStream->ReadAllBytes();
						writer.AddEntry(key, bytes.data(), bytes.size());
					}

					writer.Close();
				} catch (const std::exception& ex) {
					std::remove(partialAbs.c_str());
					return Format("Write '{0}': {1}", pak, ex.what());
				}

				std::remove(targetAbs.c_str());
				if (std::rename(partialAbs.c_str(), targetAbs.c_str()) != 0) {
					std::remove(partialAbs.c_str());
					return Format("Rename failed for '{0}'", pak);
				}
			}
			return std::string{};
		}

		std::string ModsScreenHelper::ResetUserMods() {
			std::string userModsAbs = UserModsRootAbs();
			if (!IsDirAbs(userModsAbs))
				return std::string{};
			for (const std::string& name : ListDir(userModsAbs)) {
				if (!EndsWithPak(name))
					continue;
				std::string abs = userModsAbs + "/" + name;
				if (std::remove(abs.c_str()) != 0)
					return Format("Failed to remove '{0}'", name);
			}
			return std::string{};
		}

	} // namespace gui
} // namespace spades
