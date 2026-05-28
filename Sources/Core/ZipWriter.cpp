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

#include "ZipWriter.h"

#include <cstring>
#include <ctime>
#include <vector>

#include <zlib.h>

#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace {
		// Returns a (dosTime, dosDate) pair for the current local time.
		void CurrentDosTime(std::uint16_t& dosTime, std::uint16_t& dosDate) {
			std::time_t now = std::time(nullptr);
			std::tm tm{};
#ifdef _WIN32
			localtime_s(&tm, &now);
#else
			localtime_r(&now, &tm);
#endif
			int year = tm.tm_year + 1900;
			if (year < 1980)
				year = 1980;
			dosDate = static_cast<std::uint16_t>(((year - 1980) << 9) |
			                                    ((tm.tm_mon + 1) << 5) | tm.tm_mday);
			dosTime = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) |
			                                    (tm.tm_sec / 2));
		}

		// DEFLATE-compresses `src` into `out`. Returns true if the compressed
		// size is strictly smaller than the input (otherwise caller should use
		// STORED). `out` is resized to the compressed size on success.
		bool DeflateInto(const void* src, std::size_t size, std::vector<unsigned char>& out) {
			if (size == 0)
				return false;

			z_stream zs{};
			// Raw deflate (no zlib wrapper) — that's what zip uses. Negative
			// windowBits selects raw mode.
			if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
			                 Z_DEFAULT_STRATEGY) != Z_OK)
				SPRaise("deflateInit2 failed");

			out.resize(deflateBound(&zs, static_cast<uLong>(size)));
			zs.next_in = const_cast<Bytef*>(static_cast<const Bytef*>(src));
			zs.avail_in = static_cast<uInt>(size);
			zs.next_out = out.data();
			zs.avail_out = static_cast<uInt>(out.size());

			int ret = deflate(&zs, Z_FINISH);
			if (ret != Z_STREAM_END) {
				deflateEnd(&zs);
				SPRaise("deflate did not finish (ret=%d)", ret);
			}
			std::size_t compressed = zs.total_out;
			deflateEnd(&zs);

			if (compressed >= size)
				return false;
			out.resize(compressed);
			return true;
		}
	} // namespace

	ZipWriter::ZipWriter(const std::string& path)
	    : file(nullptr), cursor(0), closed(false) {
		SPADES_MARK_FUNCTION();
		file = std::fopen(path.c_str(), "wb");
		if (!file)
			SPRaise("Failed to open '%s' for writing", path.c_str());
	}

	ZipWriter::~ZipWriter() {
		try {
			Close();
		} catch (...) {
			// Destructors must not throw. The half-written file is left for
			// the caller's atomic-rename strategy to discard.
		}
		if (file) {
			std::fclose(file);
			file = nullptr;
		}
	}

	void ZipWriter::WriteRaw(const void* data, std::size_t size) {
		if (size == 0)
			return;
		if (std::fwrite(data, 1, size, file) != size)
			SPRaise("Write failed");
		cursor += static_cast<std::uint32_t>(size);
	}

	void ZipWriter::WriteU16(std::uint16_t v) {
		unsigned char b[2] = {static_cast<unsigned char>(v & 0xff),
		                     static_cast<unsigned char>((v >> 8) & 0xff)};
		WriteRaw(b, 2);
	}

	void ZipWriter::WriteU32(std::uint32_t v) {
		unsigned char b[4] = {static_cast<unsigned char>(v & 0xff),
		                     static_cast<unsigned char>((v >> 8) & 0xff),
		                     static_cast<unsigned char>((v >> 16) & 0xff),
		                     static_cast<unsigned char>((v >> 24) & 0xff)};
		WriteRaw(b, 4);
	}

	void ZipWriter::AddEntry(const std::string& name, const void* data, std::size_t size) {
		SPADES_MARK_FUNCTION();
		if (closed)
			SPRaise("ZipWriter already closed");
		if (name.empty())
			SPRaise("Empty entry name");
		if (size > 0xfffffff0u)
			SPRaise("Entry too large for zip32: %s", name.c_str());

		Entry e;
		e.name = name;
		e.uncompressedSize = static_cast<std::uint32_t>(size);
		e.crc32 = size == 0
		              ? 0u
		              : static_cast<std::uint32_t>(
		                    crc32(0L, static_cast<const Bytef*>(data),
		                          static_cast<uInt>(size)));
		e.localHeaderOffset = cursor;

		std::vector<unsigned char> deflated;
		const void* payload = data;
		std::size_t payloadSize = size;
		if (DeflateInto(data, size, deflated)) {
			e.method = 8;
			payload = deflated.data();
			payloadSize = deflated.size();
		} else {
			e.method = 0;
		}
		e.compressedSize = static_cast<std::uint32_t>(payloadSize);

		std::uint16_t dosTime, dosDate;
		CurrentDosTime(dosTime, dosDate);

		// Local file header (PK\3\4).
		WriteU32(0x04034b50u);
		WriteU16(20);          // version needed
		WriteU16(0);           // general purpose flags
		WriteU16(e.method);
		WriteU16(dosTime);
		WriteU16(dosDate);
		WriteU32(e.crc32);
		WriteU32(e.compressedSize);
		WriteU32(e.uncompressedSize);
		WriteU16(static_cast<std::uint16_t>(name.size()));
		WriteU16(0); // extra length
		WriteRaw(name.data(), name.size());
		WriteRaw(payload, payloadSize);

		entries.push_back(std::move(e));
	}

	void ZipWriter::Close() {
		SPADES_MARK_FUNCTION();
		if (closed)
			return;
		closed = true;
		if (!file)
			return;

		std::uint32_t cdOffset = cursor;
		for (const Entry& e : entries) {
			// Central directory file header (PK\1\2).
			WriteU32(0x02014b50u);
			WriteU16(20); // version made by
			WriteU16(20); // version needed
			WriteU16(0);  // flags
			WriteU16(e.method);
			WriteU16(0); // mod time
			WriteU16(0); // mod date
			WriteU32(e.crc32);
			WriteU32(e.compressedSize);
			WriteU32(e.uncompressedSize);
			WriteU16(static_cast<std::uint16_t>(e.name.size()));
			WriteU16(0); // extra length
			WriteU16(0); // comment length
			WriteU16(0); // disk number start
			WriteU16(0); // internal attrs
			WriteU32(0); // external attrs
			WriteU32(e.localHeaderOffset);
			WriteRaw(e.name.data(), e.name.size());
		}
		std::uint32_t cdSize = cursor - cdOffset;

		// End of central directory (PK\5\6).
		WriteU32(0x06054b50u);
		WriteU16(0); // this disk
		WriteU16(0); // disk with CD start
		WriteU16(static_cast<std::uint16_t>(entries.size())); // entries on this disk
		WriteU16(static_cast<std::uint16_t>(entries.size())); // total entries
		WriteU32(cdSize);
		WriteU32(cdOffset);
		WriteU16(0); // comment length

		if (std::fflush(file) != 0)
			SPRaise("flush failed");
	}
} // namespace spades
