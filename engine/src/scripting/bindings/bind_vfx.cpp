#include <pch.h>
#include "bindings.h"

#include <cmath>

#include <animation/animation_module.h>
#include <scene/particle_runtime.h>

namespace wiwa
{
	namespace
	{
		template<typename T>
		T option(const sol::table& table, const char* key, T fallback)
		{
			const sol::optional<T> value = table[key];
			return value ? *value : fallback;
		}

		bool finite(const glm::vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		Scene& scene_of(ScriptContext& context)
		{
			return require_module(context.ActiveScene, "there is no active scene");
		}

		// The ParticleSystem of an entity; an error that names the call when it has none.
		ParticleSystem& system_of(ScriptContext& context, const EntityHandle& handle, const char* call)
		{
			const entt::entity entity = handle.resolve();
			if (entity == entt::null)
			{
				throw sol::error(std::string("Vfx.") + call + ": the entity no longer exists");
			}
			ParticleSystem* system = scene_of(context).registry().try_get<ParticleSystem>(entity);
			if (system == nullptr)
			{
				throw sol::error(std::string("Vfx.") + call + ": the entity has no ParticleSystem");
			}
			return *system;
		}

		// "color" (a vec4, or a vec3 with alpha 1), "size", "rate" and "time_scale".
		void set_param(ParticleSystem& system, const std::string& name, const sol::object& value)
		{
			if (name == "color")
			{
				if (value.is<LuaVec4>())
				{
					system.ColorTint = value.as<LuaVec4>().Value;
				}
				else if (value.is<LuaVec3>())
				{
					system.ColorTint = glm::vec4(value.as<LuaVec3>().Value, 1.0f);
				}
				else
				{
					throw sol::error("Vfx: the parameter 'color' is a vec4 (or a vec3)");
				}
				return;
			}

			float* target = name == "size" ? &system.SizeScale : name == "rate" ? &system.RateScale : name == "time_scale" ? &system.TimeScale : nullptr;
			if (target == nullptr)
			{
				throw sol::error("Vfx: there is no parameter '" + name + "' (color, size, rate or time_scale)");
			}
			if (!value.is<double>() || !std::isfinite(value.as<double>()) || value.as<double>() < 0.0)
			{
				throw sol::error("Vfx: the parameter '" + name + "' is a number of zero or more");
			}
			*target = static_cast<float>(value.as<double>());
		}
	}

