#pragma once

#include <core/module.h>

#include "physics_thread.h"

namespace wiwa
{
	// Owns the physics thread and its command and snapshot channels. A world exists only while a scene plays: start_world when play
	// begins, stop_world when it ends (Play, Stop, Play again builds a fresh world each time). Used by the game too, so nothing here
	// depends on editor code.
	class PhysicsModule : public IModule
	{
	public:
		PhysicsModule() = default;
		virtual ~PhysicsModule() override = default;

		virtual void shutdown() override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "PhysicsModule"; }
		virtual int get_priority() const override { return -20; } // after the scene (-80), before the UI (-10) and the scripts (0)

		PhysicsThread& thread() { return m_Thread; }
		bool running() const { return m_Thread.is_running(); }

		// Main thread. Builds a new, empty world (a running one is stopped first).
		void start_world(const PhysicsSettings& settings);
		// Main thread. Stops the thread and frees the world. Does nothing when no world runs.
		void stop_world();

	private:
		PhysicsThread m_Thread{};
	};
}
