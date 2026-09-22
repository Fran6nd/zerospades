/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "ModelFileTypes.h"

#include <Core/LocalFileSystem.h>
#include <Core/Strings.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		namespace {
			/** One model file type, and whether the editor can open it yet. A type
			 *  that cannot be opened is still listed: a model the editor will
			 *  eventually read should be visible where the others are, rather than
			 *  leaving the player wondering where their file went. */
			struct ModelFileType {
				const char* extension;
				bool editable;
				/** A scene holds several named objects with their own transforms,
				 *  rather than one grid of voxels, so the editor opens it in Object
				 *  mode instead of straight into Edit mode. */
				bool scene;
			};

			const ModelFileType kModelFileTypes[] = {
			  {".kv6", true, false},
			  {".2kv6", true, true},
			  {".vxl", false, false},
			};

			const ModelFileType* FindType(const std::string& name) {
				for (const ModelFileType& type : kModelFileTypes) {
					if (fs::HasExtension(name, type.extension))
						return &type;
				}
				return nullptr;
			}
		} // namespace

		const std::vector<std::string>& KV6ModelExtensions() {
			static const std::vector<std::string> extensions = [] {
				std::vector<std::string> list;
				for (const ModelFileType& type : kModelFileTypes)
					list.push_back(type.extension);
				return list;
			}();
			return extensions;
		}

		const std::string& KV6DocumentExtension() {
			// The first editable type is what a new document is written as.
			static const std::string extension = [] {
				for (const ModelFileType& type : kModelFileTypes) {
					if (type.editable)
						return std::string(type.extension);
				}
				return std::string();
			}();
			return extension;
		}

		const std::vector<std::string>& KV6EditableExtensions() {
			static const std::vector<std::string> extensions = [] {
				std::vector<std::string> list;
				for (const ModelFileType& type : kModelFileTypes) {
					if (type.editable)
						list.push_back(type.extension);
				}
				return list;
			}();
			return extensions;
		}

		const std::string& KV6SceneExtension() {
			// The first editable scene type is what a new scene is written as.
			static const std::string extension = [] {
				for (const ModelFileType& type : kModelFileTypes) {
					if (type.editable && type.scene)
						return std::string(type.extension);
				}
				return std::string();
			}();
			return extension;
		}

		bool KV6IsEditable(const std::string& name) {
			const ModelFileType* type = FindType(name);
			return type && type->editable;
		}

		bool KV6IsScene(const std::string& name) {
			const ModelFileType* type = FindType(name);
			return type && type->scene;
		}

		std::string KV6DocumentFileName(const std::string& name, const std::string& extension) {
			return FindType(name) ? name : name + extension;
		}

		std::string KV6DocumentFileName(const std::string& name) {
			return KV6DocumentFileName(name, KV6DocumentExtension());
		}

		std::string KV6UntitledFileName(const std::string& extension) {
			return "untitled" + extension;
		}

		std::string KV6UntitledFileName() { return KV6UntitledFileName(KV6DocumentExtension()); }

		std::string KV6ModelFilterLabel() { return _Tr("KV6Editor", "Voxel models"); }

		std::string KV6UnsupportedHint() { return _Tr("KV6Editor", "(not implemented)"); }

		std::string KV6UnsupportedMessage() {
			// Name every type that can be opened, not just the one a new document is
			// written as, so the message stays true as types become editable.
			std::string editable;
			for (const ModelFileType& type : kModelFileTypes) {
				if (!type.editable)
					continue;
				if (!editable.empty())
					editable += ", ";
				editable += type.extension;
			}
			return _Tr("KV6Editor",
					   "This file type is not supported yet. Only {0} files can be edited.",
					   editable);
		}
	} // namespace gui
} // namespace spades
