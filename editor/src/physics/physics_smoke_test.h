#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --physics-smoke: checks the physics thread and module (fixed-rate stepping, snapshots, commands, start and stop) and
	// bodies made from an in-memory scene through PhysicsSync (a box dropped on the ground rests, a stack sleeps, compounds, kinematic
	// bodies, destroyed entities, no stale bodies after Play, Stop, Play), and physics materials (.wphysmat imported through the pipeline:
	// restitution decides the bounce, a material that arrives while a body falls applies, a missing one uses the default).
	// Returns true if everything held (and logs "Physics smoke test passed").
	bool run_physics_smoke_test(AssetPipelineModule& pipeline);
}
