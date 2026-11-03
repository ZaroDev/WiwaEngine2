#pragma once

namespace wiwa
{
	// wiwa_editor --physics-smoke: checks the physics thread and module (fixed-rate stepping, snapshots, commands, start and stop).
	// Returns true if everything held (and logs "Physics smoke test passed").
	bool run_physics_smoke_test();
}
