#include <core/engine.h>
#include <core/log.h>
#include <core/perf_stats.h>
#include <renderer/render_module.h>
#include <scene/scene_module.h>
#include <game_module.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

using wiwa::LogLevel; // the WI_LOG macro names its level through it
// A game on the engine. It has no editor in it: scenes and assets come from the pack (assets.wpak) next to the program.
//
//   wiwa_game                                        load the default scene of the pack (chosen in the editor's Build Settings)
//   wiwa_game --scene Assets/scenes/level.wscene     load that scene instead (it must be in the pack)
//   wiwa_game --scene <path> --scene-smoke-load      ...and quit when it is loaded: exit code 0 if it worked, 1 if not
//   wiwa_game --scene <path> --bench [seconds]       ...and measure: waits for the scene and its uploads, runs the seconds (default 20) with vsync off, logs the average
//                                                    FPS, the 1% and 0.1% lows, the worst frame, the renderer's numbers and the memory, and quits: exit code 0 when the
//                                                    average reaches --bench-target <fps> (default 60), else 1
//   wiwa_game --debug-draw                           show the debug renderer's shapes (Lua debug_draw, physics bodies); a game starts with them off
class Game final : public wiwa::Engine
{
public:
	Game(int argc, char** argv) : wiwa::Engine(argc, argv) {}

protected:
	virtual void on_init() override
	{
		m_ModuleManager.add_module<GameModule>();
		for (const std::string& argument : get_arguments())
		{
			m_QuitWhenLoaded = m_QuitWhenLoaded || argument == "--scene-smoke-load";
		}

		const std::vector<std::string>& arguments = get_arguments();
		for (size_t i = 0; i < arguments.size(); i++)
		{
			const auto number = [&](double fallback)
			{
				if (i + 1 < arguments.size() && !arguments[i + 1].empty() && std::isdigit(static_cast<unsigned char>(arguments[i + 1][0])))
				{
					return std::atof(arguments[i + 1].c_str());
				}
				return fallback;
			};
			if (arguments[i] == "--bench")
			{
				m_Bench = true;
				m_BenchSeconds = number(20.0);
			}
			else if (arguments[i] == "--bench-target")
			{
				m_BenchTarget = number(60.0);
			}
		}
		if (m_Bench)
		{
			if (wiwa::RenderModule* render = get_module<wiwa::RenderModule>())
			{
				render->set_vsync(false); // measure what the machine can do, not the refresh rate
			}
		}
	}

	// --bench: load, settle, measure, report.
	void update_bench(float deltaTime)
	{
		const Ref<wiwa::JobProgress> load = get_module<wiwa::SceneModule>()->get_startup_load();
		if (load == nullptr || !load->finished())
		{
			m_BenchStart = std::chrono::steady_clock::now();
			if (load != nullptr && load->finished() && !load->succeeded())
			{
				WI_LOG(Error, "Bench", "the scene did not load");
				set_exit_code(1);
				request_quit();
			}
			return;
		}
		if (!load->succeeded())
		{
			WI_LOG(Error, "Bench", "the scene did not load");
			set_exit_code(1);
			request_quit();
			return;
		}

		const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_BenchStart).count();
		wiwa::RenderModule* render = get_module<wiwa::RenderModule>();
		const wiwa::RenderSnapshot snapshot = render != nullptr ? render->get_snapshot() : wiwa::RenderSnapshot{};

		if (!m_BenchMeasuring)
		{
			// settled: the asset uploads are spread over frames, so wait until the cache has nothing pending for two seconds (at most 120 s)
			m_BenchQuiet = snapshot.CachePending == 0 ? m_BenchQuiet + deltaTime * 0.001f : 0.0f; // deltaTime is in milliseconds
			if ((m_BenchQuiet >= 2.0f && elapsed >= 4.0) || elapsed >= 120.0)
			{
				m_BenchMeasuring = true;
				m_BenchStart = std::chrono::steady_clock::now();
				m_BenchFrames.reserve(static_cast<size_t>(m_BenchSeconds * 400));
				WI_LOG(Info, "Bench", "measuring {0} s ({1} instances, {2} lights, {3} textures, {4} pending)", m_BenchSeconds, snapshot.SceneInstances, snapshot.SceneLights, snapshot.CacheTextures, snapshot.CachePending);
			}
			return;
		}

