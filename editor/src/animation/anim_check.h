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
}
