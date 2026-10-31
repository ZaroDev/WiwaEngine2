#include <pch.h>
#include "script_instances.h"

#include <algorithm>

#include <scene/reflection/value_bag.h>
#include "bindings/bindings.h"
#include "script_names.h"

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	ScriptInstances::ScriptInstances(ScriptRuntime& runtime) : m_Runtime(runtime)
	{
		bind_entity_handle(m_Runtime.state());
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
		const sol::object member = instance.Class->Class[hook];
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

	void ScriptInstances::begin(Instance instance)
	{
		instance.Self = sol::table::create(m_Runtime.state().lua_state());
		sol::table meta = sol::table::create(m_Runtime.state().lua_state());
		meta["__index"] = instance.Class->Class;
		instance.Self[sol::metatable_key] = meta;
		if (instance.Entity.valid())
		{
			instance.Self["entity"] = EntityHandle{ instance.Entity, &m_Context };
		}

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

	void ScriptInstances::update(float deltaTime)
	{
		if (m_Scene == nullptr)
		{
			return;
		}

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
