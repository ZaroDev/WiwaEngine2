#pragma once

#include <entt/entt.hpp>

#include <scene/entity_guid.h>
#include <scene/scene.h>

namespace wiwa
{
	class AssetManager;
	class DebugDraw;
	class InputModule;
	class PhysicsModule;
	class SceneModule;
	class UiHandlerTable;
	class UiModule;
	class ScriptRuntime;

	// What the Lua bindings reach besides the Lua state: the scene scripts are running on. Set by ScriptInstances.
	struct ScriptContext
	{
		Scene* ActiveScene = nullptr;
		ScriptRuntime* Runtime = nullptr;
		AssetManager* Assets = nullptr; // null where there is no such module (a test): the calls that need them are Lua errors
		SceneModule* Scenes = nullptr;
		DebugDraw* Debug = nullptr;     // null without a renderer: debug drawing is ignored
		UiModule* Ui = nullptr;         // null where there is no such module (a test): the UI calls are Lua errors
		PhysicsModule* Physics = nullptr; // null where there is no such module (a test): the physics calls are Lua errors
		InputModule* Input = nullptr;   // null where there is no such module (a test): the input calls are Lua errors
		bool GamePlaying = true;        // false while the game clock does not play: the input queries then read false / zero
		bool InputListening = false;    // a Lua function waits in input.listen (cancelled when the scripts stop)
		UiHandlerTable* UiHandlers = nullptr; // the Lua functions that listen to UI events, owned by the script instances
		float DeltaTime = 0.0f;         // seconds, of the update that is running
		double TotalTime = 0.0;         // seconds of updates so far
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
