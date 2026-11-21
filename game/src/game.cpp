#include <core/engine.h>
#include <core/bench_recorder.h>
#include <core/log.h>
#include <core/perf_stats.h>
#include <renderer/render_module.h>
#include <scene/scene_module.h>
#include <game_module.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using wiwa::LogLevel; // the WI_LOG macro names its level through it

// A game on the engine. It has no editor in it: scenes and assets come from the pack (assets.wpak) next to the program.
//
//   wiwa_game                                        load the default scene of the pack (chosen in the editor's Build Settings)
//   wiwa_game --scene Assets/scenes/level.wscene     load that scene instead (it must be in the pack)
//   wiwa_game --scene <path> --scene-smoke-load      ...and quit when it is loaded: exit code 0 if it worked, 1 if not
//   wiwa_game --scene <path> --bench [seconds]       ...and measure: waits for the scene and its uploads, runs the seconds (default 20) with vsync off, records every
//                                                    frame, logs the average FPS, the 1% and 0.1% lows and the worst frame, and quits: exit code 0 when the average
//                                                    reaches --bench-target <fps> (default 60), else 1
//     --bench-out <file.json>                        also writes the frames (frame time, memory, per-module CPU time, per-pass GPU time, draw counts) for
//                                                    scripts/bench_report.py, which makes the HTML report (docs/superpowers/documentation/benchmarking.md)
//     --bench-label <text>                           names the run in the report
//     --bench-meta key=value                         adds a line to the report header (repeatable): the runner passes the scale of the scene, the commit...
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
			const bool hasValue = i + 1 < arguments.size();
			const auto number = [&](double fallback)
			{
				if (hasValue && !arguments[i + 1].empty() && std::isdigit(static_cast<unsigned char>(arguments[i + 1][0])))
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
			else if (arguments[i] == "--bench-out" && hasValue)
			{
				m_BenchOut = arguments[i + 1];
			}
			else if (arguments[i] == "--bench-label" && hasValue)
			{
				m_BenchLabel = arguments[i + 1];
			}
			else if (arguments[i] == "--bench-meta" && hasValue)
			{
				const std::string& pair = arguments[i + 1];
				const size_t equals = pair.find('=');
				if (equals != std::string::npos)
				{
					m_BenchExtra[pair.substr(0, equals)] = pair.substr(equals + 1);
				}
			}
			else if (arguments[i] == "--scene" && hasValue)
			{
				m_BenchScene = arguments[i + 1];
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
	void update_bench(float deltaTime) // milliseconds
	{
		const Ref<wiwa::JobProgress> load = get_module<wiwa::SceneModule>()->get_startup_load();
		if (load == nullptr || !load->finished())
		{
			m_BenchStart = std::chrono::steady_clock::now();
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
			m_BenchQuiet = snapshot.CachePending == 0 ? m_BenchQuiet + deltaTime * 0.001f : 0.0f;
			if ((m_BenchQuiet >= 2.0f && elapsed >= 4.0) || elapsed >= 120.0)
			{
				m_BenchMeasuring = true;
				m_BenchStart = std::chrono::steady_clock::now();

				wiwa::BenchRecorder::Meta meta;
				meta.Label = m_BenchLabel;
				meta.Scene = m_BenchScene;
#if defined(WI_DEBUG)
				meta.Config = "Debug";
#elif defined(WI_SHIPPING)
				meta.Config = "Shipping";
#else
				meta.Config = "Release";
#endif
				meta.Seconds = m_BenchSeconds;
				meta.TargetFps = m_BenchTarget;
				meta.Vsync = false;
				meta.Extra = m_BenchExtra;
				m_Recorder.begin(meta);
				WI_LOG(Info, "Bench", "measuring {0} s ({1} instances, {2} lights, {3} textures, {4} pending)", m_BenchSeconds, snapshot.SceneInstances, snapshot.SceneLights, snapshot.CacheTextures, snapshot.CachePending);
			}
			return;
		}

		m_Recorder.sample(deltaTime, get_perf(), snapshot);
		if (elapsed < m_BenchSeconds)
		{
			return;
		}

		const wiwa::PerfStats& perf = get_perf();
		const double fps = m_Recorder.average_fps();
		WI_LOG(Info, "Bench", "{0} frames, average {1:.1f} FPS ({2:.2f} ms), 1% low {3:.1f} FPS, 0.1% low {4:.1f} FPS, worst {5:.1f} ms", m_Recorder.frames(), fps, 1000.0 / std::max(fps, 0.001),
			1000.0 / std::max(m_Recorder.percentile_ms(0.99), 0.001), 1000.0 / std::max(m_Recorder.percentile_ms(0.999), 0.001), m_Recorder.worst_ms());
		WI_LOG(Info, "Bench", "{0} on {1}, VRAM up to {2:.0} MB, RAM up to {3:.0} MB", snapshot.Api, snapshot.Adapter, perf.vram_used_mb().max(), perf.process_ram_mb().max());

		if (!m_BenchOut.empty())
		{
			std::string error;
			if (m_Recorder.write_json(m_BenchOut, error))
			{
				WI_LOG(Info, "Bench", "frames written to {0}", m_BenchOut);
			}
			else
			{
				WI_LOG(Error, "Bench", "{0}", error);
				set_exit_code(2);
				request_quit();
				return;
			}
		}

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
	std::string m_BenchOut{};
	std::string m_BenchLabel{};
	std::string m_BenchScene{};
	std::map<std::string, std::string> m_BenchExtra{};
	wiwa::BenchRecorder m_Recorder{};
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Game(argc, argv);
}