		m_BenchFrames.push_back(deltaTime); // milliseconds
		m_BenchRenderMs += snapshot.FrameMs;
		m_BenchVisible = std::max(m_BenchVisible, snapshot.VisibleInstances);
		m_BenchDraws = std::max(m_BenchDraws, snapshot.DrawCalls);
		for (int level = 0; level < 4; level++)
		{
			m_BenchLods[level] = std::max(m_BenchLods[level], snapshot.LodPrimitives[level]);
		}

		if (elapsed < m_BenchSeconds)
		{
			return;
		}

		std::vector<float> sorted = m_BenchFrames;
		std::sort(sorted.begin(), sorted.end());
		double total = 0.0;
		for (float ms : sorted)
		{
			total += ms;
		}
		const double averageMs = total / static_cast<double>(sorted.size());
		const auto percentile = [&](double fraction) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(static_cast<double>(sorted.size()) * fraction))]; };
		const double fps = 1000.0 / averageMs;
		const wiwa::PerfStats& perf = get_perf();
		WI_LOG(Info, "Bench", "{0} frames, average {1:.1f} FPS ({2:.2f} ms), 1% low {3:.1f} FPS, 0.1% low {4:.1f} FPS, worst {5:.1f} ms", sorted.size(), fps, averageMs,
			1000.0 / percentile(0.99), 1000.0 / percentile(0.999), sorted.back());
		WI_LOG(Info, "Bench", "render thread {0:.2f} ms average, up to {1} visible instances and {2} draw calls, {3} instances in the scene, {4} lights, {5} cache bytes",
			m_BenchRenderMs / static_cast<double>(sorted.size()), m_BenchVisible, m_BenchDraws, snapshot.SceneInstances, snapshot.SceneLights, snapshot.CacheBytes);
		WI_LOG(Info, "Bench", "{0} on {1}, VRAM up to {2:.0} MB, RAM up to {3:.0} MB", snapshot.Api, snapshot.Adapter, perf.vram_used_mb().max(), perf.process_ram_mb().max());
		WI_LOG(Info, "Bench", "most primitives drawn per level of detail: full {0}, then {1}, {2}, {3}", m_BenchLods[0], m_BenchLods[1], m_BenchLods[2], m_BenchLods[3]);
		const bool reached = fps >= m_BenchTarget;
		WI_LOG(Info, "Bench", "{0} the target of {1:.0} FPS", reached ? "reached" : "MISSED", m_BenchTarget);
		set_exit_code(reached ? 0 : 1);
		request_quit();
	}

	virtual void on_update(float deltaTime) override
	{
		if (m_Bench)
		{
			update_bench(deltaTime);
			return;
		}
		if (!m_QuitWhenLoaded)
		{
			return;
		}

		const Ref<wiwa::JobProgress> load = get_module<wiwa::SceneModule>()->get_startup_load();
		if (load == nullptr)
		{
			set_exit_code(1); // asked to quit after loading, but there is no --scene
			request_quit();
		}
		else if (load->finished())
		{
			set_exit_code(load->succeeded() ? 0 : 1);
			request_quit();
		}
	}

private:
	bool m_QuitWhenLoaded = false;

	bool m_Bench = false;
	bool m_BenchMeasuring = false;
	double m_BenchSeconds = 20.0;
	double m_BenchTarget = 60.0;
	float m_BenchQuiet = 0.0f;
	std::chrono::steady_clock::time_point m_BenchStart = std::chrono::steady_clock::now();
	std::vector<float> m_BenchFrames{};
	double m_BenchRenderMs = 0.0;
	wiwa::u32 m_BenchVisible = 0;
	wiwa::u32 m_BenchDraws = 0;
	wiwa::u32 m_BenchLods[4]{};
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Game(argc, argv);
}
