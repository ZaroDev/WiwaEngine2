#include <pch.h>
#include "scene_module.h"

#include <chrono>
#include <future>
#include <thread>

#include <assets/pak_source.h>
#include <core/engine.h>

#include "scene_components.h"

#define WI_SCENE_LOG(level, ...) WI_LOG(level, "Scene", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// How long a path that is not in the catalog is looked for (the editor writes the catalog after importing).
		constexpr auto PATH_LOOKUP_TIMEOUT = std::chrono::seconds(30);
		// How many entities of a scene that was replaced are destroyed per update.
		constexpr size_t RETIRE_CHUNK = 2000;
	}

	enum class BuildState : int
	{
		Running,
		Ready,
		Failed, // failed or cancelled; the progress object says which
	};

	struct SceneModule::LoadOp
	{
		AssetRef SceneRef{};
		std::string Path{};
		LoadMode Mode = LoadMode::Replace;
		Ref<JobProgress> Progress = CreateRef<JobProgress>();
		std::future<void> Worker{};

		std::atomic<BuildState> Build{ BuildState::Running };
		SceneBlueprint Blueprint{}; // written by the worker before Build becomes Ready

		// stage 3, main thread
		Ref<wiwa::Scene> Target{};
		Scope<SceneCommit> Commit{};
	};

	namespace
	{
		template<typename T>
		LoadState wait_loaded(const Handle<T>& handle, const JobProgress& progress)
		{
			LoadState state = handle.state();
			while (state == LoadState::Loading && !progress.cancel_requested())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				state = handle.state();
			}
			return state;
		}
	}

	void SceneModule::init()
	{
		register_scene_components();

		m_Assets = Engine::get().get_module<AssetManager>();
		m_Jobs = CreateScope<JobSystem>(2);
		m_Active = CreateRef<Scene>();

		const std::vector<std::string>& arguments = Engine::get().get_arguments();
		for (size_t i = 0; i + 1 < arguments.size(); i++)
		{
			if (arguments[i] == "--scene")
			{
				WI_SCENE_LOG(Info, "Loading scene {0}", arguments[i + 1]);
				m_StartupLoad = load_async(std::string_view(arguments[i + 1]));
			}
		}
	}

	void SceneModule::shutdown()
	{
		std::vector<Ref<LoadOp>> running;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			running = m_Running;
			m_Pending.reset();
		}

		for (const Ref<LoadOp>& op : running)
		{
			op->Progress->request_cancel();
		}
		for (const Ref<LoadOp>& op : running)
		{
			if (op->Worker.valid())
			{
				op->Worker.wait();
			}
		}

		m_Current.reset();
		m_Retired.clear();
		m_Jobs.reset();
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Running.clear();
		m_Active.reset();
	}

	Ref<Scene> SceneModule::get_active() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Active;
	}

	void SceneModule::set_active(Ref<Scene> scene)
	{
		Ref<Scene> old;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			old = std::move(m_Active);
			m_Active = std::move(scene);
		}
		retire(std::move(old));
	}

	bool SceneModule::is_loading() const
	{
		// every op that has not ended is in m_Running, from the moment it is asked for (m_Current belongs to the main thread)
		std::lock_guard<std::mutex> lock(m_Mutex);
		return std::any_of(m_Running.begin(), m_Running.end(), [](const Ref<LoadOp>& op) { return !op->Progress->finished(); });
	}

	SceneLoadStats SceneModule::get_last_load_stats() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Stats;
	}

	Ref<JobProgress> SceneModule::load_async(AssetRef scene, LoadMode mode)
	{
		Ref<LoadOp> op = CreateRef<LoadOp>();
		op->SceneRef = scene;
		op->Mode = mode;
		return start_load(std::move(op));
	}

	Ref<JobProgress> SceneModule::load_async(std::string_view sourcePath, LoadMode mode)
	{
		Ref<LoadOp> op = CreateRef<LoadOp>();
		op->Path = std::string(sourcePath);
		op->Mode = mode;
		return start_load(std::move(op));
	}

	Ref<JobProgress> SceneModule::start_load(Ref<LoadOp> op)
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			// one load at a time: everything before this one is cancelled
			// (the op being created is in m_Running until it ends, so this reaches m_Current too; m_Current itself belongs to the main thread)
			for (const Ref<LoadOp>& other : m_Running)
			{
				other->Progress->request_cancel();
			}
			if (m_Pending != nullptr)
			{
				// asked for and replaced before update() took it up: nobody else will finish its progress object
				m_Pending->Progress->set_stage("Cancelled");
				m_Pending->Progress->finish(false);
			}
			m_Pending = op;
			m_Running.push_back(op);
		}

		op->Worker = m_Jobs->submit([this, op]() { run_build(op); });
		return op->Progress;
	}

	void SceneModule::run_build(const Ref<LoadOp>& op)
	{
		JobProgress& progress = *op->Progress;
		auto fail = [&](const std::string& why)
		{
			progress.set_stage(progress.cancel_requested() ? "Cancelled" : "Failed: " + why);
			op->Build.store(BuildState::Failed, std::memory_order_release);
			progress.finish(false);
		};

		progress.begin("Loading scene", 1);

		AssetRef ref = op->SceneRef;
		if (!op->Path.empty())
		{
			// the catalog of a pack is complete from the start: a path that is not in it is not coming
			const bool packed = dynamic_cast<PakSource*>(m_Assets->get_source()) != nullptr;
			const auto deadline = std::chrono::steady_clock::now() + (packed ? std::chrono::seconds(0) : PATH_LOOKUP_TIMEOUT);
			std::optional<AssetRef> found;
			while (!(found = m_Assets->find_asset(op->Path)) && !progress.cancel_requested() && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			if (!found)
			{
				fail(op->Path + " is not in the asset catalog (is the path right, and was the scene imported?)");
				return;
			}
			ref = *found;
		}

		Handle<SceneData> scene = m_Assets->load<SceneData>(ref);
		if (wait_loaded(scene, progress) != LoadState::Ready)
		{
			fail("the scene asset could not be loaded");
			return;
		}
		const SceneData& data = *scene.get();
		progress.advance(data.Name);

		// the prefabs of the instances
		std::vector<std::pair<AssetRef, Handle<PrefabData>>> prefabs;
		for (const EntityRecord& entity : data.Entities)
		{
			for (const ComponentRecord& component : entity.Components)
			{
				if (component.Name != "PrefabInstance")
				{
					continue;
				}
				for (const FieldRecord& field : component.Fields)
				{
					const AssetRef* prefab = std::get_if<AssetRef>(&field.Value);
					if (field.Name == "Prefab" && prefab != nullptr && prefab->valid()
						&& std::none_of(prefabs.begin(), prefabs.end(), [&](const auto& known) { return known.first == *prefab; }))
					{
						prefabs.emplace_back(*prefab, m_Assets->load<PrefabData>(*prefab));
					}
				}
			}
		}
		for (auto& [prefabRef, handle] : prefabs)
		{
			if (wait_loaded(handle, progress) == LoadState::Loading)
			{
				fail("cancelled");
				return;
			}
		}

		SceneBlueprint blueprint = build_blueprint(data, [&](const AssetRef& wanted) -> const PrefabData*
			{
				for (const auto& [prefabRef, handle] : prefabs)
				{
					if (prefabRef == wanted)
					{
						return handle.get();
					}
				}
				return nullptr;
			});
		for (const std::string& warning : blueprint.Warnings)
		{
			WI_SCENE_LOG(Warning, "{0}: {1}", data.Name, warning);
		}
		if (progress.cancel_requested())
		{
			fail("cancelled");
			return;
		}

		// the assets the entities use, loading in parallel while the scene waits
		Ref<JobProgress> assets = m_Assets->preload(blueprint.References);
		progress.begin("Loading assets", assets->total());
		u32 reportedTotal = assets->total();
		u32 reportedDone = 0;
		for (;;)
		{
			const u32 total = assets->total();
			if (total > reportedTotal)
			{
				progress.add_total(total - reportedTotal);
				reportedTotal = total;
			}
			for (const u32 done = assets->done(); reportedDone < done; reportedDone++)
			{
				progress.advance();
			}

			if (assets->finished() || progress.cancel_requested())
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		if (progress.cancel_requested())
		{
			fail("cancelled");
			return;
		}
		if (!assets->succeeded())
		{
			WI_SCENE_LOG(Warning, "{0}: some of the assets it refers to could not be loaded", data.Name);
		}

		op->Blueprint = std::move(blueprint);
		op->Build.store(BuildState::Ready, std::memory_order_release);
	}

	void SceneModule::adopt_pending()
	{
		Ref<LoadOp> next;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			next = std::move(m_Pending);
			m_Pending.reset();
			std::erase_if(m_Running, [](const Ref<LoadOp>& op) { return op->Worker.valid() && op->Worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready && op->Progress->finished(); });
		}

		if (next == nullptr)
		{
			return;
		}

		if (m_Current != nullptr)
		{
			discard_commit(*m_Current);
		}
		m_Current = std::move(next);
	}

	void SceneModule::discard_commit(LoadOp& op)
	{
		op.Progress->request_cancel();

		if (op.Commit != nullptr)
		{
			if (op.Mode == LoadMode::Additive)
			{
				Ref<Scene> active = get_active();
				for (const entt::entity entity : op.Commit->entities())
				{
					if (active->registry().valid(entity))
					{
						active->destroy(entity);
					}
				}
			}
			op.Commit.reset();
		}

		if (op.Target != nullptr)
		{
			retire(std::move(op.Target));
		}

		if (!op.Progress->finished())
		{
			op.Progress->set_stage("Cancelled");
			op.Progress->finish(false);
		}
	}

	void SceneModule::retire(Ref<Scene> scene)
	{
		m_Retired.push_back(std::move(scene));
	}

	void SceneModule::update(float deltaTime)
	{
		adopt_pending();
		update_commit();

		// a scene that was replaced is taken down a part at a time
		if (!m_Retired.empty())
		{
			Ref<Scene>& scene = m_Retired.front();
			if (scene.use_count() > 1 || scene->destroy_some(RETIRE_CHUNK) == 0)
			{
				m_Retired.erase(m_Retired.begin());
			}
		}
	}

	void SceneModule::update_commit()
	{
		if (m_Current == nullptr)
		{
			return;
		}

		LoadOp& op = *m_Current;
		if (op.Progress->cancel_requested())
		{
			discard_commit(op);
			m_Current.reset();
			return;
		}

		const BuildState build = op.Build.load(std::memory_order_acquire);
		if (build == BuildState::Running)
		{
			return;
		}
		if (build == BuildState::Failed)
		{
			m_Current.reset();
			return;
		}

		if (op.Commit == nullptr)
		{
			op.Target = op.Mode == LoadMode::Additive ? get_active() : CreateRef<Scene>();
			op.Progress->begin("Creating entities", static_cast<u32>(op.Blueprint.Entities.size()));
			op.Commit = CreateScope<SceneCommit>(*op.Target, std::move(op.Blueprint));
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stats = SceneLoadStats{};
		}

		const auto start = std::chrono::steady_clock::now();
		const size_t before = op.Commit->created();
		const bool done = op.Commit->step(m_CommitBudgetMs);
		const float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		for (size_t i = before; i < op.Commit->created(); i++)
		{
			op.Progress->advance();
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stats.CommitTicks++;
			m_Stats.LongestCommitMs = std::max(m_Stats.LongestCommitMs, elapsed);
		}

		if (!done)
		{
			return;
		}

		for (const std::string& warning : op.Commit->report().Warnings)
		{
			WI_SCENE_LOG(Warning, "{0}", warning);
		}

		op.Target->update_world_transforms();
		if (op.Mode == LoadMode::Replace)
		{
			Ref<Scene> old;
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				old = std::move(m_Active);
				m_Active = op.Target;
			}
			retire(std::move(old));
		}

		WI_SCENE_LOG(Info, "Scene {0} loaded: {1} entities", op.Commit->blueprint().Name, op.Commit->created());
		op.Commit.reset();
		op.Target.reset();
		op.Progress->finish(true);
		m_Current.reset();
	}
}
