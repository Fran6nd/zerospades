/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#include "VoxelModel2KV6.h"

#include <cstring>
#include <algorithm>

#include "Debug.h"
#include "Exception.h"
#include "IStream.h"

namespace spades {

	std::vector<VoxelObject> VoxelModel2KV6::Load(IStream& stream) {
		SPADES_MARK_FUNCTION();

		// Read and verify magic
		std::string magic = stream.Read(4);
		if (magic != MAGIC)
			SPRaise("Invalid .2kv6 magic: expected '2kv6', got '%s'", magic.c_str());

		// Read version
		uint16_t version;
		if (stream.Read(&version, sizeof(version)) < sizeof(version))
			SPRaise(".2kv6 file truncated: failed to read version");

		if (version != FORMAT_VERSION)
			SPRaise("Unsupported .2kv6 format version: %u (expected %u)", version,
			        FORMAT_VERSION);

		// Read number of objects
		uint16_t numObjects;
		if (stream.Read(&numObjects, sizeof(numObjects)) < sizeof(numObjects))
			SPRaise(".2kv6 file truncated: failed to read object count");

		std::vector<VoxelObject> objects;
		objects.reserve(numObjects);

		for (uint16_t i = 0; i < numObjects; i++) {
			VoxelObject obj;

			// Read object name
			uint16_t nameLen;
			if (stream.Read(&nameLen, sizeof(nameLen)) < sizeof(nameLen))
				SPRaise(".2kv6 file truncated: failed to read name length for object %u", i);

			if (nameLen > 0) {
				std::string name(nameLen, '\0');
				if (stream.Read(&name[0], nameLen) < nameLen)
					SPRaise(".2kv6 file truncated: failed to read name for object %u", i);
				obj.name = name;
			}

			// Read static transform
			float posData[3], rotData[4], scaleData[3];

			if (stream.Read(posData, sizeof(posData)) < sizeof(posData))
				SPRaise(".2kv6 file truncated: failed to read position for object %u", i);
			obj.position = MakeVector3(posData[0], posData[1], posData[2]);

			if (stream.Read(rotData, sizeof(rotData)) < sizeof(rotData))
				SPRaise(".2kv6 file truncated: failed to read rotation for object %u", i);
			obj.rotation = MakeVector4(rotData[0], rotData[1], rotData[2], rotData[3]);

			if (stream.Read(scaleData, sizeof(scaleData)) < sizeof(scaleData))
				SPRaise(".2kv6 file truncated: failed to read scale for object %u", i);
			obj.scale = MakeVector3(scaleData[0], scaleData[1], scaleData[2]);

			// Read animation keyframes
			uint16_t numKeyframes;
			if (stream.Read(&numKeyframes, sizeof(numKeyframes)) < sizeof(numKeyframes))
				SPRaise(".2kv6 file truncated: failed to read keyframe count for object %u", i);

			obj.keyframes.reserve(numKeyframes);
			for (uint16_t k = 0; k < numKeyframes; k++) {
				TransformKeyframe kf;

				if (stream.Read(&kf.time, sizeof(kf.time)) < sizeof(kf.time))
					SPRaise(".2kv6 file truncated: failed to read keyframe time for object %u "
					        "keyframe %u",
					        i, k);

				float kfPosData[3], kfRotData[4], kfScaleData[3];

				if (stream.Read(kfPosData, sizeof(kfPosData)) < sizeof(kfPosData))
					SPRaise(".2kv6 file truncated: failed to read keyframe position for object "
					        "%u keyframe %u",
					        i, k);
				kf.position = MakeVector3(kfPosData[0], kfPosData[1], kfPosData[2]);

				if (stream.Read(kfRotData, sizeof(kfRotData)) < sizeof(kfRotData))
					SPRaise(".2kv6 file truncated: failed to read keyframe rotation for object "
					        "%u keyframe %u",
					        i, k);
				kf.rotation = MakeVector4(kfRotData[0], kfRotData[1], kfRotData[2], kfRotData[3]);

				if (stream.Read(kfScaleData, sizeof(kfScaleData)) < sizeof(kfScaleData))
					SPRaise(".2kv6 file truncated: failed to read keyframe scale for object %u "
					        "keyframe %u",
					        i, k);
				kf.scale = MakeVector3(kfScaleData[0], kfScaleData[1], kfScaleData[2]);

				obj.keyframes.push_back(kf);
			}

			// Load embedded KV6 data
			try {
				obj.model = VoxelModel::LoadKV6(stream);
			} catch (const std::exception& e) {
				SPRaise("Failed to load KV6 data for object %u: %s", i, e.what());
			}

			objects.push_back(obj);
		}

		return objects;
	}

	void VoxelModel2KV6::Save(IStream& stream, const std::vector<VoxelObject>& objects) {
		SPADES_MARK_FUNCTION();

		if (objects.empty())
			SPRaise("Cannot save empty .2kv6 scene");

		if (objects.size() > UINT16_MAX)
			SPRaise("Too many objects in .2kv6 scene: %zu (max %u)", objects.size(), UINT16_MAX);

		// Write magic
		stream.Write(MAGIC, 4);

		// Write version
		uint16_t version = FORMAT_VERSION;
		stream.Write(&version, sizeof(version));

		// Write number of objects
		uint16_t numObjects = static_cast<uint16_t>(objects.size());
		stream.Write(&numObjects, sizeof(numObjects));

		for (const VoxelObject& obj : objects) {
			// Write object name
			uint16_t nameLen = static_cast<uint16_t>(obj.name.length());
			if (nameLen > UINT16_MAX)
				SPRaise("Object name too long: %zu characters", obj.name.length());

			stream.Write(&nameLen, sizeof(nameLen));
			if (nameLen > 0)
				stream.Write(obj.name.data(), nameLen);

			// Write static transform
			float posData[3] = {obj.position.x, obj.position.y, obj.position.z};
			float rotData[4] = {obj.rotation.x, obj.rotation.y, obj.rotation.z, obj.rotation.w};
			float scaleData[3] = {obj.scale.x, obj.scale.y, obj.scale.z};

			stream.Write(posData, sizeof(posData));
			stream.Write(rotData, sizeof(rotData));
			stream.Write(scaleData, sizeof(scaleData));

			// Write animation keyframes
			uint16_t numKeyframes = static_cast<uint16_t>(obj.keyframes.size());
			stream.Write(&numKeyframes, sizeof(numKeyframes));

			for (const TransformKeyframe& kf : obj.keyframes) {
				stream.Write(&kf.time, sizeof(kf.time));

				float kfPosData[3] = {kf.position.x, kf.position.y, kf.position.z};
				float kfRotData[4] = {kf.rotation.x, kf.rotation.y, kf.rotation.z, kf.rotation.w};
				float kfScaleData[3] = {kf.scale.x, kf.scale.y, kf.scale.z};

				stream.Write(kfPosData, sizeof(kfPosData));
				stream.Write(kfRotData, sizeof(kfRotData));
				stream.Write(kfScaleData, sizeof(kfScaleData));
			}

			// Write embedded KV6 data (use existing SaveKV6)
			if (!obj.model)
				SPRaise("Object '%s' has no voxel model", obj.name.c_str());

			obj.model->SaveKV6(stream);
		}

		stream.Flush();
	}

} // namespace spades
