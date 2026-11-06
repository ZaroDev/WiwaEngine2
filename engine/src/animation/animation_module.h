#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <core/module.h>
#include <scene/scene.h>

#include "animator_instance.h"

namespace wiwa
{
	// A clip event that fired, for the scripts of the next frame.
	struct QueuedAnimationEvent
	{
		EntityGuid Animator{};
		ClipEvent Event{};
	};

	// Plays the Animators of the active scene. Runs after the scripts (priority 10), so the evaluation sees what they set this frame:
	//   update():            every Animator gets an AnimatorInstance while the game clock plays; the instances advance, evaluate, build their
	//                        palettes, write the AnimationSocket entities and queue the clip events
	//   pre_render_update(): the palettes are given to the renderer (see animation.md)
	// Instances belong to a scene object: another scene (play mode's clone, a loaded scene) starts with none. Main thread only.
	class AnimationModule : public IModule
	{
	public:
		AnimationModule() = default;
		virtual ~AnimationModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const override { return "AnimationModule"; }
		virtual int get_priority() const override { return 10; } // after the scripts (0)

		// How skeletons and clips are found. The default asks the AssetManager; tests and tools put their own.
		void set_sources(AnimationSources sources) { m_Sources = std::move(sources); }

		// The work of update() on a given scene: playing is whether the game clock runs. Public for tests and tools.
		void tick(const Ref<Scene>& scene, float deltaTime, bool playing);

		// The instance of an Animator entity of the scene last ticked; null when it has none (not playing, disabled, no such entity).
		AnimatorInstance* instance_for(entt::entity entity);
		const AnimatorInstance* instance_for(entt::entity entity) const;

		// For scripts, which run before tick(): the instance of an Animator entity of this scene, made now if the game plays and there is none yet,
		// so that a script can use its animator in the frame the scene starts. Null for an entity without an enabled Animator, or when the game does
		// not play and no instance was made.
		AnimatorInstance* acquire(const Ref<Scene>& scene, entt::entity entity, bool playing);

		// An instance in edit mode, for the editor's preview: made when asked for, advanced by tick() even when the game does not play,
		// never saved. Null when the entity has no enabled Animator. drop_previews() ends them all.
		AnimatorInstance* preview(entt::entity entity);
		void set_preview_paused(entt::entity entity, bool paused);
		void drop_previews();

		// The events that fired since the last call. The scripts hear them at the start of the next frame.
		std::vector<QueuedAnimationEvent> take_events();

		// The world matrix of a joint as of the last evaluation; identity when there is no such animator or joint.
		glm::mat4 joint_world(entt::entity animator, const std::string& joint) const;

		size_t instance_count() const { return m_Instances.size(); }

	private:
		struct Entry
		{
			std::unique_ptr<AnimatorInstance> Instance{};
			EntityGuid Guid{};
			bool Preview = false;
			bool Paused = false;
		};

		void drain_reloads();
		void adopt(const Ref<Scene>& scene);
		Entry* make_entry(entt::registry& registry, entt::entity entity);
		void write_sockets(Scene& scene);

		AssetManager* m_Assets = nullptr;
		AnimationSources m_Sources{};
		Ref<Scene> m_Scene{};
		std::unordered_map<entt::entity, Entry> m_Instances{};
		std::vector<QueuedAnimationEvent> m_Events{};

		// the assets the default sources loaded: kept for as long as an instance may point into them
		std::unordered_map<AssetGuid, Handle<SkeletonData>> m_SkeletonHandles{};
		std::unordered_map<AssetGuid, Handle<AnimationClipData>> m_ClipHandles{};
		u32 m_ReloadListener = 0;
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadQueue{};
	};
}
