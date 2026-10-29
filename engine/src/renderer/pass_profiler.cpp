#include <pch.h>
#include "pass_profiler.h"

namespace wiwa
{
	void PassProfiler::initialize(nvrhi::IDevice* device, std::vector<const char*> passNames, u32 slotCount)
	{
		m_PassNames = std::move(passNames);
		m_SlotCount = slotCount;
		m_Slot = 0;

		const size_t passCount = m_PassNames.size();
		m_Queries.clear();
		m_QueryUsed.assign(slotCount * passCount, false);

		m_GpuAvailable = true;
		for (size_t i = 0; i < slotCount * passCount; i++)
		{
			nvrhi::TimerQueryHandle query = device->createTimerQuery();
			if (query == nullptr)
			{
				WI_LOG(Warning, "Renderer", "Timer queries are not available, GPU timings disabled");
				m_GpuAvailable = false;
				m_Queries.clear();
				break;
			}
			m_Queries.push_back(std::move(query));
		}

		m_CpuStart.assign(passCount, {});
		m_Averager.reset(passCount);
		m_Timings.assign(passCount, PassTiming{});
		for (size_t i = 0; i < passCount; i++)
		{
			m_Timings[i].Name = m_PassNames[i];
		}
	}

	void PassProfiler::begin_frame(nvrhi::IDevice* device, u64 frameIndex)
	{
		m_Slot = static_cast<u32>(frameIndex % m_SlotCount);
		if (!m_GpuAvailable)
		{
			return;
		}

		for (size_t pass = 0; pass < m_PassNames.size(); pass++)
		{
			const size_t index = m_Slot * m_PassNames.size() + pass;
			if (!m_QueryUsed[index])
			{
				continue;
			}

			nvrhi::ITimerQuery* query = m_Queries[index];
			if (device->pollTimerQuery(query))
			{
				m_Averager.add_gpu(pass, device->getTimerQueryTime(query) * 1000.0f); // seconds -> ms
			}
			device->resetTimerQuery(query);
			m_QueryUsed[index] = false;
		}
	}

	void PassProfiler::begin_pass(nvrhi::ICommandList* commandList, size_t passIndex)
	{
		commandList->beginMarker(m_PassNames[passIndex]);
		if (nvrhi::ITimerQuery* query = get_query(passIndex))
		{
			commandList->beginTimerQuery(query);
		}
		m_CpuStart[passIndex] = std::chrono::steady_clock::now();
	}

	void PassProfiler::end_pass(nvrhi::ICommandList* commandList, size_t passIndex)
	{
		const std::chrono::duration<float, std::milli> cpuTime = std::chrono::steady_clock::now() - m_CpuStart[passIndex];
		m_Averager.add_cpu(passIndex, cpuTime.count());

		if (nvrhi::ITimerQuery* query = get_query(passIndex))
		{
			commandList->endTimerQuery(query);
			m_QueryUsed[m_Slot * m_PassNames.size() + passIndex] = true;
		}
		commandList->endMarker();
	}

	void PassProfiler::end_frame(float time)
	{
		m_Averager.update(time);
		for (size_t i = 0; i < m_Timings.size(); i++)
		{
			m_Timings[i].GpuMs = m_Averager.get_gpu(i);
			m_Timings[i].CpuMs = m_Averager.get_cpu(i);
		}
	}

	nvrhi::ITimerQuery* PassProfiler::get_query(size_t passIndex) const
	{
		return m_GpuAvailable ? m_Queries[m_Slot * m_PassNames.size() + passIndex].Get() : nullptr;
	}
}
