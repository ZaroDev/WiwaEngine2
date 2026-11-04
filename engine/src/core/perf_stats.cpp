#include <pch.h>
#include "perf_stats.h"

#include <algorithm>

#include <core/system_memory.h>

namespace wiwa
{
	namespace
	{
		constexpr double BytesPerMb = 1024.0 * 1024.0;
	}

	void Series::push(float value)
	{
		m_Values[m_Next] = value;
		m_Next = (m_Next + 1) % Capacity;
		m_Count = std::min(m_Count + 1, Capacity);
	}

	float Series::at(size_t index) const
	{
		const size_t oldest = (m_Next + Capacity - m_Count) % Capacity;
		return m_Values[(oldest + index) % Capacity];
	}

	float Series::average() const
	{
		if (m_Count == 0)
		{
			return 0.0f;
		}
		double sum = 0.0;
		for (size_t i = 0; i < m_Count; ++i)
		{
			sum += at(i);
		}
		return static_cast<float>(sum / static_cast<double>(m_Count));
	}

	float Series::max() const
	{
		float result = 0.0f;
		for (size_t i = 0; i < m_Count; ++i)
		{
			result = std::max(result, at(i));
		}
		return result;
	}

	void PerfStats::begin_frame()
	{
		std::fill(m_ModuleFrameMs.begin(), m_ModuleFrameMs.end(), 0.0f);
	}

	void PerfStats::add_module_ms(const std::string& module, float ms)
	{
		for (size_t i = 0; i < m_Modules.size(); ++i)
		{
			if (m_Modules[i].first == module)
			{
				m_ModuleFrameMs[i] += ms;
				return;
			}
		}
		m_Modules.emplace_back(module, Series{});
		m_ModuleFrameMs.push_back(ms);
	}

	void PerfStats::end_frame(float mainFrameMs)
	{
		m_MainFrame.push(mainFrameMs);
		for (size_t i = 0; i < m_Modules.size(); ++i)
		{
			m_Modules[i].second.push(m_ModuleFrameMs[i]);
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - m_LastMemorySample >= std::chrono::milliseconds(250))
		{
			m_LastMemorySample = now;
			sample_memory();
		}
	}

	void PerfStats::add_render_frame(float frameMs, float cpuMs)
	{
		m_RenderFrame.push(frameMs);
		m_RenderCpu.push(cpuMs);
	}

	void PerfStats::add_physics_step(float stepMs)
	{
		m_PhysicsStep.push(stepMs);
	}

	void PerfStats::sample_memory()
	{
		m_ProcessRam.push(static_cast<float>(static_cast<double>(process_ram_bytes()) / BytesPerMb));

		m_VramAvailable = false;
		if (!m_AdapterName.empty())
		{
			if (const std::optional<VideoMemory> video = query_video_memory(m_AdapterName))
			{
				m_VramAvailable = true;
				m_VramUsed.push(static_cast<float>(static_cast<double>(video->UsedBytes) / BytesPerMb));
				m_VramBudget.push(static_cast<float>(static_cast<double>(video->BudgetBytes) / BytesPerMb));
			}
		}
	}
}
