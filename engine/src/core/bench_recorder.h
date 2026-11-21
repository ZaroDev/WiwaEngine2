#pragma once

#include <map>
#include <string>
#include <vector>

#include <math/math.h>

namespace wiwa
{
	class PerfStats;
	struct RenderSnapshot;

	// Records what a run of the engine did, frame by frame, for a benchmark report (docs/superpowers/documentation/benchmarking.md). The game's
	// --bench mode drives it: one sample() per frame while it measures, then write_json(). Main thread only.
	//
	// The file ("wiwa-bench/1") holds the metadata of the run, a summary and parallel arrays with one value per frame: the frame time (the main
	// thread's real delta), the render thread's, the CPU time of its frame, the physics step, memory, the renderer's counters, the time of every
	// module on the main thread and the GPU time of every render pass. scripts/bench_report.py turns it into an HTML report.
	class BenchRecorder
	{
	public:
		// What the report shows at the top: where the run came from.
		struct Meta
		{
			std::string Label{};   // a name for the run ("before the LOD change")
			std::string Scene{};
			std::string Config{};  // Debug, Release, Shipping
			double Seconds = 0.0;  // asked for
			double TargetFps = 60.0;
			bool Vsync = false;
			std::map<std::string, std::string> Extra{}; // --bench-meta key=value, anything the runner knows (scale, commit...)
		};

		void begin(const Meta& meta);
		// One frame: the main thread's frame time in milliseconds, the engine's numbers of the latest frame and the render thread's latest snapshot.
		void sample(float frameMs, const PerfStats& perf, const RenderSnapshot& snapshot);

		size_t frames() const { return m_FrameMs.size(); }
		double elapsed_seconds() const { return m_Time; }
		double average_fps() const;
		// The frame time that this fraction of the frames stay under (0.99: the 1% low).
		double percentile_ms(double fraction) const;
		double worst_ms() const;

		// The whole file. The scene numbers and the machine come from the last snapshot.
		std::string to_json() const;
		bool write_json(const std::string& path, std::string& error) const;

	private:
		// A series that starts late (a pass that first runs on frame 40) is padded with zeros up to its first value.
		static void push_named(std::map<std::string, std::vector<float>>& series, const std::string& name, float value, size_t frame);

		Meta m_Meta{};
		double m_Time = 0.0;
		std::vector<float> m_Times{};
		std::vector<float> m_FrameMs{};
		std::vector<float> m_RenderMs{};
		std::vector<float> m_RenderCpuMs{};
		std::vector<float> m_PhysicsMs{};
		std::vector<float> m_RamMb{};
		std::vector<float> m_VramMb{};
		std::vector<float> m_DrawCalls{};
		std::vector<float> m_Visible{};
		std::vector<float> m_Lod[4]{};
		std::map<std::string, std::vector<float>> m_Modules{};
		std::map<std::string, std::vector<float>> m_Passes{};

		std::string m_Api{};
		std::string m_Adapter{};
		u32 m_Instances = 0;
		u32 m_Lights = 0;
		u32 m_Textures = 0;
		u32 m_Meshes = 0;
		u64 m_CacheBytes = 0;
		u32 m_ShadowViews = 0;
	};
}
