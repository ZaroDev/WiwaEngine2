#pragma once

#include "physics_test_rig.h"

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// The vehicle checks of --physics-smoke: the tire model, vehicles on the physics world, assists, and vehicles made from a scene.
	void run_vehicle_checks(physics_test::Check& check, AssetPipelineModule& pipeline);
}
