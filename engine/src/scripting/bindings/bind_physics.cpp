#include <pch.h>
#include "bindings.h"

#include <physics/physics_module.h>
#include <physics/query_snapshot.h>

namespace wiwa
{
	namespace
	{
		// A rigid body as a script holds it: `entity:body()`. Commands go to the physics world and take effect in the next step.
		struct LuaBody
		{
			EntityHandle Handle{};
		};

		PhysicsModule& physics_of(ScriptContext& context)
		{
			if (context.Physics == nullptr)
			{
				throw sol::error("there is no physics module");
			}
			return *context.Physics;
		}

		PhysicsModule& physics_of(const LuaBody& body)
		{
			if (body.Handle.Context == nullptr)
			{
				throw sol::error("the body has no script context");
			}
			if (!body.Handle.is_valid())
			{
				throw sol::error("the entity no longer exists");
			}
			return physics_of(*body.Handle.Context);
		}

		sol::table hit_table(sol::state_view lua, ScriptContext& context, const RayHit& hit)
		{
			sol::table result = lua.create_table();
			result["entity"] = EntityHandle{ hit.Key, &context };
			result["point"] = LuaVec3{ hit.Point, {} };
			result["normal"] = LuaVec3{ hit.Normal, {} };
			result["distance"] = hit.Distance;
			return result;
		}

		u32 mask_of(const sol::optional<double>& mask)
		{
			return mask ? static_cast<u32>(static_cast<u64>(std::max(0.0, *mask))) : ~0u;
		}

		// { kind = "sphere", radius = r }, { kind = "box", half_extents = vec3 }, { kind = "capsule", radius = r, half_height = h }
		ShapeDesc shape_of(const sol::table& table)
		{
			const std::string kind = table.get_or<std::string>("kind", "");
			ShapeDesc shape;
			if (kind == "sphere")
			{
				shape.Kind = ShapeKind::Sphere;
				shape.Size = glm::vec3(table.get_or("radius", 0.5f));
			}
			else if (kind == "box")
			{
				const sol::optional<LuaVec3> half = table["half_extents"];
				if (!half)
				{
					throw sol::error("shape_cast: a box needs half_extents");
				}
				shape.Kind = ShapeKind::Box;
				shape.Size = half->Value;
			}
			else if (kind == "capsule")
			{
				shape.Kind = ShapeKind::Capsule;
				shape.Size = glm::vec3(table.get_or("radius", 0.5f), table.get_or("half_height", 0.5f), 0.0f);
			}
			else
			{
				throw sol::error("shape_cast: kind must be \"sphere\", \"box\" or \"capsule\"");
			}
			return shape;
		}
	}

