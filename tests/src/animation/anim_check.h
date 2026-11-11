#pragma once

#include <animation/animation_log.h>

namespace wiwa
{
	// The check helper of the animation smoke tests: counts failures and logs each one.
	struct AnimCheck
	{
		u32 Failures = 0;

		void operator()(bool condition, const std::string& what)
		{
			if (!condition)
			{
				Failures++;
				WI_ANIM_LOG(Error, "Animation smoke test FAILED: {0}", what);
			}
		}
	};

	// Checks of the pose math and the pose nodes (anim_pose_checks.cpp); they need no engine.
	void run_pose_checks(AnimCheck& check);
	// Checks of the components, the AnimatorInstance and the AnimationModule (anim_module_checks.cpp).
	void run_module_checks(AnimCheck& check);
	// Checks of animation graphs: the file, validation, the nodes, the instance, the edit model (anim_graph_checks.cpp).
	void run_graph_checks(AnimCheck& check);
	// Checks of the Lua API (anim_lua_checks.cpp).
	void run_lua_checks(AnimCheck& check);
}
