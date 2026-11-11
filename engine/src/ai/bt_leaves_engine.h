#pragma once

#include <functional>
#include <string>

#include "bt_instance.h"

namespace wiwa
{
	class NavModule;
	class Scene;

	// What the leaves that talk to the engine use. Set by the AIModule from the modules; a check sets what it wants to look at.
	struct BtEngineHooks
	{
		Scene* TheScene = nullptr;
		NavModule* Nav = nullptr;
		// Starts a clip on the entity's animator by name. False when there is no animator or no such clip.
		std::function<bool(entt::entity, const std::string&)> PlayClip{};
		// Whether the entity's animator is still starting or playing the clip it was asked for.
		std::function<bool(entt::entity)> ClipBusy{};
	};

	// MoveTo {target: [x, y, z] | "key:<blackboard key>" (vec3 or entity), stoppingDistance} and PlayAnimation {clip, wait}. Any other node type, and a call
	// without the hooks the node needs, answers Failure.
	//
	// MoveTo: Running while the NavAgent walks, Success on arrival (or within stoppingDistance), Failure when the target cannot be found (an entity that is
	// gone, a key that is empty), there is no NavAgent or no navmesh, no route leads there, or the agent was stopped by someone else. A target that is an
	// entity is followed: the destination is set again when it has moved more than a metre. Abort stops the agent. One log per node per run on a failure.
	BtStatus bt_engine_leaf(BtLeafCall& call, BtEngineHooks& hooks);
}
