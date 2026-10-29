#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "timing_averager.h"

namespace wiwa
{
	// Averaged timings of one pass, in milliseconds. Empty: no sample in the last averaging window.
	struct PassTiming
	{
		const char* Name{};
		std::optional<float> GpuMs{};
		std::optional<float> CpuMs{};
	};

	// Times every pass on the GPU (timer queries) and on the CPU (command recording), and puts a debug
	// marker around it. There is one set of timer queries per frame that can still be on the GPU, plus one:
	// a set is read back right before it is reused, so reading never waits for the GPU.
	class PassProfiler
	{
	public:
		// Creates slotCount * passNames.size() timer queries. If one can't be created, only CPU timing runs.
		void initialize(nvrhi::IDevice* device, std::vector<const char*> passNames, u32 slotCount);
		bool is_initialized_for(size_t passCount) const { return m_SlotCount > 0 && m_PassNames.size() == passCount; }

		// Collects the results of the slot this frame reuses. A query that is not ready loses its sample.
		void begin_frame(nvrhi::IDevice* device, u64 frameIndex);
		void begin_pass(nvrhi::ICommandList* commandList, size_t passIndex);
		void end_pass(nvrhi::ICommandList* commandList, size_t passIndex);
		void end_frame(float time);

		const std::vector<PassTiming>& get_timings() const { return m_Timings; }
		bool are_gpu_timings_available() const { return m_GpuAvailable; }

	private:
		nvrhi::ITimerQuery* get_query(size_t passIndex) const;

	private:
		std::vector<const char*> m_PassNames{};
		u32 m_SlotCount{};
		u32 m_Slot{};
		bool m_GpuAvailable{};

		// Indexed slot * passCount + pass. Used: begun and ended in a frame whose result was not read yet.
		std::vector<nvrhi::TimerQueryHandle> m_Queries{};
		std::vector<bool> m_QueryUsed{};

		std::vector<std::chrono::steady_clock::time_point> m_CpuStart{};
		TimingAverager m_Averager{};
		std::vector<PassTiming> m_Timings{};
	};
}
