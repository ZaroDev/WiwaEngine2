#pragma once

namespace wiwa
{
	// wiwa_editor --physics-smoke: checks the physics thread and module (fixed-rate stepping, snapshots, commands, start and stop) and
	// bodies made from an in-memory scene through PhysicsSync (a box dropped on the ground rests, a stack sleeps, compounds, kinematic
	// bodies, destroyed entities, no stale bodies after Play, Stop, Play).
	// Returns true if everything held (and logs "Physics smoke test passed").
	bool run_physics_smoke_test();
}
