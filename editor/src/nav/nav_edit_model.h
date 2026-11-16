#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <entt/entt.hpp>

#include <scene/scene.h>

#include "../asset_pipeline/nav_bake.h"
#include "../asset_pipeline/nav_geometry.h"

namespace wiwa
{
	// The state behind the Navigation panel, without any ImGui: a bake that runs on a worker thread from a snapshot of the scene, its progress and log,
	// and the check whether a baked mesh still matches the scene. Main thread, except for the worker it owns. The model writes the .wnav file; giving the
	// file an asset reference (the import) and putting that in the volume is the panel's business (it needs the database and the undoable scene edit).
	class NavEditModel
	{
	public:
		enum class BakeState { Idle, Running, Done, Failed };

		// `meshes` must outlive the model; the worker reads through it while a bake runs.
		explicit NavEditModel(INavMeshSource& meshes);
		~NavEditModel();

		NavEditModel(const NavEditModel&) = delete;
		NavEditModel& operator=(const NavEditModel&) = delete;

		// Captures the volume (on this thread) and bakes it on a worker; the .wnav is written to outFile only when the bake succeeds (a failed or cancelled
		// bake leaves the previous file alone). False, with error() saying why, if a bake is running or the entity is not a volume.
		bool start_bake(Scene& scene, entt::entity volume, const std::filesystem::path& outFile);
		void cancel();
		// Call every frame: collects the worker when it is done.
		void poll();
		// Waits for the worker (checks and shutdown).
		void wait();

		BakeState state() const { return static_cast<BakeState>(m_State.load()); }
		float progress() const { return m_Progress.load(); }
		std::string phase() const;
		std::string log() const;
		const std::string& error() const { return m_Error; }
		// Numbers of the last bake that finished Ok (meaningless otherwise).
		const NavBakeResult& last() const { return m_Last; }
		const std::filesystem::path& out_file() const { return m_OutFile; }
		entt::entity volume() const { return m_Volume; }

		// Whether `stored` was baked from other input than the volume would give now (the geometry or the settings changed). Loads the meshes on this
		// thread, so it is for a button, not for every frame. Nullopt while a bake runs or if the volume cannot be read.
		std::optional<bool> is_stale(Scene& scene, entt::entity volume, const NavMeshData& stored);

	private:
		void run(NavSnapshot snapshot);
		void append_log(const std::string& line);

		INavMeshSource& m_Meshes;
		std::thread m_Worker{};
		std::atomic<int> m_State{ static_cast<int>(BakeState::Idle) };
		std::atomic<float> m_Progress{ 0.0f };
		std::atomic<bool> m_Cancel{ false };
		std::atomic<bool> m_Finished{ false }; // the worker has reached its end (join it)

		mutable std::mutex m_Mutex{};
		std::string m_Phase{};
		std::string m_Log{};
		std::string m_WorkerError{};
		NavBakeResult m_WorkerResult{};

		std::string m_Error{};
		NavBakeResult m_Last{};
		std::filesystem::path m_OutFile{};
		entt::entity m_Volume{ entt::null };
	};
}
