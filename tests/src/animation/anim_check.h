#pragma once

#include <animation/animation_log.h>
#include <test_kit.h>

namespace wiwa
{
	// The check helper of the animation smoke tests: counts failures and logs each one.
	struct AnimCheckLog
	{
		static constexpr const char* Area = "Animation";
		static constexpr const char* Prefix = "Animation smoke test";
	};
	using AnimCheck = BasicCheck<AnimCheckLog>;

	// Checks of the pose math and the pose nodes (anim_pose_checks.cpp); they need no engine.
	void run_pose_checks(AnimCheck& check);
	// Checks of the components, the AnimatorInstance and the AnimationModule (anim_module_checks.cpp).
	void run_module_checks(AnimCheck& check);
	// Checks of animation graphs: the file, validation, the nodes, the instance, the edit model (anim_graph_checks.cpp).
	void run_graph_checks(AnimCheck& check);
	// Checks of the Lua API (anim_lua_checks.cpp).
	void run_lua_checks(AnimCheck& check);
}
