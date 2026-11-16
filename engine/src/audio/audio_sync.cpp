#include <pch.h>
#include "audio_sync.h"

#include <scene/scene.h>

namespace wiwa
{
	namespace
	{
		AudioVec3 to_audio(const glm::vec3& v)
		{
			return { v.x, v.y, v.z };
		}

		// A world matrix as a position and a facing: forward is -Z (as the camera looks), up is +Y; scale in the matrix is divided out.
		AudioTransform transform_of(const glm::mat4& world)
		{
			AudioTransform transform;
			transform.Position = to_audio(glm::vec3(world[3]));

			const glm::vec3 z = glm::vec3(world[2]);
			const glm::vec3 y = glm::vec3(world[1]);
			const float zLength = glm::length(z);
			const float yLength = glm::length(y);
			if (zLength > 1e-6f)
			{
				transform.Forward = to_audio(-z / zLength);
			}
			if (yLength > 1e-6f)
			{
				transform.Up = to_audio(y / yLength);
			}
			return transform;
		}
	}

	void AudioSync::update(IAudioBackend& backend, Scene& scene)
	{
		entt::registry& registry = scene.registry();

		// Emitters of entities (or components) that went away.
		for (auto it = m_Emitters.begin(); it != m_Emitters.end();)
		{
			if (!registry.valid(it->first) || !registry.all_of<AudioEmitter>(it->first))
			{
				backend.destroy_emitter(it->second.Handle);
				it = m_Emitters.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (auto [entity, emitter, world] : registry.view<AudioEmitter, WorldTransform>().each())
		{
			const AudioTransform transform = transform_of(world.Matrix);
			auto found = m_Emitters.find(entity);
			if (found == m_Emitters.end())
			{
				track(backend, scene, entity, emitter, world);
				continue;
			}

			Tracked& tracked = found->second;
			if (!(tracked.Transform == transform))
			{
				tracked.Transform = transform;
				backend.set_emitter(tracked.Handle, transform);
			}
			if (tracked.Scale != emitter.AttenuationScale)
			{
				tracked.Scale = emitter.AttenuationScale;
				backend.set_emitter_scale(tracked.Handle, tracked.Scale);
			}
		}

		// The listener: the first active AudioListener, else the primary camera.
		const glm::mat4* listenerWorld = nullptr;
		for (auto [entity, listener, world] : registry.view<AudioListener, WorldTransform>().each())
		{
			if (listener.Active)
			{
				listenerWorld = &world.Matrix;
				break;
			}
		}
		if (listenerWorld == nullptr)
		{
			for (auto [entity, camera, world] : registry.view<Camera, WorldTransform>().each())
			{
				if (camera.Primary)
				{
					listenerWorld = &world.Matrix;
					break;
				}
			}
		}
		if (listenerWorld != nullptr)
		{
			const AudioTransform transform = transform_of(*listenerWorld);
			if (!m_HasListener || !(m_Listener == transform))
			{
				m_Listener = transform;
				m_HasListener = true;
				backend.set_listener(transform);
			}
		}
	}

	EmitterHandle AudioSync::track(IAudioBackend& backend, Scene& scene, entt::entity entity, const AudioEmitter& emitter, const WorldTransform& world)
	{
		const Name* name = scene.registry().try_get<Name>(entity);
		Tracked tracked;
		tracked.Handle = backend.create_emitter(name ? std::string_view(name->Value) : std::string_view("Emitter"));
		tracked.Transform = transform_of(world.Matrix);
		tracked.Scale = emitter.AttenuationScale;
		backend.set_emitter(tracked.Handle, tracked.Transform);
		backend.set_emitter_scale(tracked.Handle, tracked.Scale);
		if (emitter.PlayOnStart && !emitter.Event.empty())
		{
			backend.post_event(emitter.Event, tracked.Handle);
		}
		m_Emitters.emplace(entity, tracked);
		return tracked.Handle;
	}

	EmitterHandle AudioSync::ensure(IAudioBackend& backend, Scene& scene, entt::entity entity)
	{
		if (const EmitterHandle existing = emitter_of(entity); existing.valid())
		{
			return existing;
		}
		entt::registry& registry = scene.registry();
		if (!registry.valid(entity))
		{
			return {};
		}
		const AudioEmitter* emitter = registry.try_get<AudioEmitter>(entity);
		const WorldTransform* world = registry.try_get<WorldTransform>(entity);
		return emitter && world ? track(backend, scene, entity, *emitter, *world) : EmitterHandle{};
	}

	void AudioSync::stop(IAudioBackend& backend)
	{
		for (const auto& [entity, tracked] : m_Emitters)
		{
			backend.destroy_emitter(tracked.Handle);
		}
		m_Emitters.clear();
		m_HasListener = false;
		backend.stop_all();
	}

	EmitterHandle AudioSync::emitter_of(entt::entity entity) const
	{
		const auto found = m_Emitters.find(entity);
		return found != m_Emitters.end() ? found->second.Handle : EmitterHandle{};
	}
}
