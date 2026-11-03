#include <pch.h>
#include "physics_module.h"

namespace wiwa
{
	void PhysicsModule::shutdown()
	{
		stop_world();
	}

	void PhysicsModule::start_world(const PhysicsSettings& settings)
	{
		m_Thread.start(settings);
	}

	void PhysicsModule::stop_world()
	{
		m_Thread.stop();
	}
}
