#include <pch.h>
#include "bindings.h"

#include <cmath>

#include <nav/nav_module.h>

namespace wiwa
{
	namespace
	{
		// A NavAgent as a script holds it: the entity, looked up again on every use.
		struct LuaNavAgent
		{
			EntityHandle Entity{};
		};

		NavModule& nav_of(ScriptContext& context)
		{
			if (context.Nav == nullptr)
			{
				throw sol::error("the navigation module is not available");
			}
			return *context.Nav;
		}

		// A vec3 argument. The scripting state has no sol2 type checks, so a number or a table where a vector goes must be refused here, not read as one.
		glm::vec3 vec3_arg(const sol::object& value, const char* function, const char* argument)
		{
			if (!value.is<LuaVec3>())
			{
				throw sol::error(std::string("Nav.") + function + ": " + argument + " must be a vec3");
			}
			const glm::vec3 vector = value.as<LuaVec3&>().Value;
			if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z))
			{
				throw sol::error(std::string("Nav.") + function + ": " + argument + " must be finite numbers (got " + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " + std::to_string(vector.z) + ")");
			}
			return vector;
		}

		float number_arg(const sol::object& value, const char* function, const char* argument)
		{
			if (!value.is<double>() || !std::isfinite(value.as<double>()))
			{
				throw sol::error(std::string("Nav.") + function + ": " + argument + " must be a finite number");
			}
			return static_cast<float>(value.as<double>());
		}

		u32 mask_of(const sol::object& mask)
		{
			if (!mask.is<double>() || !std::isfinite(mask.as<double>()) || mask.as<double>() < 0.0)
			{
				return 0xFFFFFFFFu;
			}
			return static_cast<u32>(static_cast<u64>(mask.as<double>()));
		}

		// `self` of an agent method: called with a dot instead of a colon it is not an agent, and sol2 (no safety defines) would read it unchecked.
		const LuaNavAgent& self_of(const sol::object& self)
		{
			if (!self.is<LuaNavAgent>())
			{
				throw sol::error("call agent methods with a colon: agent:state()");
			}
			return self.as<LuaNavAgent&>();
		}

		entt::entity agent_entity(ScriptContext& context, const sol::object& self)
		{
			const LuaNavAgent& agent = self_of(self);
			const entt::entity entity = agent.Entity.resolve();
			if (entity == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			if (context.ActiveScene == nullptr || !context.ActiveScene->registry().all_of<NavAgent>(entity))
			{
				throw sol::error("the entity has no NavAgent component");
			}
			return entity;
		}

		const char* state_name(NavAgentState state)
		{
			switch (state)
			{
			case NavAgentState::Moving: return "moving";
			case NavAgentState::Arrived: return "arrived";
			case NavAgentState::NoPath: return "no_path";
			default: return "idle";
			}
		}
	}

	void bind_nav(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaNavAgent>("NavAgent", sol::no_constructor,
			// Starts a walk to the nearest point of the navmesh to the destination; false if there is no navmesh, none near it, or no route.
			"set_destination", [&context](const sol::object& agent, const sol::object& destination) -> bool
			{
				const glm::vec3 target = vec3_arg(destination, "agent:set_destination", "the destination");
				return nav_of(context).set_destination(agent_entity(context, agent), target);
			},
			"stop", [&context](const sol::object& agent) { nav_of(context).stop(agent_entity(context, agent)); },
			"warp", [&context](const sol::object& agent, const sol::object& position) -> bool
			{
				const glm::vec3 target = vec3_arg(position, "agent:warp", "the position");
				return nav_of(context).warp(agent_entity(context, agent), target);
			},
			// "idle", "moving", "arrived" or "no_path".
			"state", [&context](const sol::object& agent) { return std::string(state_name(nav_of(context).state(agent_entity(context, agent)))); },
			"remaining_distance", [&context](const sol::object& agent) { return nav_of(context).remaining_distance(agent_entity(context, agent)); },
			"velocity", [&context](const sol::object& agent) { return LuaVec3{ nav_of(context).velocity(agent_entity(context, agent)), {} }; });

		sol::table nav = lua.create_table();

		// The handle of the NavAgent of an entity (an error when it has none).
		nav.set_function("agent", [&context](const sol::object& entity, sol::this_state state)
			{
				if (!entity.is<EntityHandle>())
				{
					throw sol::error("Nav.agent: the argument must be an entity");
				}
				LuaNavAgent agent{ entity.as<EntityHandle>() };
				agent_entity(context, sol::make_object(sol::state_view(state), agent));
				return agent;
			});

		// { status = "complete" | "partial" | "failed", length = metres, points = { vec3, ... } }. Partial: the walk ends where it comes closest to the
		// target. No navmesh, or no mesh near a point: failed, no points.
		nav.set_function("find_path", [&context](const sol::object& from, const sol::object& to, const sol::object& mask, sol::this_state state)
			{
				const glm::vec3 start = vec3_arg(from, "find_path", "from");
				const glm::vec3 end = vec3_arg(to, "find_path", "to");
				NavModule& module = nav_of(context);
				sol::state_view view(state);
				sol::table result = view.create_table();
				NavPath path;
				if (const NavQuery* query = module.query())
				{
					path = query->find_path(start, end, module.filter_for(mask_of(mask)));
				}
				result["status"] = path.Status == NavPathStatus::Complete ? "complete" : (path.Status == NavPathStatus::Partial ? "partial" : "failed");
				result["length"] = path.Length;
				sol::table points = view.create_table();
				int index = 1;
				for (const glm::vec3& corner : path.Corners)
				{
					points[index++] = LuaVec3{ corner, {} };
				}
				result["points"] = points;
				return result;
			});

		// The point of the navmesh nearest to the point within extents (default 2 x 4 x 2 m), or nil.
		nav.set_function("nearest_point", [&context](const sol::object& point, const sol::object& extents, sol::this_state state) -> sol::object
			{
				const glm::vec3 position = vec3_arg(point, "nearest_point", "the point");
				const glm::vec3 reach = extents.valid() && extents.get_type() != sol::type::lua_nil ? vec3_arg(extents, "nearest_point", "the extents") : glm::vec3(2.0f, 4.0f, 2.0f);
				NavModule& module = nav_of(context);
				sol::state_view view(state);
				if (const NavQuery* query = module.query())
				{
					if (const std::optional<glm::vec3> nearest = query->nearest_point(position, reach, module.filter_for(0xFFFFFFFFu)))
					{
						return sol::make_object(view, LuaVec3{ *nearest, {} });
					}
				}
				return sol::object(view, sol::lua_nil);
			});

		// hit, point: whether the line from a to b leaves the navmesh (a wall or an edge) on the way, and where.
		nav.set_function("raycast", [&context](const sol::object& a, const sol::object& b, sol::this_state state) -> std::tuple<bool, sol::object>
			{
				const glm::vec3 start = vec3_arg(a, "raycast", "a");
				const glm::vec3 end = vec3_arg(b, "raycast", "b");
				NavModule& module = nav_of(context);
				sol::state_view view(state);
				if (const NavQuery* query = module.query())
				{
					glm::vec3 hit{ 0.0f };
					if (query->raycast(start, end, module.filter_for(0xFFFFFFFFu), &hit))
					{
						return { true, sol::make_object(view, LuaVec3{ hit, {} }) };
					}
				}
				return { false, sol::object(view, sol::lua_nil) };
			});

		// A random point of the navmesh within radius of the centre (which must be on it), or nil.
		nav.set_function("random_point", [&context](const sol::object& centre, const sol::object& radius, sol::this_state state) -> sol::object
			{
				const glm::vec3 position = vec3_arg(centre, "random_point", "the centre");
				const float distance = number_arg(radius, "random_point", "the radius");
				NavModule& module = nav_of(context);
				sol::state_view view(state);
				if (const NavQuery* query = module.query())
				{
					if (const std::optional<glm::vec3> point = query->random_point_around(position, distance, module.filter_for(0xFFFFFFFFu)))
					{
						return sol::make_object(view, LuaVec3{ *point, {} });
					}
				}
				return sol::object(view, sol::lua_nil);
			});

		// Whether the navmesh is there (loaded) for the scene that plays.
		nav.set_function("has_navmesh", [&context]() { return nav_of(context).has_navmesh(); });

		runtime.sandbox()["Nav"] = nav;
	}
}
