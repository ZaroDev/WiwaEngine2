#include <pch.h>
#include "bindings.h"

#include <renderer/debug/debug_draw.h>

namespace wiwa
{
	namespace
	{
		glm::vec4 color_or_white(const sol::optional<LuaVec4>& color)
		{
			return color ? color->Value : glm::vec4(1.0f);
		}
	}

	void bind_render(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::table debug = runtime.state().create_table();

		// Without a renderer (a test, a tool) there is nowhere to draw and the calls do nothing.
		debug.set_function("line", [&context](const LuaVec3& a, const LuaVec3& b, sol::optional<LuaVec4> color, sol::optional<float> duration)
			{
				if (context.Debug != nullptr)
				{
					context.Debug->line(a.Value, b.Value, color_or_white(color), DebugDepth::Tested, 1.0f, duration.value_or(0.0f));
				}
			});
		debug.set_function("box", [&context](const LuaVec3& min, const LuaVec3& max, sol::optional<LuaVec4> color, sol::optional<float> duration)
			{
				if (context.Debug != nullptr)
				{
					context.Debug->box(min.Value, max.Value, color_or_white(color), DebugDepth::Tested, 1.0f, duration.value_or(0.0f));
				}
			});
		debug.set_function("sphere", [&context](const LuaVec3& center, float radius, sol::optional<LuaVec4> color, sol::optional<float> duration)
			{
				if (context.Debug != nullptr)
				{
					context.Debug->sphere(center.Value, radius, color_or_white(color), DebugDepth::Tested, 1.0f, duration.value_or(0.0f));
				}
			});
		debug.set_function("arrow", [&context](const LuaVec3& from, const LuaVec3& to, sol::optional<LuaVec4> color, sol::optional<float> duration)
			{
				if (context.Debug != nullptr)
				{
					context.Debug->arrow(from.Value, to.Value, color_or_white(color), DebugDepth::Tested, 1.0f, duration.value_or(0.0f));
				}
			});
		runtime.sandbox()["debug_draw"] = debug;

		sol::table camera = runtime.state().create_table();

		// the first entity with a Camera marked Primary, or nil
		camera.set_function("primary", [&context](sol::this_state state) -> sol::object
			{
				if (context.ActiveScene == nullptr)
				{
					throw sol::error("there is no active scene");
				}
				entt::registry& registry = context.ActiveScene->registry();
				for (const entt::entity entity : registry.view<Camera>())
				{
					if (registry.get<Camera>(entity).Primary)
					{
						return sol::make_object(state, EntityHandle{ registry.get<Id>(entity).Guid, &context });
					}
				}
				return sol::object(sol::state_view(state), sol::lua_nil);
			});

		// makes the entity's camera the primary one; no other camera stays primary
		camera.set_function("set_primary", [&context](const EntityHandle& handle)
			{
				const entt::entity target = handle.resolve();
				if (target == entt::null || context.ActiveScene == nullptr || !context.ActiveScene->registry().all_of<Camera>(target))
				{
					throw sol::error("the entity has no Camera");
				}
				entt::registry& registry = context.ActiveScene->registry();
				for (const entt::entity entity : registry.view<Camera>())
				{
					registry.get<Camera>(entity).Primary = entity == target;
				}
			});
		runtime.sandbox()["camera"] = camera;
	}
}
