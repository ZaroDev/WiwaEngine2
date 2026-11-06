#include <pch.h>
#include "bindings.h"

#include <animation/animation_module.h>
#include <scene/scene_module.h>

#include "../anim_handlers.h"
#include "../ui_handlers.h"

namespace wiwa
{
	namespace
	{
		// An animator as a script holds it: the guid of the entity that has the Animator. Everything is looked up on each use, so a handle to an
		// entity that is gone (or to a scene that was replaced) reads nil and ignores writes, like the handles of the UI.
		struct LuaAnimator
		{
			EntityGuid Entity{};
			ScriptContext* Context = nullptr;
		};

		struct LuaAnimLayer
		{
			EntityGuid Entity{};
			LayerId Layer = NO_LAYER;
			ScriptContext* Context = nullptr;
		};

		entt::entity entity_of(ScriptContext& context, const EntityGuid& guid)
		{
			return context.ActiveScene != nullptr ? context.ActiveScene->find(guid) : entt::null;
		}

		// Null when there is nothing to talk to: no animation module, the entity is gone or has no (enabled) Animator, or the game does not play.
		AnimatorInstance* instance_of(ScriptContext& context, const EntityGuid& guid)
		{
			if (context.Animation == nullptr || context.ActiveSceneRef == nullptr)
			{
				return nullptr;
			}
			const entt::entity entity = entity_of(context, guid);
			return entity != entt::null ? context.Animation->acquire(context.ActiveSceneRef, entity, context.GamePlaying) : nullptr;
		}

		sol::object nil_object(sol::this_state state)
		{
			return sol::object(sol::state_view(state), sol::lua_nil);
		}

		std::string joined(const std::vector<std::string>& names)
		{
			std::string text;
			for (const std::string& name : names)
			{
				text += (text.empty() ? "" : ", ") + name;
			}
			return text;
		}

		// A clip is a name of the animator or an asset (load_asset). An unknown name is an error that lists the clips there are.
		ClipSelector select_clip(AnimatorInstance& instance, const sol::object& clip)
		{
			ClipSelector selector;
			if (clip.is<std::string>())
			{
				selector.Name = clip.as<std::string>();
				AssetRef ref;
				if (instance.lookup(selector.Name, ref) == ClipLookup::Unknown)
				{
					throw sol::error("the animator has no clip named '" + selector.Name + "' (clips: " + joined(instance.clip_names()) + ")");
				}
			}
			else if (clip.is<LuaAsset>())
			{
				selector.Ref = clip.as<LuaAsset>().Ref;
				if (!selector.Ref.valid())
				{
					throw sol::error("the clip asset is not valid");
				}
			}
			else
			{
				throw sol::error("a clip is a name (a string) or an asset from load_asset");
			}
			return selector;
		}

		PlayOptions play_options(const sol::optional<sol::table>& options, bool loopDefault)
		{
			PlayOptions result;
			result.Loop = loopDefault;
			if (options)
			{
				result.Loop = options->get_or("loop", loopDefault);
				result.Speed = options->get_or("speed", 1.0f);
			}
			return result;
		}

		LayerOptions layer_options(const sol::optional<sol::table>& options)
		{
			LayerOptions result;
			if (options)
			{
				result.Mask = options->get_or("mask", std::string{});
				result.Weight = options->get_or("weight", 1.0f);
				result.Additive = options->get_or("additive", false);
				result.Loop = options->get_or("loop", false);
			}
			return result;
		}

		const sol::table& require_owner(ScriptContext& context, const char* what)
		{
			if (context.AnimHandlers == nullptr || context.UiHandlers == nullptr || context.UiHandlers->current() == nullptr)
			{
				throw sol::error(std::string(what) + ": handlers are registered from a script hook (on_init, on_update, ...)");
			}
			return *context.UiHandlers->current();
		}

		void bind_layer(sol::state& lua, ScriptContext& context)
		{
			lua.new_usertype<LuaAnimLayer>("AnimationLayer", sol::no_constructor,
				"weight", sol::property(
					[&context](const LuaAnimLayer& layer, sol::this_state state) -> sol::object
					{
						AnimatorInstance* instance = instance_of(context, layer.Entity);
						if (instance == nullptr || !instance->layer_alive(layer.Layer))
						{
							return nil_object(state);
						}
						return sol::make_object(state, instance->layer_weight(layer.Layer));
					},
					[&context](const LuaAnimLayer& layer, float weight)
					{
						AnimatorInstance* instance = instance_of(context, layer.Entity);
						if (instance != nullptr && instance->layer_alive(layer.Layer))
						{
							instance->set_layer_weight(layer.Layer, weight);
						}
					}),
				"crossfade", [&context](const LuaAnimLayer& layer, sol::object clip, sol::optional<float> seconds, sol::optional<sol::table> options)
				{
					AnimatorInstance* instance = instance_of(context, layer.Entity);
					if (instance == nullptr || !instance->layer_alive(layer.Layer))
					{
						return;
					}
					instance->layer_crossfade(layer.Layer, select_clip(*instance, clip), seconds.value_or(0.0f), play_options(options, false).Loop);
				},
				"stop", [&context](const LuaAnimLayer& layer, sol::optional<float> seconds)
				{
					if (AnimatorInstance* instance = instance_of(context, layer.Entity))
					{
						instance->layer_stop(layer.Layer, seconds.value_or(0.0f));
					}
				});
		}

