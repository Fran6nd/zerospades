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

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace spades {
	// Minimal writer for the PKZip "store" / DEFLATE format. Writes a single
	// .zip / .pak that the bundled minizip unzip reader can open.
	class ZipWriter {
	public:
		explicit ZipWriter(const std::string& path);
		~ZipWriter();

		ZipWriter(const ZipWriter&) = delete;
		ZipWriter& operator=(const ZipWriter&) = delete;

		// Adds a file entry. `data` is the uncompressed bytes; ZipWriter
		// chooses DEFLATE or STORED automatically (STORED if DEFLATE would
		// not shrink the data). May be called many times before Close().
		void AddEntry(const std::string& name, const void* data, std::size_t size);

		// Writes the central directory and closes the file. Idempotent.
		void Close();

	private:
		struct Entry {
			std::string name;
			std::uint32_t crc32;
			std::uint32_t compressedSize;
			std::uint32_t uncompressedSize;
			std::uint32_t localHeaderOffset;
			std::uint16_t method; // 0 = STORED, 8 = DEFLATE
		};

		std::FILE* file;
		std::vector<Entry> entries;
		std::uint32_t cursor;
		bool closed;

		void WriteRaw(const void* data, std::size_t size);
		void WriteU16(std::uint16_t v);
		void WriteU32(std::uint32_t v);
	};
} // namespace spades
