#pragma once

#include <unordered_map>

#include <entt/entt.hpp>

#include <scene/components.h>

#include "audio_backend.h"

namespace wiwa
{
	class Scene;

	// Keeps a backend in step with a scene: an emitter for each AudioEmitter entity (created when it appears, moved when it moves, destroyed
	// when the entity or the component goes), and the listener from the first active AudioListener (else the primary camera). Main thread. It
	// knows no vendor and no module, so a check can drive it with a Null backend and a scene alone.
	class AudioSync
	{
	public:
		// Sends what changed since the last call. An emitter seen for the first time with PlayOnStart posts its event once.
		void update(IAudioBackend& backend, Scene& scene);

		// Destroys every emitter and stops all sound. The next update starts over (PlayOnStart posts again).
		void stop(IAudioBackend& backend);

		// The emitter of an entity with an AudioEmitter, made now if the next update has not yet (a script posting in its first frame). Invalid when the
		// entity has no AudioEmitter or no WorldTransform.
		EmitterHandle ensure(IAudioBackend& backend, Scene& scene, entt::entity entity);

		// The backend's emitter for an entity; invalid when it has none.
		EmitterHandle emitter_of(entt::entity entity) const;
		u32 emitter_count() const { return static_cast<u32>(m_Emitters.size()); }

	private:
		EmitterHandle track(IAudioBackend& backend, Scene& scene, entt::entity entity, const AudioEmitter& emitter, const WorldTransform& world);

		struct Tracked
		{
			EmitterHandle Handle{};
			AudioTransform Transform{};
			float Scale = 1.0f;
		};

		std::unordered_map<entt::entity, Tracked> m_Emitters;
		AudioTransform m_Listener{};
		bool m_HasListener = false;
	};
}