	void bind_physics(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaBody>("Body", sol::no_constructor,
			"add_force", [](const LuaBody& body, const LuaVec3& force) { physics_of(body).add_force(body.Handle.Guid, force.Value); },
			"add_impulse", [](const LuaBody& body, const LuaVec3& impulse) { physics_of(body).add_impulse(body.Handle.Guid, impulse.Value); },
			"add_torque", [](const LuaBody& body, const LuaVec3& torque) { physics_of(body).add_torque(body.Handle.Guid, torque.Value); },
			"set_velocity", [](const LuaBody& body, const LuaVec3& velocity) { physics_of(body).set_velocity(body.Handle.Guid, velocity.Value); },
			"set_angular_velocity", [](const LuaBody& body, const LuaVec3& angular) { physics_of(body).set_angular_velocity(body.Handle.Guid, angular.Value); },
			"get_velocity", [](const LuaBody& body, sol::this_state state) -> sol::object
			{
				const std::optional<glm::vec3> velocity = physics_of(body).velocity(body.Handle.Guid);
				return velocity ? sol::make_object(state, LuaVec3{ *velocity, {} }) : sol::object(sol::state_view(state), sol::lua_nil);
			},
			"get_angular_velocity", [](const LuaBody& body, sol::this_state state) -> sol::object
			{
				const std::optional<glm::vec3> angular = physics_of(body).angular_velocity(body.Handle.Guid);
				return angular ? sol::make_object(state, LuaVec3{ *angular, {} }) : sol::object(sol::state_view(state), sol::lua_nil);
			},
			"set_kinematic_target", [](const LuaBody& body, const LuaVec3& position, sol::optional<LuaQuat> rotation)
			{
				PhysicsModule& physics = physics_of(body);
				glm::quat turn = rotation ? rotation->Value : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				if (!rotation)
				{
					// without a rotation the body keeps the one it has
					const Scene* scene = body.Handle.Context->ActiveScene;
					const entt::entity entity = body.Handle.resolve();
					if (scene != nullptr && entity != entt::null)
					{
						turn = glm::normalize(glm::quat_cast(glm::mat3(scene->registry().get<WorldTransform>(entity).Matrix)));
					}
				}
				physics.set_kinematic_target(body.Handle.Guid, position.Value, turn);
			},
			"wake", [](const LuaBody& body) { physics_of(body).wake(body.Handle.Guid); });

		// entity:body()
		sol::table entityType = lua["Entity"];
		entityType["body"] = [](const EntityHandle& handle) -> LuaBody
		{
			if (!handle.is_valid())
			{
				throw sol::error("the entity no longer exists");
			}
			return LuaBody{ handle };
		};

		sol::table physics = lua.create_table();
		const auto query = [&context]() { return physics_of(context).query(); };

		// physics.raycast(origin, direction, max_distance, layer_mask): the nearest hit { entity, point, normal, distance } or nil.
		physics.set_function("raycast", [&context, query](const LuaVec3& origin, const LuaVec3& direction, float maxDistance, sol::optional<double> mask, sol::this_state state) -> sol::object
		{
			sol::state_view view(state);
			const Ref<const QuerySnapshot> snapshot = query();
			const std::optional<RayHit> hit = snapshot ? snapshot->raycast(origin.Value, direction.Value, maxDistance, mask_of(mask)) : std::nullopt;
			return hit ? sol::object(hit_table(view, context, *hit)) : sol::object(view, sol::lua_nil);
		});
		// every hit, nearest first, as an array of the same tables
		physics.set_function("raycast_all", [&context, query](const LuaVec3& origin, const LuaVec3& direction, float maxDistance, sol::optional<double> mask, sol::this_state state)
		{
			sol::state_view view(state);
			sol::table result = view.create_table();
			if (const Ref<const QuerySnapshot> snapshot = query())
			{
				int index = 1;
				for (const RayHit& hit : snapshot->raycast_all(origin.Value, direction.Value, maxDistance, mask_of(mask)))
				{
					result[index++] = hit_table(view, context, hit);
				}
			}
			return result;
		});
		// physics.overlap_sphere(center, radius, layer_mask) / overlap_box(center, half_extents, rotation, layer_mask): an array of entities
		physics.set_function("overlap_sphere", [&context, query](const LuaVec3& center, float radius, sol::optional<double> mask, sol::this_state state)
		{
			sol::state_view view(state);
			sol::table result = view.create_table();
			if (const Ref<const QuerySnapshot> snapshot = query())
			{
				int index = 1;
				for (const BodyKey& key : snapshot->overlap_sphere(center.Value, radius, mask_of(mask)))
				{
					result[index++] = EntityHandle{ key, &context };
				}
			}
			return result;
		});
		physics.set_function("overlap_box", [&context, query](const LuaVec3& center, const LuaVec3& halfExtents, sol::optional<LuaQuat> rotation, sol::optional<double> mask, sol::this_state state)
		{
			sol::state_view view(state);
			sol::table result = view.create_table();
			if (const Ref<const QuerySnapshot> snapshot = query())
			{
				int index = 1;
				for (const BodyKey& key : snapshot->overlap_box(center.Value, halfExtents.Value, rotation ? rotation->Value : glm::quat(1.0f, 0.0f, 0.0f, 0.0f), mask_of(mask)))
				{
					result[index++] = EntityHandle{ key, &context };
				}
			}
			return result;
		});
		// physics.shape_cast(shape, from, to, layer_mask): like raycast, with a { kind = ... } shape swept from `from` to `to`
		physics.set_function("shape_cast", [&context, query](const sol::table& shape, const LuaVec3& from, const LuaVec3& to, sol::optional<double> mask, sol::this_state state) -> sol::object
		{
			sol::state_view view(state);
			const ShapeDesc desc = shape_of(shape);
			const Ref<const QuerySnapshot> snapshot = query();
			const std::optional<RayHit> hit = snapshot ? snapshot->shape_cast(desc, from.Value, to.Value, mask_of(mask)) : std::nullopt;
			return hit ? sol::object(hit_table(view, context, *hit)) : sol::object(view, sol::lua_nil);
		});
		physics.set_function("set_gravity", [&context](const LuaVec3& gravity) { physics_of(context).set_gravity(gravity.Value); });

		runtime.sandbox()["physics"] = physics;
	}
}