		void bind_animator(sol::state& lua, ScriptContext& context)
		{
			lua.new_usertype<LuaAnimator>("AnimatorHandle", sol::no_constructor,
				// anim:play("Idle", { loop = true, speed = 1.0 }) or anim:play(load_asset("assets/models/x.glb"))
				"play", [&context](const LuaAnimator& animator, sol::object clip, sol::optional<sol::table> options)
				{
					if (AnimatorInstance* instance = instance_of(context, animator.Entity))
					{
						instance->play(select_clip(*instance, clip), play_options(options, true));
					}
				},
				"crossfade", [&context](const LuaAnimator& animator, sol::object clip, sol::optional<float> seconds, sol::optional<sol::table> options)
				{
					if (AnimatorInstance* instance = instance_of(context, animator.Entity))
					{
						instance->crossfade(select_clip(*instance, clip), seconds.value_or(0.0f), play_options(options, true));
					}
				},
				"stop", [&context](const LuaAnimator& animator, sol::optional<float> seconds)
				{
					if (AnimatorInstance* instance = instance_of(context, animator.Entity))
					{
						instance->stop(seconds.value_or(0.0f));
					}
				},
				// anim:layer("upper", "Wave", { mask = "Spine", weight = 1.0, additive = false, loop = false })
				"layer", [&context](const LuaAnimator& animator, const std::string& name, sol::object clip, sol::optional<sol::table> options, sol::this_state state) -> sol::object
				{
					AnimatorInstance* instance = instance_of(context, animator.Entity);
					if (instance == nullptr)
					{
						return nil_object(state);
					}
					const LayerId id = instance->add_layer(name, select_clip(*instance, clip), layer_options(options));
					return sol::make_object(state, LuaAnimLayer{ animator.Entity, id, &context });
				},
				// anim:on("footstep", function(self, event) ... end): self is the script instance, event is { name, clip, time }
				"on", [&context](const LuaAnimator& animator, const std::string& event, sol::object function)
				{
					const sol::table& owner = require_owner(context, "on");
					if (function.get_type() != sol::type::function)
					{
						throw sol::error("on: the handler is not a function");
					}
					AnimHandler handler;
					handler.Animator = animator.Entity;
					handler.Event = event;
					handler.Function = sol::protected_function(function);
					handler.Owner = owner;
					context.AnimHandlers->add(std::move(handler));
				},
				"off", [&context](const LuaAnimator& animator, const std::string& event)
				{
					const sol::table& owner = require_owner(context, "off");
					context.AnimHandlers->remove(owner, animator.Entity, event);
				},
				"joint_world", [&context](const LuaAnimator& animator, const std::string& joint, sol::this_state state) -> sol::object
				{
					AnimatorInstance* instance = instance_of(context, animator.Entity);
					if (instance == nullptr || !instance->ready())
					{
						return nil_object(state);
					}
					return sol::make_object(state, LuaMat4{ context.Animation->joint_world(entity_of(context, animator.Entity), joint) });
				},
				"speed", sol::property(
					[&context](const LuaAnimator& animator, sol::this_state state) -> sol::object
					{
						const entt::entity entity = entity_of(context, animator.Entity);
						const Animator* component = entity != entt::null ? context.ActiveScene->registry().try_get<Animator>(entity) : nullptr;
						return component != nullptr ? sol::make_object(state, component->Speed) : nil_object(state);
					},
					[&context](const LuaAnimator& animator, float speed)
					{
						const entt::entity entity = entity_of(context, animator.Entity);
						if (Animator* component = entity != entt::null ? context.ActiveScene->registry().try_get<Animator>(entity) : nullptr)
						{
							component->Speed = speed;
						}
					}),
				"time", sol::property([&context](const LuaAnimator& animator, sol::this_state state) -> sol::object
					{
						AnimatorInstance* instance = instance_of(context, animator.Entity);
						return instance != nullptr ? sol::make_object(state, instance->time()) : nil_object(state);
					}),
				"playing", sol::property([&context](const LuaAnimator& animator, sol::this_state state) -> sol::object
					{
						AnimatorInstance* instance = instance_of(context, animator.Entity);
						return instance != nullptr ? sol::make_object(state, instance->playing()) : nil_object(state);
					}),
				"ready", sol::property([&context](const LuaAnimator& animator, sol::this_state state) -> sol::object
					{
						AnimatorInstance* instance = instance_of(context, animator.Entity);
						return instance != nullptr ? sol::make_object(state, instance->ready()) : nil_object(state);
					}));
		}
	}

	void bind_animation(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();
		bind_layer(lua, context);
		bind_animator(lua, context);

		sol::table animation = lua.create_table();
		// Animation.animator(entity): the animator of the entity, or nil if it has no Animator
		animation.set_function("animator", [&context](const EntityHandle& entity, sol::this_state state) -> sol::object
			{
				const entt::entity resolved = entity.resolve();
				if (resolved == entt::null)
				{
					throw sol::error("the entity no longer exists");
				}
				if (!context.ActiveScene->registry().all_of<Animator>(resolved))
				{
					return nil_object(state);
				}
				return sol::make_object(state, LuaAnimator{ entity.Guid, &context });
			});
		runtime.sandbox()["Animation"] = animation;
	}
}
