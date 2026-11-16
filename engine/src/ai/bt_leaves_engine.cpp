#include <pch.h>
#include "bt_leaves_engine.h"

#include <cmath>

#include <nav/nav_module.h>
#include <scene/components.h>
#include <scene/scene.h>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		// Stops the agent only if this node started the walk it is on: another branch may have sent it elsewhere since (a reactive switch).
		void stop_if_owner(BtLeafCall& call, NavModule& nav)
		{
			if (call.Instance->move_owner() == call.NodeIndex)
			{
				nav.stop(call.Self);
				call.Instance->set_move_owner(BtInstance::NO_OWNER);
			}
		}

		void fail(BtLeafCall& call, const std::string& why)
		{
			if (call.Instance->warn_once(call.NodeIndex))
			{
				WI_AI_LOG(Warning, "Behaviour tree: {0} node {1} failed: {2}", call.Node->Type, call.Node->Id, why);
			}
		}

		// The point a MoveTo goes to. False, and why, when there is none.
		bool resolve_target(BtLeafCall& call, BtEngineHooks& hooks, glm::vec3& out, bool& followsEntity, std::string& why)
		{
			followsEntity = false;
			const nlohmann::json& target = call.Node->Params.contains("target") ? call.Node->Params["target"] : nlohmann::json();
			if (target.is_array() && target.size() == 3)
			{
				out = glm::vec3(target[0].get<float>(), target[1].get<float>(), target[2].get<float>());
			}
			else if (target.is_string() && target.get<std::string>().rfind("key:", 0) == 0)
			{
				const std::string name = target.get<std::string>().substr(4);
				const std::optional<BtValue> value = call.Instance->blackboard().get(name);
				if (!value)
				{
					why = "the blackboard has no key \"" + name + "\"";
					return false;
				}
				if (const glm::vec3* point = std::get_if<glm::vec3>(&*value))
				{
					out = *point;
				}
				else if (const EntityGuid* guid = std::get_if<EntityGuid>(&*value))
				{
					if (!guid->valid())
					{
						why = "the entity key \"" + name + "\" is empty";
						return false;
					}
					const entt::entity entity = hooks.TheScene != nullptr ? hooks.TheScene->find(*guid) : entt::null;
					const WorldTransform* world = entity != entt::null ? hooks.TheScene->registry().try_get<WorldTransform>(entity) : nullptr;
					if (world == nullptr)
					{
						why = "the entity of \"" + name + "\" is gone";
						return false;
					}
					out = glm::vec3(world->Matrix[3]);
					followsEntity = true;
				}
				else
				{
					why = "the key \"" + name + "\" is not a vec3 or an entity";
					return false;
				}
			}
			else
			{
				why = "the target is neither a point nor a key";
				return false;
			}
			if (!finite(out))
			{
				why = "the target is not a finite point";
				return false;
			}
			return true;
		}

		BtStatus move_to(BtLeafCall& call, BtEngineHooks& hooks)
		{
			NavModule* nav = hooks.Nav;
			if (call.Phase == BtLeafPhase::Abort)
			{
				if (nav != nullptr && call.Instance->move_owner() == call.NodeIndex)
				{
					nav->stop(call.Self);
					call.Instance->set_move_owner(BtInstance::NO_OWNER);
				}
				return BtStatus::Failure;
			}
			if (nav == nullptr || hooks.TheScene == nullptr)
			{
				fail(call, "there is no navigation module");
				return BtStatus::Failure;
			}
			if (!hooks.TheScene->registry().valid(call.Self) || !hooks.TheScene->registry().all_of<NavAgent, WorldTransform>(call.Self))
			{
				fail(call, "the entity has no NavAgent");
				return BtStatus::Failure;
			}
			if (!nav->has_navmesh() && !nav->loading())
			{
				fail(call, "the scene has no navmesh");
				return BtStatus::Failure;
			}

			glm::vec3 target{ 0.0f };
			bool follows = false;
			std::string why;
			if (!resolve_target(call, hooks, target, follows, why))
			{
				stop_if_owner(call, *nav);
				fail(call, why);
				return BtStatus::Failure;
			}

			BtNodeState& state = call.Instance->state(call.NodeIndex);
			const double stopping = call.Node->Params.value("stoppingDistance", 0.0);
			const glm::vec3 position(hooks.TheScene->registry().get<WorldTransform>(call.Self).Matrix[3]);
			if (stopping > 0.0 && glm::distance(position, target) <= stopping)
			{
				stop_if_owner(call, *nav);
				return BtStatus::Success;
			}

			if (call.Phase == BtLeafPhase::Start || (follows && glm::distance(state.Anchor, target) > 1.0f))
			{
				if (!nav->set_destination(call.Self, target))
				{
					stop_if_owner(call, *nav);
					fail(call, "no route leads to the target");
					return BtStatus::Failure;
				}
				state.Anchor = target;
				call.Instance->set_move_owner(call.NodeIndex);
				return BtStatus::Running;
			}

			switch (nav->state(call.Self))
			{
			case NavAgentState::Arrived: return BtStatus::Success;
			case NavAgentState::NoPath:
				fail(call, "the route ends short of the target");
				return BtStatus::Failure;
			case NavAgentState::Moving: break;
			case NavAgentState::Idle:
				if (!nav->loading())
				{
					fail(call, "the agent was stopped");
					return BtStatus::Failure;
				}
				break;
			}
			if (stopping > 0.0 && nav->remaining_distance(call.Self) > 0.0f && nav->remaining_distance(call.Self) <= stopping)
			{
				stop_if_owner(call, *nav);
				return BtStatus::Success;
			}
			return BtStatus::Running;
		}

		BtStatus play_animation(BtLeafCall& call, BtEngineHooks& hooks)
		{
			if (call.Phase == BtLeafPhase::Abort)
			{
				return BtStatus::Failure;
			}
			const bool wait = call.Node->Params.value("wait", false);
			if (call.Phase == BtLeafPhase::Start)
			{
				const std::string clip = call.Node->Params.value("clip", std::string());
				if (!hooks.PlayClip || !hooks.PlayClip(call.Self, clip))
				{
					fail(call, "the entity has no animator with a clip named \"" + clip + "\"");
					return BtStatus::Failure;
				}
				return wait ? BtStatus::Running : BtStatus::Success;
			}
			return hooks.ClipBusy && hooks.ClipBusy(call.Self) ? BtStatus::Running : BtStatus::Success;
		}
	}

	BtStatus bt_engine_leaf(BtLeafCall& call, BtEngineHooks& hooks)
	{
		if (call.Node->Type == "MoveTo")
		{
			return move_to(call, hooks);
		}
		if (call.Node->Type == "PlayAnimation")
		{
			return play_animation(call, hooks);
		}
		return BtStatus::Failure;
	}
}
