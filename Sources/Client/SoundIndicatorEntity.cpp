#include "SoundIndicatorEntity.h"
#include "Client.h"

namespace spades {
	namespace client {
		SoundIndicatorEntity::SoundIndicatorEntity(Client& c, Vector3 pos, SoundType t)
			: client(c), origin(pos), lifetime(1.0F), type(t) {
			switch (t) {
				case SoundType::Movement: maxRadius = 1.7F; duration = 1.5F; break;
				case SoundType::Action: maxRadius = 2.5F; duration = 0.8F; break;
			}
		}

		bool SoundIndicatorEntity::Update(float dt) {
			lifetime -= dt / duration;
			return lifetime > 0.0F;
		}

		void SoundIndicatorEntity::Render3D() {
			float t = 1.0F - lifetime;
			float radius = maxRadius * t;
			float ringZ = origin.z;

			// adjust footsteps position
			bool isMovementSound = type == SoundType::Movement;
			if (isMovementSound)
				ringZ += 1.0F;

			IRenderer& r = client.GetRenderer();

			float a = lifetime * lifetime;
			Vector4 col = isMovementSound
				? MakeVector4(0.2F, 0.8F, 1.0F, a)
				: MakeVector4(1.0F, 0.8F, 0.2F, a);

			const int numSegments = 16;
			for (int i = 0; i < numSegments; i++) {
				float ang1 = (float)i / numSegments * 2.0F * M_PI_F;
				float ang2 = (float)(i + 1) / numSegments * 2.0F * M_PI_F;
				r.AddDebugLine(
					MakeVector3(origin.x + cosf(ang1) * radius, origin.y + sinf(ang1) * radius, ringZ),
					MakeVector3(origin.x + cosf(ang2) * radius, origin.y + sinf(ang2) * radius, ringZ),
					col
				);
			}
		}
	} // namespace client
} // namespace spades