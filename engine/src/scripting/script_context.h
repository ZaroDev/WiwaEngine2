#pragma once

#include <entt/entt.hpp>

#include <functional>

#include <scene/entity_guid.h>
#include <scene/scene.h>

namespace wiwa
{
	class AnimHandlerTable;
	class AnimationModule;
	class AudioModule;
	class AssetManager;
	class DebugDraw;
	class AIModule;
	class InputModule;
	class NavModule;
	class PhysicsModule;
	class SceneModule;
	class PrefabSpawner;
	class UiHandlerTable;
	class UiModule;
	class VideoModule;
	class ScriptRuntime;

	// What the Lua bindings reach besides the Lua state: the scene scripts are running on. Set by ScriptInstances.
	struct ScriptContext
	{
		Scene* ActiveScene = nullptr;
		ScriptRuntime* Runtime = nullptr;
		AssetManager* Assets = nullptr; // null where there is no such module (a test): the calls that need them are Lua errors
		SceneModule* Scenes = nullptr;
		PrefabSpawner* Prefabs = nullptr; // where instantiate_prefab queues instances: the scene module's spawner (a test gives its own)
		DebugDraw* Debug = nullptr;     // null without a renderer: debug drawing is ignored
		UiModule* Ui = nullptr;         // null where there is no such module (a test): the UI calls are Lua errors
		PhysicsModule* Physics = nullptr; // null where there is no such module (a test): the physics calls are Lua errors
		InputModule* Input = nullptr;   // null where there is no such module (a test): the input calls are Lua errors
		AnimationModule* Animation = nullptr; // null where there is no such module (a test): Animation.animator then finds nothing
		AIModule* AI = nullptr;         // null where there is no such module (a test): the AI calls are Lua errors
		NavModule* Nav = nullptr;       // null where there is no such module (a test): the Nav calls are Lua errors
		AudioModule* Audio = nullptr;   // null where there is no such module (a test): the Audio calls are Lua errors
		VideoModule* Video = nullptr;   // null where there is no such module (a test): the video_* methods of a Video widget are Lua errors
		AnimHandlerTable* AnimHandlers = nullptr; // the Lua functions that listen to clip events, owned by the script instances
		Ref<Scene> ActiveSceneRef{};    // the same scene as ActiveScene, for the calls that need to share it
		bool GamePlaying = true;        // false while the game clock does not play: the input queries then read false / zero
		bool InputListening = false;    // a Lua function waits in input.listen (cancelled when the scripts stop)
		UiHandlerTable* UiHandlers = nullptr; // the Lua functions that listen to UI events, owned by the script instances
		std::function<void()> RequestQuit{}; // what quit() calls; empty where the host has no such thing (a test): quit() is then a Lua error
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
