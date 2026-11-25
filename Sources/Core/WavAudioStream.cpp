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

#include "WavAudioStream.h"
#include "Debug.h"
#include "Exception.h"

namespace spades {
	WavAudioStream::WavAudioStream(IStream* s, bool ac) {
		SPADES_MARK_FUNCTION();

		stream = s;
		autoClose = ac;

		// skip header
		s->SetPosition(12 + s->GetPosition());
		// A chunk of an odd length is followed by a pad byte (RIFF keeps chunks
		// at even offsets). Not skipping it misread every chunk after one such
		// (a LIST chunk an editor wrote before 'fmt '), and the file was refused.
		while (s->GetPosition() + 8 <= s->GetLength()) {
			RiffChunkInfo info = ReadChunkInfo();
			chunks.emplace(info.name, info); // the first of a name counts
			s->SetPosition(info.dataPosition + info.length + (info.length & 1));
		}

		const RiffChunkInfo& fmt = GetChunk("fmt ");

		stream->SetPosition(fmt.dataPosition);
		stream->ReadLittleShort(); // ??
		channels = stream->ReadLittleShort();
		rate = stream->ReadLittleInt();
		stream->ReadLittleInt();
		stream->ReadLittleShort();
		int bits = stream->ReadLittleShort();
		switch (bits) {
			case 8: sampleFormat = UnsignedByte; break;
			case 16: sampleFormat = SignedShort; break;
			case 32: sampleFormat = SingleFloat; break;
			default: SPRaise("Unsupported bit count: %d", bits);
		}

		dataChunk = &GetChunk("data");
		stream->SetPosition(dataChunk->dataPosition);

		startPos = dataChunk->dataPosition;
		endPos = dataChunk->dataPosition + dataChunk->length;
	}

	const WavAudioStream::RiffChunkInfo& WavAudioStream::GetChunk(const std::string& name) {
		SPADES_MARK_FUNCTION();

		std::map<std::string, RiffChunkInfo>::iterator it;
		it = chunks.find(name);
		if (it == chunks.end())
			SPRaise("Failed to find RIFF chunk: '%s'", name.c_str());
		return it->second;
	}

	WavAudioStream::RiffChunkInfo WavAudioStream::ReadChunkInfo() {
		SPADES_MARK_FUNCTION();

		RiffChunkInfo info;
		info.name = stream->Read(4);
		if (info.name.size() < 4)
			SPRaise("Failed to read RIFF header name");

		info.length = stream->ReadLittleInt();
		info.dataPosition = stream->GetPosition();
		return info;
	}

	WavAudioStream::~WavAudioStream() {
		SPADES_MARK_FUNCTION();

		if (autoClose)
			delete stream;
	}

	uint64_t WavAudioStream::GetLength() { return dataChunk->length; }
	int WavAudioStream::GetSamplingFrequency() { return rate; }
	int WavAudioStream::GetNumChannels() { return channels; }

	WavAudioStream::SampleFormat WavAudioStream::GetSampleFormat() { return sampleFormat; }

	int WavAudioStream::ReadByte() {
		SPADES_MARK_FUNCTION();

		if (stream->GetPosition() >= endPos)
			return -1;
		else
			return stream->ReadByte();
	}

	size_t WavAudioStream::Read(void* data, size_t bytes) {
		SPADES_MARK_FUNCTION();

		uint64_t maxLen = endPos - stream->GetPosition();
		if ((uint64_t)bytes > maxLen)
			bytes = (size_t)maxLen;
		return stream->Read(data, bytes);
	}

	uint64_t WavAudioStream::GetPosition() { return stream->GetPosition() - startPos; }
	void WavAudioStream::SetPosition(uint64_t pos) { stream->SetPosition(pos + startPos); }
} // namespace spades