#pragma once

#include <entt/entt.hpp>

#include <scene/entity_guid.h>
#include <scene/scene.h>

namespace wiwa
{
	// What the Lua bindings reach besides the Lua state: the scene scripts are running on. Set by ScriptInstances.
	struct ScriptContext
	{
		Scene* ActiveScene = nullptr;
	};

	// How a script holds an entity: by guid, resolved through the active scene on every use. A destroyed entity (or a handle kept
	// across a scene change) is simply not valid; the handle never points at memory that was reused.
	struct EntityHandle
	{
		EntityGuid Guid{};
		ScriptContext* Context = nullptr;

		entt::entity resolve() const
		{
			if (Context == nullptr || Context->ActiveScene == nullptr || !Guid.valid())
			{
				return entt::null;
			}
			return Context->ActiveScene->find(Guid);
		}

		bool is_valid() const { return resolve() != entt::null; }
	};
}
