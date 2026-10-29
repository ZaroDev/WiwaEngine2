#pragma once

#include <atomic>
#include <mutex>
#include <string_view>
#include <vector>

#include <assets/asset_manager.h>
#include <core/job_progress.h>
#include <core/module.h>

#include "scene.h"
#include "scene_loader.h"

namespace wiwa
{
	enum class LoadMode
	{
		Replace,  // the new scene takes the place of the active one once it is complete
		Additive, // the new entities join the active scene
	};

	struct SceneLoadStats
	{
		float LongestCommitMs = 0.0f; // the longest time one update() spent creating entities
		u32 CommitTicks = 0;          // how many updates it took
	};

	// Owns the active scene and loads scenes without stalling a frame. A load has three stages:
	//  1. on a worker: read the SceneData, expand prefab instances into a SceneBlueprint (data only);
	//  2. on a worker: preload every asset the blueprint refers to, through the AssetManager;
	//  3. on the main thread, a little each update() (see set_commit_budget_ms): create the entities.
	// Only stage 3 touches a registry. `--scene <path>` loads a scene when the module starts.
	class SceneModule : public IModule
	{
	public:
		SceneModule() = default;
		virtual ~SceneModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "SceneModule"; }
		virtual int get_priority() const override { return -80; }

		// Makes this scene the active one at once (a new empty scene in the editor). Main thread only; the old one is taken down over
		// the next updates.
		void set_active(Ref<Scene> scene);

		// The scene in use. Never null (an empty scene to start with). A Replace load swaps it for another object in one step;
		// the old one lives on while someone holds it.
		Ref<Scene> get_active() const;

		// Starts loading and returns at once; the progress object shows the stages and ends Succeeded or Failed (the reason is in its
		// stage text). Asking for another load cancels the one in progress. May be called from any thread.
		Ref<JobProgress> load_async(AssetRef scene, LoadMode mode = LoadMode::Replace);
		// The scene asset whose source file is at that path ("Assets/scenes/level.wscene"), found in the asset catalog; a path that is
		// not catalogued yet is looked for again for a while (the editor writes the catalog when its import has finished).
		Ref<JobProgress> load_async(std::string_view sourcePath, LoadMode mode = LoadMode::Replace);

		// The load that `--scene <path>` started when the module did; null if there was no such argument.
		Ref<JobProgress> get_startup_load() const { return m_StartupLoad; }

		// How long one update() may spend creating entities. At least one entity is created per update, whatever the budget.
		void set_commit_budget_ms(float milliseconds) { m_CommitBudgetMs = milliseconds; }
		bool is_loading() const;
		SceneLoadStats get_last_load_stats() const;

	private:
		struct LoadOp;

		Ref<JobProgress> start_load(Ref<LoadOp> op);
		void run_build(const Ref<LoadOp>& op);
		void adopt_pending();
		void update_commit();
		void discard_commit(LoadOp& op);
		void retire(Ref<Scene> scene);

		AssetManager* m_Assets = nullptr;
		Scope<JobSystem> m_Jobs{};
		Ref<JobProgress> m_StartupLoad{};

		mutable std::mutex m_Mutex{};
		Ref<Scene> m_Active{};
		Ref<LoadOp> m_Pending{};              // asked for, not taken up by update() yet
		std::vector<Ref<LoadOp>> m_Running{}; // every op whose worker has not finished (cancelled ones too)
		SceneLoadStats m_Stats{};

		// main thread only (nothing else may touch m_Current: load_async runs on any thread and works through m_Pending and m_Running)
		Ref<LoadOp> m_Current{};
		std::vector<Ref<Scene>> m_Retired{};
		std::atomic<float> m_CommitBudgetMs{ 2.0f };
	};
}
