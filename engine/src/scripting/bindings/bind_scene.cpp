#include <pch.h>
#include "bindings.h"

#include <algorithm>
#include <cctype>

#include <scene/reflection/component_registry.h>

namespace wiwa
{
	namespace
	{
		// Errors thrown here reach the script as Lua errors (the runtime's exception handler turns them into messages).
		Scene& scene_of(ScriptContext& context)
		{
			if (context.ActiveScene == nullptr)
			{
				throw sol::error("there is no active scene");
			}
			return *context.ActiveScene;
		}

		entt::entity entity_of(const EntityHandle& handle)
		{
			const entt::entity entity = handle.resolve();
			if (entity == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			return entity;
		}

		EntityHandle handle_of(ScriptContext& context, entt::entity entity)
		{
			return EntityHandle{ context.ActiveScene->registry().get<Id>(entity).Guid, &context };
		}

		// A component by registry name ("Light", "script:<guid>") or, for a script, by file name ("Enemy"). A file name that two
		// scripts share is an error naming both, never a guess.
		const ComponentInfo& find_component(ScriptContext& context, const std::string& name)
		{
			if (const ComponentInfo* exact = ComponentRegistry::get().find(name))
			{
				return *exact;
			}

			std::vector<const ComponentInfo*> matches;
			for (const ComponentInfo* info : ComponentRegistry::get().all())
			{
				if (info->DisplayName == name)
				{
					matches.push_back(info);
				}
			}

			if (matches.size() == 1)
			{
				return *matches[0];
			}
			if (matches.empty())
			{
				throw sol::error("there is no component named '" + name + "'");
			}

			std::string paths;
			for (const ComponentInfo* info : matches)
			{
				std::string path = info->Name;
				if (context.Runtime != nullptr)
				{
					if (const std::optional<AssetGuid> guid = AssetGuid::from_string(info->Name.substr(info->Name.find(':') + 1)))
					{
						if (const ScriptClass* scriptClass = context.Runtime->find(AssetRef{ *guid, 0 }))
						{
							path = scriptClass->Path;
						}
					}
				}
				paths += (paths.empty() ? "" : ", ") + path;
			}
			throw sol::error("'" + name + "' names more than one script: " + paths);
		}

		// The components every entity has and the scene keeps consistent: a script cannot add or remove them.
		void require_changeable(const ComponentInfo& info)
		{
			if (info.Name == "Id" || info.Name == "Name" || info.Name == "Transform" || info.Name == "PrefabInstance")
			{
				throw sol::error("component '" + info.Name + "' cannot be added or removed by a script");
			}
		}

		const ComponentInfo& component_of_proxy(const ComponentProxy& proxy)
		{
			const ComponentInfo* info = ComponentRegistry::get().find(proxy.Component);
			if (info == nullptr)
			{
				throw sol::error("the component '" + proxy.Component + "' is no longer registered");
			}
			return *info;
		}

		size_t field_of(const ComponentInfo& info, const sol::stack_object& key)
		{
			if (key.get_type() != sol::type::string)
			{
				throw sol::error("a component is indexed by field name");
			}
			const std::string field = key.as<std::string>();

			// the exact name, else the same name in any case: reflected fields are written Position, scripts write position
			int index = info.field_index(field);
			for (size_t i = 0; i < info.Fields.size() && index < 0; i++)
			{
				const std::string& candidate = info.Fields[i].Name;
				if (candidate.size() == field.size() && std::equal(candidate.begin(), candidate.end(), field.begin(), [](char x, char y)
					{
						return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
					}))
				{
					index = static_cast<int>(i);
				}
			}
			if (index < 0)
			{
				throw sol::error("component '" + info.display_name() + "' has no field '" + field + "'");
			}
			return static_cast<size_t>(index);
		}

		void bind_component(sol::state& lua)
		{
			lua.new_usertype<ComponentProxy>("Component", sol::no_constructor,
				sol::meta_function::index, [](const ComponentProxy& proxy, sol::stack_object key, sol::this_state state) -> sol::object
				{
					const ComponentInfo& info = component_of_proxy(proxy);
					const size_t field = field_of(info, key);
					const entt::entity entity = entity_of(proxy.Handle); // checks that the entity, and so the scene, is there
					entt::registry& registry = proxy.Handle.Context->ActiveScene->registry();
					if (!info.Has(registry, entity))
					{
						throw sol::error("the entity has no component '" + info.display_name() + "'");
					}
					return field_to_lua(sol::state_view(state), info.Get(registry, entity, field), info.Fields[field], FieldLink{ proxy.Handle, proxy.Component, field });
				},
				sol::meta_function::new_index, [](const ComponentProxy& proxy, sol::stack_object key, sol::stack_object value)
				{
					const ComponentInfo& info = component_of_proxy(proxy);
					const size_t field = field_of(info, key);
					const entt::entity entity = entity_of(proxy.Handle); // checks that the entity, and so the scene, is there
					entt::registry& registry = proxy.Handle.Context->ActiveScene->registry();
					if (!info.Has(registry, entity))
					{
						throw sol::error("the entity has no component '" + info.display_name() + "'");
					}

					FieldValue converted;
					std::string error;
					if (!field_from_lua(info.Fields[field], sol::object(value), converted, error))
					{
						throw sol::error(info.display_name() + "." + info.Fields[field].Name + ": " + error);
					}
					if (!info.Set(registry, entity, field, converted))
					{
						throw sol::error(info.display_name() + "." + info.Fields[field].Name + ": the value is not valid for the field");
					}
				});
		}

		void bind_entity(sol::state& lua, ScriptContext& context)
		{
			lua.new_usertype<EntityHandle>("Entity", sol::no_constructor,
				"is_valid", &EntityHandle::is_valid,
				"guid", sol::property([](const EntityHandle& handle) { return handle.Guid.to_string(); }),
				sol::meta_function::equal_to, [](const EntityHandle& a, const EntityHandle& b) { return a.Guid == b.Guid; },

				"has", [&context](const EntityHandle& handle, const std::string& name)
				{
					const ComponentInfo& info = find_component(context, name);
					return info.Has(scene_of(context).registry(), entity_of(handle));
				},
				"get", [&context](const EntityHandle& handle, const std::string& name)
				{
					const ComponentInfo& info = find_component(context, name);
					if (!info.Has(scene_of(context).registry(), entity_of(handle)))
					{
						throw sol::error("the entity has no component '" + name + "'");
					}
					return ComponentProxy{ handle, info.Name };
				},
				"add", [&context](const EntityHandle& handle, const std::string& name)
				{
					const ComponentInfo& info = find_component(context, name);
					require_changeable(info);
					info.Add(scene_of(context).registry(), entity_of(handle));
					return ComponentProxy{ handle, info.Name };
				},
				"remove", [&context](const EntityHandle& handle, const std::string& name)
				{
					const ComponentInfo& info = find_component(context, name);
					require_changeable(info);
					info.Remove(scene_of(context).registry(), entity_of(handle));
				},

				"name", sol::property(
					[&context](const EntityHandle& handle) { return scene_of(context).registry().get<Name>(entity_of(handle)).Value; },
					[&context](const EntityHandle& handle, const std::string& value) { scene_of(context).registry().get<Name>(entity_of(handle)).Value = value; }),
				"parent", sol::property([&context](const EntityHandle& handle, sol::this_state state) -> sol::object
					{
						Scene& scene = scene_of(context);
						const entt::entity parent = scene.registry().get<Hierarchy>(entity_of(handle)).Parent;
						return parent == entt::null ? sol::object(sol::state_view(state), sol::lua_nil) : sol::make_object(state, handle_of(context, parent));
					}),
				"children", sol::property([&context](const EntityHandle& handle, sol::this_state state)
					{
						Scene& scene = scene_of(context);
						sol::table children = sol::table::create(state.L);
						for (entt::entity child = scene.registry().get<Hierarchy>(entity_of(handle)).FirstChild; child != entt::null;
							child = scene.registry().get<Hierarchy>(child).NextSibling)
						{
							children.add(handle_of(context, child));
						}
						return children;
					}),
				"set_parent", [&context](const EntityHandle& handle, sol::optional<EntityHandle> parent)
				{
					Scene& scene = scene_of(context);
					return scene.set_parent(entity_of(handle), parent ? entity_of(*parent) : entt::null);
				});
		}
	}

	void bind_scene(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();
		bind_component(lua);
		bind_entity(lua, context);

		sol::table& sandbox = runtime.sandbox();

		sandbox.set_function("create_entity", [&context](const std::string& name, sol::optional<EntityHandle> parent)
			{
				Scene& scene = scene_of(context);
				return handle_of(context, scene.create(name, parent ? entity_of(*parent) : entt::null));
			});

		sandbox.set_function("destroy_entity", [&context](const EntityHandle& handle)
			{
				scene_of(context).destroy(entity_of(handle));
			});

		// by guid, or by the name of the first entity that has it
		sandbox.set_function("find_entity", [&context](const std::string& what, sol::this_state state) -> sol::object
			{
				Scene& scene = scene_of(context);
				entt::entity found = entt::null;
				if (const std::optional<EntityGuid> guid = EntityGuid::from_string(what))
				{
					found = scene.find(*guid);
				}
				if (found == entt::null)
				{
					for (const entt::entity entity : scene.registry().view<Name>())
					{
						if (scene.registry().get<Name>(entity).Value == what)
						{
							found = entity;
							break;
						}
					}
				}
				return found == entt::null ? sol::object(sol::state_view(state), sol::lua_nil) : sol::make_object(state, handle_of(context, found));
			});
	}
}