	// Vfx.spawn{ effect, position, rotation, scale, parent, name, looping, time_scale, params, animator, joint }
	// Vfx.play(e), Vfx.stop(e, clear?), Vfx.restart(e), Vfx.is_playing(e), Vfx.set_param(e, name, value)
	//
	// spawn makes an entity with a ParticleSystem that plays the effect (an asset from load_asset) once and removes itself when the effect is over (with
	// looping = true it plays until it is stopped or destroyed). With animator (an entity with an Animator) and joint (a joint name) it is placed at
	// that joint, position and rotation then being offsets in the joint's space: what an animation event of a footstep or a sword swing asks for.
	void bind_vfx(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::table vfx = runtime.state().create_table();

		vfx.set_function("spawn", [&context](const sol::table& args)
			{
				Scene& scene = scene_of(context);

				const sol::optional<LuaAsset> effect = args["effect"];
				if (!effect || !effect->Ref.valid())
				{
					throw sol::error("Vfx.spawn needs an effect (an asset from load_asset)");
				}

				const sol::optional<LuaVec3> positionArg = args["position"];
				const sol::optional<LuaQuat> rotationArg = args["rotation"];
				glm::vec3 position = positionArg ? positionArg->Value : glm::vec3(0.0f);
				glm::quat rotation = rotationArg ? rotationArg->Value : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				glm::vec3 scale(1.0f);
				if (const sol::optional<LuaVec3> scaleVector = args["scale"])
				{
					scale = scaleVector->Value;
				}
				else if (const sol::optional<double> scaleNumber = args["scale"])
				{
					scale = glm::vec3(static_cast<float>(*scaleNumber));
				}
				if (!finite(position) || !finite(scale) || !std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
				{
					throw sol::error("Vfx.spawn: position, rotation and scale must be finite numbers");
				}
				rotation = glm::length(rotation) > 1e-6f ? glm::normalize(rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

				// at a joint of an animator: the position and rotation are offsets from it
				const sol::optional<EntityHandle> animatorArg = args["animator"];
				const sol::optional<std::string> jointArg = args["joint"];
				if (animatorArg || jointArg)
				{
					if (!animatorArg || !jointArg)
					{
						throw sol::error("Vfx.spawn: animator and joint go together");
					}
					const entt::entity animator = animatorArg->resolve();
					if (animator == entt::null)
					{
						throw sol::error("Vfx.spawn: the animator entity no longer exists");
					}
					if (context.Animation == nullptr)
					{
						throw sol::error("Vfx.spawn: there is no animation here to take a joint from");
					}
					const glm::mat4 joint = context.Animation->joint_world(animator, *jointArg);
					const glm::mat4 placed = joint * glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
					position = glm::vec3(placed[3]);
					rotation = glm::quat_cast(glm::mat3(glm::normalize(placed[0]), glm::normalize(placed[1]), glm::normalize(placed[2])));
				}

				entt::entity parent = entt::null;
				if (const sol::optional<EntityHandle> parentArg = args["parent"])
				{
					parent = parentArg->resolve();
					if (parent == entt::null)
					{
						throw sol::error("Vfx.spawn: the parent no longer exists");
					}
				}

				const entt::entity entity = scene.create(option<std::string>(args, "name", "Effect"), parent);
				Transform& transform = scene.registry().get<Transform>(entity);
				transform.Position = position;
				transform.Rotation = rotation;
				transform.Scale = scale;

				ParticleSystem system;
				system.Effect = effect->Ref;
				system.PlayOnStart = true;
				system.Looping = option<bool>(args, "looping", false);
				system.TimeScale = std::max(option<float>(args, "time_scale", 1.0f), 0.0f);
				if (const sol::optional<sol::table> params = args["params"])
				{
					for (const auto& [key, value] : *params)
					{
						if (key.get_type() != sol::type::string)
						{
							throw sol::error("Vfx.spawn: params is a table of names");
						}
						set_param(system, key.as<std::string>(), sol::object(value));
					}
				}
				scene.registry().emplace<ParticleSystem>(entity, system);
				particle_runtime_of(scene.registry(), entity).SelfDestruct = true;

				return EntityHandle{ scene.registry().get<Id>(entity).Guid, &context };
			});

		// Starts the system (it plays from where it is; one that has finished starts over).
		vfx.set_function("play", [&context](const EntityHandle& handle)
			{
				system_of(context, handle, "play");
				ParticleRuntime& state = particle_runtime_of(scene_of(context).registry(), handle.resolve());
				if (state.Finished)
				{
					state.restart();
				}
				state.Playing = true;
			});

		// Stops it spawning; the particles in the air live out their lives, or are discarded at once with clear = true.
		vfx.set_function("stop", [&context](const EntityHandle& handle, sol::optional<bool> clear)
			{
				system_of(context, handle, "stop");
				ParticleRuntime& state = particle_runtime_of(scene_of(context).registry(), handle.resolve());
				state.Playing = false;
				if (clear.value_or(false))
				{
					state.ClearCount++;
				}
			});

		// Plays the effect from its beginning, with no particle of the last play left.
		vfx.set_function("restart", [&context](const EntityHandle& handle)
			{
				system_of(context, handle, "restart");
				particle_runtime_of(scene_of(context).registry(), handle.resolve()).restart();
			});

		vfx.set_function("is_playing", [&context](const EntityHandle& handle)
			{
				system_of(context, handle, "is_playing");
				const ParticleRuntime& state = particle_runtime_of(scene_of(context).registry(), handle.resolve());
				return state.Playing && !state.Finished;
			});

		vfx.set_function("set_param", [&context](const EntityHandle& handle, const std::string& name, sol::object value)
			{
				set_param(system_of(context, handle, "set_param"), name, value);
			});

		runtime.sandbox()["Vfx"] = vfx;
	}
}
