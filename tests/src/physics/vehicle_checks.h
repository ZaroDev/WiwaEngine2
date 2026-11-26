#pragma once

#include "physics_test_rig.h"

namespace wiwa
{
	// The vehicle checks of --physics-smoke: the tire model, vehicles on the physics world, assists, and vehicles made from a scene.
	void run_vehicle_checks(physics_test::Check& check);
}
