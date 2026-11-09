#pragma once

#include <entt/entt.hpp>

namespace wiwa
{
	// Whether the preview of an Animator entity goes on: it ends when the selection moves to another entity (or to none), and when the game plays.
	inline bool preview_continues(entt::entity previewed, entt::entity selected, bool editing)
	{
		return previewed != entt::null && previewed == selected && editing;
	}
}
