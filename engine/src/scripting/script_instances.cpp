#include <pch.h>
#include "script_instances.h"

#include <algorithm>

#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include "bindings/bindings.h"
#include "script_names.h"

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	ScriptInstances::ScriptInstances(ScriptRuntime& runtime) : m_Runtime(runtime)
	{
		m_Context.Runtime = &m_Runtime;
		bind_scene(m_Runtime, m_Context);
		bind_assets(m_Runtime, m_Context);
		bind_render(m_Runtime, m_Context);
		bind_engine(m_Runtime, m_Context);
	}

	ScriptInstances::~ScriptInstances()
	{
		stop();
	}

	void ScriptInstances::start(Ref<Scene> scene)
	{
		stop();
		m_Scene = std::move(scene);
		m_Context.ActiveScene = m_Scene.get();
		m_BrokenScripts.clear();
	}

	void ScriptInstances::stop()
	{
		if (m_Scene == nullptr)
		{
			return;
		}

		for (std::vector<Instance>* instances : { &m_Instances, &m_SceneInstances })
		{
			for (Instance& instance : *instances)
			{
				if (!instance.Failed)
				{
					call_hook(instance, "on_destroy", nullptr);
				}
			}
			instances->clear();
		}
		m_Known.clear();
		m_Context.ActiveScene = nullptr;
		m_Scene.reset();
	}

	ScriptClass* ScriptInstances::class_of(AssetGuid script)
	{
		if (m_BrokenScripts.contains(script))
		{
			return nullptr; // it failed to load: not tried again until the scripts start again
		}

		std::string error;
		ScriptClass* scriptClass = m_Runtime.load(AssetRef{ script, 0 }, error);
		if (scriptClass == nullptr && m_BrokenScripts.insert(script).second)
		{
			WI_SCRIPT_LOG(Error, "Script {0} cannot be used: {1}", script.to_string(), error);
		}
		return scriptClass;
	}

	std::string ScriptInstances::describe(const Instance& instance) const
	{
		std::string entityName = "scene";
		if (instance.Entity.valid())
		{
			entityName = "destroyed entity";
			if (m_Scene != nullptr)
			{
				const entt::entity entity = m_Scene->find(instance.Entity);
				if (entity != entt::null)
				{
					if (const Name* name = m_Scene->registry().try_get<Name>(entity))
					{
						entityName = name->Value;
					}
				}
			}
		}
		return instance.Class->Path + " on " + entityName;
	}

	bool ScriptInstances::call_hook(Instance& instance, const char* hook, const float* deltaTime)
	{
		sol::object member;
		std::string lookupError;
		if (!m_Runtime.protected_get(instance.Class->Class, sol::make_object(m_Runtime.state().lua_state(), hook), member, lookupError))
		{
			WI_SCRIPT_LOG(Error, "{0}: {1} failed: {2}", describe(instance), hook, lookupError);
			instance.Failed = true;
			return false;
		}
		if (member.get_type() != sol::type::function)
		{
			return true; // a script does not have to have every hook
		}

		sol::protected_function function = member;
		sol::protected_function_result result = deltaTime != nullptr
			? m_Runtime.call(function, instance.Self, *deltaTime)
			: m_Runtime.call(function, instance.Self);
		if (result.valid())
		{
			return true;
		}

		sol::error failure = result;
		WI_SCRIPT_LOG(Error, "{0}: {1} failed: {2}", describe(instance), hook, failure.what());
		instance.Failed = true;
		return false;
	}

	bool ScriptInstances::alive(const Instance& instance) const
	{
		if (!instance.Entity.valid())
		{
			const std::vector<AssetRef>& scripts = m_Scene->settings().Scripts;
			return std::any_of(scripts.begin(), scripts.end(), [&](const AssetRef& script) { return script.resolved() == instance.Script; });
		}

		const entt::entity entity = m_Scene->find(instance.Entity);
		if (entity == entt::null)
		{
			return false;
		}
		const ValueBags* bags = m_Scene->registry().try_get<ValueBags>(entity);
		if (bags == nullptr)
		{
			return false;
		}
		const std::string name = script_component_name(instance.Script);
		return std::any_of(bags->Bags.begin(), bags->Bags.end(), [&](const ValueBag& bag) { return bag.Name == name; });
	}

	void ScriptInstances::reap()
	{
		for (std::vector<Instance>* instances : { &m_Instances, &m_SceneInstances })
		{
			for (size_t i = 0; i < instances->size();)
			{
				Instance& instance = (*instances)[i];
				if (alive(instance))
				{
					i++;
					continue;
				}

				if (!instance.Failed)
				{
					call_hook(instance, "on_destroy", nullptr);
				}
				m_Known.erase({ instance.Entity, instance.Script });
				instances->erase(instances->begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
	}

	// `self`: a table for the script's own fields, whose metatable gives the rest. Reading a declared property reads the entity's value
	// bag (a scene-level script has no bag: it reads the declared default until it sets its own); writing one is checked against the
	// property's type. `transform` and `name` are the entity's; anything else comes from the class (methods, inherited ones too).
	void ScriptInstances::make_self(Instance& instance)
	{
		lua_State* state = m_Runtime.state().lua_state();
		instance.Self = sol::table::create(state);
		sol::table meta = sol::table::create(state);

		ScriptClass* scriptClass = instance.Class;
		const EntityHandle handle{ instance.Entity, &m_Context };
		const std::string componentName = script_component_name(instance.Script);
		const bool entityScript = instance.Entity.valid();

		auto find_property = [scriptClass](const sol::stack_object& key) -> const FieldDescriptor*
		{
			if (key.get_type() != sol::type::string)
			{
				return nullptr;
			}
			const std::string name = key.as<std::string>();
			const auto found = std::find_if(scriptClass->Properties.begin(), scriptClass->Properties.end(), [&](const FieldDescriptor& field) { return field.Name == name; });
			return found == scriptClass->Properties.end() ? nullptr : &*found;
		};

		meta["__index"] = [this, scriptClass, handle, componentName, entityScript, find_property](sol::table, sol::stack_object key, sol::this_state lua) -> sol::object
		{
			if (const FieldDescriptor* property = find_property(key))
			{
				if (!entityScript)
				{
					return field_to_lua(sol::state_view(lua), property->Default, *property, FieldLink{});
				}

				const ComponentInfo* info = ComponentRegistry::get().find(componentName);
				const entt::entity entity = handle.resolve();
				if (info == nullptr || entity == entt::null)
				{
					throw sol::error(scriptClass->Name + ": the entity no longer exists");
				}
				const int index = info->field_index(property->Name);
				if (index < 0)
				{
					// the file was imported with this property gone and the reload has not come yet: the script keeps running on the default
					return field_to_lua(sol::state_view(lua), property->Default, *property, FieldLink{});
				}
				return field_to_lua(sol::state_view(lua), info->Get(m_Scene->registry(), entity, static_cast<size_t>(index)), *property, FieldLink{ handle, componentName, static_cast<size_t>(index) });
			}

			if (entityScript && key.get_type() == sol::type::string)
			{
				const std::string name = key.as<std::string>();
				if (name == "transform")
				{
					return sol::make_object(lua, ComponentProxy{ handle, "Transform" });
				}
				if (name == "name")
				{
					const entt::entity entity = handle.resolve();
					if (entity == entt::null)
					{
						throw sol::error(scriptClass->Name + ": the entity no longer exists");
					}
					return sol::make_object(lua, m_Scene->registry().get<Name>(entity).Value);
				}
			}
			sol::object member;
			std::string lookupError;
			if (!m_Runtime.protected_get(scriptClass->Class, sol::object(key), member, lookupError))
			{
				throw sol::error(lookupError);
			}
			return member;
		};

		meta["__newindex"] = [this, scriptClass, handle, componentName, entityScript, find_property](sol::table self, sol::stack_object key, sol::stack_object value)
		{
			if (const FieldDescriptor* property = find_property(key))
			{
				FieldValue converted;
				std::string error;
				if (!field_from_lua(*property, sol::object(value), converted, error))
				{
					throw sol::error(scriptClass->Name + "." + property->Name + ": " + error);
				}

				if (!entityScript)
				{
					self.raw_set(sol::object(key), sol::object(value));
					return;
				}

				const ComponentInfo* info = ComponentRegistry::get().find(componentName);
				const entt::entity entity = handle.resolve();
				if (info == nullptr || entity == entt::null)
				{
					throw sol::error(scriptClass->Name + ": the entity no longer exists");
				}
				const int index = info->field_index(property->Name);
				if (index < 0)
				{
					return; // the property is gone from the registry (see __index): the write has nowhere to go until the reload
				}
				if (!info->Set(m_Scene->registry(), entity, static_cast<size_t>(index), converted))
				{
					throw sol::error(scriptClass->Name + "." + property->Name + ": the value is not valid for the property");
				}
				return;
			}

			if (entityScript && key.get_type() == sol::type::string && key.as<std::string>() == "name" && value.get_type() == sol::type::string)
			{
				const entt::entity entity = handle.resolve();
				if (entity != entt::null)
				{
					m_Scene->registry().get<Name>(entity).Value = value.as<std::string>();
				}
				return;
			}
			self.raw_set(sol::object(key), sol::object(value));
		};

		instance.Self[sol::metatable_key] = meta;
		if (entityScript)
		{
			instance.Self["entity"] = handle;
		}
	}

	void ScriptInstances::begin(Instance instance)
	{
		make_self(instance);

		m_Known.insert({ instance.Entity, instance.Script });
		std::vector<Instance>& target = instance.Entity.valid() ? m_Instances : m_SceneInstances;
		target.push_back(std::move(instance));
		call_hook(target.back(), "on_init", nullptr);
	}

	void ScriptInstances::discover()
	{
		// found first, started after: a hook that creates entities must not disturb the view
		std::vector<Instance> found;

		for (const AssetRef& script : m_Scene->settings().Scripts)
		{
			if (script.valid() && !m_Known.contains({ EntityGuid{}, script.resolved() }))
			{
				found.push_back(Instance{ EntityGuid{}, script.resolved() });
			}
		}

		entt::registry& registry = m_Scene->registry();
		for (const entt::entity entity : registry.view<ValueBags>())
		{
			const EntityGuid guid = registry.get<Id>(entity).Guid;
			for (const ValueBag& bag : registry.get<ValueBags>(entity).Bags)
			{
				const std::optional<AssetGuid> script = script_guid_from_component(bag.Name);
				if (script && !m_Known.contains({ guid, *script }))
				{
					found.push_back(Instance{ guid, *script });
				}
			}
		}

		for (Instance& instance : found)
		{
			instance.Class = class_of(instance.Script);
			if (instance.Class != nullptr)
			{
				begin(std::move(instance));
			}
		}
	}

	void ScriptInstances::script_reloaded(std::span<const AssetRef> scripts)
	{
		for (const AssetRef& script : scripts)
		{
			m_BrokenScripts.erase(script.resolved());
		}

		// without on_destroy: the instance never finished starting, or stopped running on an error; discover() makes a new one
		for (std::vector<Instance>* instances : { &m_Instances, &m_SceneInstances })
		{
			std::erase_if(*instances, [&](const Instance& instance)
				{
					const bool reloaded = std::any_of(scripts.begin(), scripts.end(), [&](const AssetRef& script) { return script.resolved() == instance.Script; });
					if (reloaded && instance.Failed)
					{
						m_Known.erase({ instance.Entity, instance.Script });
						return true;
					}
					return false;
				});
		}
	}

	void ScriptInstances::update(float deltaTime)
	{
		if (m_Scene == nullptr)
		{
			return;
		}

		m_Context.DeltaTime = deltaTime;
		m_Context.TotalTime += deltaTime;

		reap();
		discover();

		for (std::vector<Instance>* instances : { &m_SceneInstances, &m_Instances })
		{
			// hooks may create and destroy entities but never start or reap instances, so the vectors keep their size
			for (size_t i = 0; i < instances->size(); i++)
			{
				Instance& instance = (*instances)[i];
				if (!instance.Failed && alive(instance))
				{
					call_hook(instance, "on_update", &deltaTime);
				}
			}
		}

		reap();
	}
}
