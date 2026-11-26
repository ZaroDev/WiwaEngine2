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
			return require_module(context.Physics, "there is no physics module");
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

		// The Joint component of an entity, for joint.set_motor and joint.set_limits. They write the component: the physics sync sends the
		// change (the joint keeps its rest pose) and it takes effect one step later, like the body control.
		Joint& joint_of(const EntityHandle& handle)
		{
			const entt::entity entity = handle.resolve();
			if (entity == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			Joint* joint = handle.Context->ActiveScene->registry().try_get<Joint>(entity);
			if (joint == nullptr)
			{
				throw sol::error("the entity has no Joint component");
			}
			return *joint;
		}

		// The entity of a character.* call: it must exist and have a CharacterController.
		void require_character(const EntityHandle& handle)
		{
			const entt::entity entity = handle.resolve();
			if (entity == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			if (!handle.Context->ActiveScene->registry().all_of<CharacterController>(entity))
			{
				throw sol::error("the entity has no CharacterController component");
			}
		}

		// The entity of a vehicle.* call: it must exist and have a Vehicle.
		void require_vehicle(const EntityHandle& handle, const char* function)
		{
			const entt::entity entity = handle.resolve();
			if (entity == entt::null)
			{
				throw sol::error(std::string("vehicle.") + function + ": the entity no longer exists");
			}
			if (!handle.Context->ActiveScene->registry().all_of<Vehicle>(entity))
			{
				throw sol::error(std::string("vehicle.") + function + ": the entity has no Vehicle");
			}
		}

		VehicleAssist vehicle_assist_of(const std::string& name)
		{
			if (name == "traction_control") return VehicleAssist::TractionControl;
			if (name == "abs") return VehicleAssist::Abs;
			if (name == "stability") return VehicleAssist::Stability;
			if (name == "steering_help") return VehicleAssist::SteeringHelp;
			if (name == "auto_gearbox") return VehicleAssist::AutoGearbox;
			throw sol::error("vehicle.set_assist: unknown assist '" + name + "' (traction_control, abs, stability, steering_help or auto_gearbox)");
		}

		JointMotorMode joint_mode_of(const std::string& mode)
		{
			if (mode == "off")
			{
				return JointMotorMode::Off;
			}
			if (mode == "velocity")
			{
				return JointMotorMode::Velocity;
			}
			if (mode == "position")
			{
				return JointMotorMode::Position;
			}
			throw sol::error("joint.set_motor: mode must be \"off\", \"velocity\" or \"position\"");
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

		// joint.set_motor(entity, mode, target, max_force) and joint.set_limits(entity, min, max) on the entity's Joint component
		sol::table joint = lua.create_table();
		joint.set_function("set_motor", [](const EntityHandle& entity, const std::string& mode, float target, sol::optional<float> maxForce)
		{
			const JointMotorMode motor = joint_mode_of(mode);
			if (!std::isfinite(target) || (maxForce && !std::isfinite(*maxForce)))
			{
				throw sol::error("joint.set_motor: the target and the maximum force must be finite numbers");
			}
			Joint& component = joint_of(entity);
			component.MotorMode = motor;
			component.MotorTarget = target;
			component.MotorMaxForce = maxForce ? std::max(*maxForce, 0.0f) : component.MotorMaxForce;
		});
		joint.set_function("set_limits", [](const EntityHandle& entity, float min, float max)
		{
			if (!std::isfinite(min) || !std::isfinite(max) || min > max)
			{
				throw sol::error("joint.set_limits: min and max must be finite numbers with min <= max");
			}
			Joint& component = joint_of(entity);
			component.LimitMin = min;
			component.LimitMax = max;
		});
		runtime.sandbox()["joint"] = joint;

		// character.move(entity, velocity), character.jump(entity, speed), character.is_grounded(entity), character.state(entity)
		sol::table character = lua.create_table();
		character.set_function("move", [&context](const EntityHandle& entity, const LuaVec3& velocity)
		{
			require_character(entity);
			if (!std::isfinite(velocity.Value.x) || !std::isfinite(velocity.Value.y) || !std::isfinite(velocity.Value.z))
			{
				throw sol::error("character.move: the velocity must be finite");
			}
			physics_of(context).set_character_velocity(entity.Guid, velocity.Value);
		});
		character.set_function("jump", [&context](const EntityHandle& entity, float speed)
		{
			require_character(entity);
			if (!std::isfinite(speed))
			{
				throw sol::error("character.jump: the speed must be a finite number");
			}
			physics_of(context).character_jump(entity.Guid, speed);
		});
		character.set_function("is_grounded", [&context](const EntityHandle& entity)
		{
			require_character(entity);
			const std::optional<CharacterPose> pose = physics_of(context).newest_character(entity.Guid);
			return pose && pose->Ground == GroundState::Grounded;
		});
		// { grounded, sliding, ground (the entity it stands on, or nil), normal, velocity }; nil while the world has no such character
		character.set_function("state", [&context](const EntityHandle& entity, sol::this_state state) -> sol::object
		{
			require_character(entity);
			sol::state_view view(state);
			const std::optional<CharacterPose> pose = physics_of(context).newest_character(entity.Guid);
			if (!pose)
			{
				return sol::object(view, sol::lua_nil);
			}
			sol::table result = view.create_table();
			result["grounded"] = pose->Ground == GroundState::Grounded;
			result["sliding"] = pose->Ground == GroundState::Sliding;
			result["ground"] = pose->GroundBody.valid() ? sol::object(sol::make_object(state, EntityHandle{ pose->GroundBody, &context })) : sol::object(view, sol::lua_nil);
			result["normal"] = LuaVec3{ pose->GroundNormal, {} };
			result["velocity"] = LuaVec3{ pose->Velocity, {} };
			return result;
		});
		runtime.sandbox()["character"] = character;

		// vehicle.set_input(entity, throttle, brake, steer [, handbrake]), shift_up/shift_down(entity), set_assist(entity, name, strength|nil),
		// state(entity), wheel_count(entity), wheel(entity, index): see docs/scripting.md
		sol::table vehicle = lua.create_table();
		vehicle.set_function("set_input", [&context](const EntityHandle& entity, float throttle, float brake, float steer, sol::optional<float> handbrake)
		{
			require_vehicle(entity, "set_input");
			const float hand = handbrake.value_or(0.0f);
			if (!std::isfinite(throttle) || !std::isfinite(brake) || !std::isfinite(steer) || !std::isfinite(hand))
			{
				throw sol::error("vehicle.set_input: the inputs must be finite numbers");
			}
			physics_of(context).set_vehicle_input(entity.Guid, VehicleInput{ std::clamp(throttle, 0.0f, 1.0f), std::clamp(brake, 0.0f, 1.0f),
				std::clamp(steer, -1.0f, 1.0f), std::clamp(hand, 0.0f, 1.0f) });
		});
		vehicle.set_function("shift_up", [&context](const EntityHandle& entity)
		{
			require_vehicle(entity, "shift_up");
			physics_of(context).shift_gear(entity.Guid, 1);
		});
		vehicle.set_function("shift_down", [&context](const EntityHandle& entity)
		{
			require_vehicle(entity, "shift_down");
			physics_of(context).shift_gear(entity.Guid, -1);
		});
		vehicle.set_function("set_assist", [&context](const EntityHandle& entity, const std::string& name, sol::optional<float> strength)
		{
			require_vehicle(entity, "set_assist");
			const VehicleAssist assist = vehicle_assist_of(name);
			if (strength && !std::isfinite(*strength))
			{
				throw sol::error("vehicle.set_assist: the strength must be a finite number, or nil for the asset's");
			}
			physics_of(context).set_vehicle_assist(entity.Guid, assist, strength ? std::clamp(*strength, 0.0f, 1.0f) : -1.0f);
		});
		// { speed, rpm, gear, clutch, throttle, brake, steer, handbrake (what the car got, after the assists), abs, tc, stability (working now) }
		vehicle.set_function("state", [&context](const EntityHandle& entity, sol::this_state state) -> sol::object
		{
			require_vehicle(entity, "state");
			sol::state_view view(state);
			const std::optional<VehiclePose> pose = physics_of(context).newest_vehicle(entity.Guid);
			if (!pose)
			{
				return sol::object(view, sol::lua_nil);
			}
			sol::table result = view.create_table();
			result["speed"] = pose->Speed;
			result["rpm"] = pose->EngineRpm;
			result["gear"] = pose->Gear;
			result["clutch"] = pose->Clutch;
			result["throttle"] = pose->Applied.Throttle;
			result["brake"] = pose->Applied.Brake;
			result["steer"] = pose->Applied.Steer;
			result["handbrake"] = pose->Applied.Handbrake;
			result["abs"] = (pose->AssistsActive & AssistAbs) != 0;
			result["tc"] = (pose->AssistsActive & AssistTc) != 0;
			result["stability"] = (pose->AssistsActive & AssistStability) != 0;
			return result;
		});
		vehicle.set_function("wheel_count", [&context](const EntityHandle& entity)
		{
			require_vehicle(entity, "wheel_count");
			const std::optional<VehiclePose> pose = physics_of(context).newest_vehicle(entity.Guid);
			return pose ? static_cast<int>(pose->Wheels.size()) : 0;
		});
		// { steer, spin, suspension, contact, contact_entity, grip, slip_ratio, slip_angle, load, aligning_torque }; nil past the last wheel
		vehicle.set_function("wheel", [&context](const EntityHandle& entity, int index, sol::this_state state) -> sol::object
		{
			require_vehicle(entity, "wheel");
			sol::state_view view(state);
			const std::optional<VehiclePose> pose = physics_of(context).newest_vehicle(entity.Guid);
			if (!pose || index < 1 || index > static_cast<int>(pose->Wheels.size()))
			{
				return sol::object(view, sol::lua_nil);
			}
			const WheelPose& wheel = pose->Wheels[static_cast<size_t>(index - 1)];
			sol::table result = view.create_table();
			result["steer"] = wheel.SteerAngle;
			result["spin"] = wheel.SpinAngle;
			result["suspension"] = wheel.SuspensionLength;
			result["contact"] = wheel.InContact;
			result["contact_entity"] = wheel.ContactBody.valid() ? sol::object(sol::make_object(state, EntityHandle{ wheel.ContactBody, &context })) : sol::object(view, sol::lua_nil);
			result["grip"] = wheel.Grip;
			result["slip_ratio"] = wheel.SlipRatio;
			result["slip_angle"] = wheel.SlipAngle;
			result["load"] = wheel.Load;
			result["aligning_torque"] = wheel.AligningTorque;
			return result;
		});
		runtime.sandbox()["vehicle"] = vehicle;
	}
}
