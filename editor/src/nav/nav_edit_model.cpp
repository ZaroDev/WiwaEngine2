#include <editor_common.h>
#include "nav_edit_model.h"

#include <core/engine.h>
#include <nav/nav_module.h>

namespace wiwa
{
	namespace
	{
		// The project areas the running editor uses (the panel applied them): a bake tags removed areas as Walkable. Null when there is no module (a check).
		const NavSettings* current_nav_settings()
		{
			const NavModule* module = Engine::get().get_module<NavModule>();
			return module != nullptr ? &module->settings() : nullptr;
		}
	}

	NavEditModel::NavEditModel(INavMeshSource& meshes) : m_Meshes(meshes) {}

	NavEditModel::~NavEditModel()
	{
		cancel();
		wait();
	}

	std::string NavEditModel::phase() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Phase;
	}

	std::string NavEditModel::log() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Log;
	}

	void NavEditModel::append_log(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Log += line;
		m_Log += '\n';
	}

	bool NavEditModel::start_bake(Scene& scene, entt::entity volume, const std::filesystem::path& outFile)
	{
		if (state() == BakeState::Running)
		{
			m_Error = "a bake is already running";
			return false;
		}
		wait(); // a finished worker that has not been collected
		m_Meshes.refresh();

		std::string error;
		NavSnapshot snapshot = capture_nav_snapshot(scene, volume, &error, current_nav_settings());
		if (!error.empty())
		{
			m_Error = error;
			return false;
		}

		m_Error.clear();
		m_OutFile = outFile;
		m_Volume = volume;
		m_Cancel = false;
		m_Finished = false;
		m_Progress = 0.0f;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Log.clear();
			m_Phase = "Gathering geometry";
			m_WorkerError.clear();
			m_WorkerResult = {};
		}
		m_State = static_cast<int>(BakeState::Running);
		m_Worker = std::thread([this, snapshot = std::move(snapshot)]() mutable { run(std::move(snapshot)); });
		return true;
	}

	void NavEditModel::run(NavSnapshot snapshot)
	{
		std::string error;
		append_log("Gathering geometry from " + std::to_string(snapshot.Items.size()) + " meshes");
		NavBakeInput input = build_nav_input(snapshot, m_Meshes, &m_Cancel, &error);
		bool ok = false;
		NavBakeResult result;
		if (m_Cancel.load())
		{
			error = "cancelled";
		}
		else if (input.Indices.empty())
		{
			if (error.empty())
			{
				error = "no geometry touches the volume";
			}
		}
		else
		{
			append_log(std::to_string(input.Indices.size() / 3) + " triangles");
			result = bake_navmesh(input, &m_Cancel, [this](float fraction, const char* phase)
			{
				m_Progress = fraction;
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Phase = phase;
			});
			if (!result.Ok)
			{
				error = result.Error;
			}
			else if (m_Cancel.load())
			{
				error = "cancelled";
			}
			else
			{
				std::string writeError;
				if (write_nav_file(m_OutFile, result.Mesh, writeError))
				{
					ok = true;
					append_log("Baked " + std::to_string(result.PolyCount) + " polygons, " + std::to_string(result.VertCount) + " vertices in " + std::to_string(result.TileCount) + " tiles, " + std::to_string(result.Seconds) + " s");
				}
				else
				{
					error = writeError;
				}
			}
		}

		if (!ok)
		{
			append_log("Failed: " + error);
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_WorkerError = error;
			m_WorkerResult = std::move(result);
			m_Phase = ok ? "Done" : "Failed";
		}
		if (ok)
		{
			m_Progress = 1.0f;
		}
		m_State = static_cast<int>(ok ? BakeState::Done : BakeState::Failed);
		m_Finished = true;
	}

	void NavEditModel::cancel()
	{
		m_Cancel = true;
	}

	void NavEditModel::poll()
	{
		if (m_Finished.load() && m_Worker.joinable())
		{
			m_Worker.join();
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Error = m_WorkerError;
			if (state() == BakeState::Done)
			{
				m_Last = std::move(m_WorkerResult);
			}
		}
	}

	void NavEditModel::wait()
	{
		if (m_Worker.joinable())
		{
			m_Worker.join();
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Error = m_WorkerError;
			if (state() == BakeState::Done)
			{
				m_Last = std::move(m_WorkerResult);
			}
		}
	}

	std::optional<bool> NavEditModel::is_stale(Scene& scene, entt::entity volume, const NavMeshData& stored)
	{
		if (state() == BakeState::Running)
		{
			return std::nullopt;
		}
		m_Meshes.refresh();
		std::string error;
		const NavSnapshot snapshot = capture_nav_snapshot(scene, volume, &error, current_nav_settings());
		if (!error.empty())
		{
			return std::nullopt;
		}
		const NavBakeInput input = build_nav_input(snapshot, m_Meshes, nullptr, &error);
		if (input.Indices.empty())
		{
			return std::nullopt;
		}
		return nav_input_hash(input) != stored.InputHash;
	}
}
