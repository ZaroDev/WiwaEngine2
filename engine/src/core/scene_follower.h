#pragma once
#include <memory>

#include <core/common.h>

namespace wiwa
{
	class Scene;

	// The "follow the active scene" state of a module that has a world while a scene plays (NavModule, AIModule, AudioModule). It holds the scene the
	// world is for (weakly) and decides when the world ends and begins; the module keeps what the world is made of and its own start_world / stop_world.
	//
	// With auto start (the default: a game, the editor's play mode) the world follows the SceneModule's active scene while the game clock plays and ends
	// when another scene replaces it. Without (checks) start_world / stop_world drive it and the active scene is ignored; a world still ends when its
	// scene is gone.
	class SceneFollower
	{
	public:
		void set_auto_start(bool autoStart) { m_AutoStart = autoStart; }
		bool auto_start() const { return m_AutoStart; }

		// True from bind to release, even when the scene has since been destroyed (scene() is then null).
		bool has_world() const { return m_HasWorld; }
		Ref<Scene> scene() const { return m_HasWorld ? m_World.lock() : nullptr; }

		void bind(const Ref<Scene>& scene)
		{
			m_World = scene;
			m_HasWorld = true;
		}
		void release()
		{
			m_World.reset();
			m_HasWorld = false;
		}

		// One frame of the lifecycle: ends the world (stop()) when its scene is gone or, with auto start, is not the active one; then, with auto start,
		// starts one (start(active)) for the active scene while the game plays and there is none. canStart lets a module hold the start back.
		template <typename StopFn, typename StartFn>
		void follow(const Ref<Scene>& active, bool playing, StopFn&& stop, StartFn&& start, bool canStart = true)
		{
			if (m_HasWorld)
			{
				const Ref<Scene> current = m_World.lock();
				if (!current || (m_AutoStart && current != active))
				{
					stop(); // the scene was replaced or is gone
				}
			}
			if (m_AutoStart && !m_HasWorld && playing && active != nullptr && canStart)
			{
				start(active);
			}
		}

	private:
		std::weak_ptr<Scene> m_World{};
		bool m_HasWorld = false;
		bool m_AutoStart = true;
	};
}
