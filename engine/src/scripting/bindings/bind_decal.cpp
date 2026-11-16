#include <pch.h>
#include "bindings.h"

#include <cmath>

#include <glm/gtx/quaternion.hpp>

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
	}

	// Decal.spawn{ material, position, normal, size, opacity, lifetime, fade_in, fade_out, sort_order, roll, parent, name }
	// Makes an entity with a Decal that projects the material onto the surface at `position` whose normal is `normal` (default +Z): the box's
	// local +Z points along the normal, so it projects along -normal. size is the box (width, height, depth of the projection). Returns the entity.
	void bind_decal(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::table decal = runtime.state().create_table();

		decal.set_function("spawn", [&context](const sol::table& args)
			{
				if (context.ActiveScene == nullptr)
				{
					throw sol::error("there is no active scene");
				}

				const sol::optional<LuaAsset> material = args["material"];
				if (!material || !material->Ref.valid())
				{
					throw sol::error("Decal.spawn needs a material (an asset from load_asset)");
				}

				const sol::optional<LuaVec3> positionArg = args["position"];
				const sol::optional<LuaVec3> normalArg = args["normal"];
				const sol::optional<LuaVec3> sizeArg = args["size"];
				const glm::vec3 position = positionArg ? positionArg->Value : glm::vec3(0.0f);
				glm::vec3 normal = normalArg ? normalArg->Value : glm::vec3(0.0f, 0.0f, 1.0f);
				const glm::vec3 size = sizeArg ? sizeArg->Value : glm::vec3(1.0f);
				if (!finite(position) || !finite(normal) || !finite(size))
				{
					throw sol::error("Decal.spawn: position, normal and size must be finite numbers");
				}
				if (glm::length(normal) < 1e-6f)
				{
					throw sol::error("Decal.spawn: the normal cannot be zero");
				}
				if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f)
				{
					throw sol::error("Decal.spawn: every part of the size must be above zero");
				}
				normal = glm::normalize(normal);

				entt::entity parent = entt::null;
				if (const sol::optional<EntityHandle> parentArg = args["parent"])
				{
					parent = parentArg->resolve();
					if (parent == entt::null)
					{
						throw sol::error("Decal.spawn: the parent no longer exists");
					}
				}

				Scene& scene = *context.ActiveScene;
				const std::string name = option<std::string>(args, "name", "Decal");
				const entt::entity entity = scene.create(name, parent);

				Transform& transform = scene.registry().get<Transform>(entity);
				transform.Position = position;
				transform.Rotation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), normal);
				const float roll = option<float>(args, "roll", 0.0f);
				if (roll != 0.0f)
				{
					transform.Rotation = transform.Rotation * glm::angleAxis(glm::radians(roll), glm::vec3(0.0f, 0.0f, 1.0f));
				}

				Decal component;
				component.Material = material->Ref;
				component.Size = size;
				component.Opacity = glm::clamp(option<float>(args, "opacity", 1.0f), 0.0f, 1.0f);
				component.Lifetime = std::max(option<float>(args, "lifetime", 0.0f), 0.0f);
				component.FadeInTime = std::max(option<float>(args, "fade_in", 0.0f), 0.0f);
				component.FadeOutTime = std::max(option<float>(args, "fade_out", 0.0f), 0.0f);
				component.SortOrder = option<int>(args, "sort_order", 0);
				scene.registry().emplace<Decal>(entity, component);

				return EntityHandle{ scene.registry().get<Id>(entity).Guid, &context };
			});

		runtime.sandbox()["Decal"] = decal;
	}
}
