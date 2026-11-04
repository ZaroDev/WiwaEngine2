#pragma once

#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <core/common.h>

namespace wiwa
{
	// A fixed ring of the latest samples of one metric. Index 0 of at() is the oldest sample.
	class Series
	{
	public:
		static constexpr size_t Capacity = 600;

		void push(float value);
		size_t count() const { return m_Count; }
		float at(size_t index) const;
		float current() const { return m_Count > 0 ? at(m_Count - 1) : 0.0f; }
		float average() const;
		float max() const;

	private:
		std::array<float, Capacity> m_Values{};
		size_t m_Next = 0;
		size_t m_Count = 0;
	};

	// What the Performance panel shows: per-thread timings, memory and VRAM, each as a Series. Main thread only; the other threads hand
	// their numbers over through the modules (RenderModule, PhysicsModule), which call the add_* functions from the main thread.
	class PerfStats
	{
	public:
		void begin_frame();
		// Adds to the module's time in the frame being built.
		void add_module_ms(const std::string& module, float ms);
		// Pushes the frame's totals, and samples the memory when a quarter of a second passed since the last sample.
		void end_frame(float mainFrameMs);

		void add_render_frame(float frameMs, float cpuMs);
		void add_physics_step(float stepMs);
		void set_physics_steps_last_frame(u32 steps) { m_PhysicsSteps = steps; }
		void set_adapter_name(const std::string& name) { m_AdapterName = name; }

		const Series& main_frame() const { return m_MainFrame; }
		const Series& render_frame() const { return m_RenderFrame; }
		const Series& render_cpu() const { return m_RenderCpu; }
		const Series& physics_step() const { return m_PhysicsStep; }
		const Series& process_ram_mb() const { return m_ProcessRam; }
		const Series& vram_used_mb() const { return m_VramUsed; }
		const Series& vram_budget_mb() const { return m_VramBudget; }
		// In the order they first ran.
		const std::vector<std::pair<std::string, Series>>& modules() const { return m_Modules; }
		u32 physics_steps_last_frame() const { return m_PhysicsSteps; }
		bool vram_available() const { return m_VramAvailable; }

	private:
		void sample_memory();

		Series m_MainFrame{};
		Series m_RenderFrame{};
		Series m_RenderCpu{};
		Series m_PhysicsStep{};
		Series m_ProcessRam{};
		Series m_VramUsed{};
		Series m_VramBudget{};
		std::vector<std::pair<std::string, Series>> m_Modules{};
		std::vector<float> m_ModuleFrameMs{}; // parallel to m_Modules: the time in the frame being built
		u32 m_PhysicsSteps = 0;
		bool m_VramAvailable = false;
		std::string m_AdapterName{};
		std::chrono::steady_clock::time_point m_LastMemorySample{};
	};
}
