#pragma once

#include "ILocalEntity.h"
#include "IRenderer.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;

		enum class SoundType { Movement, Action };

		class SoundIndicatorEntity : public ILocalEntity {
			Client& client;
			Vector3 origin;
			float lifetime;
			float maxRadius;
			float duration;
			SoundType type;

		public:
			SoundIndicatorEntity(Client& c, Vector3 pos, SoundType t);
			bool Update(float dt) override;
			void Render3D() override;
		};
	} // namespace client
} // namespace spades