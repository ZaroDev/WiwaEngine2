#pragma once

#include <cstddef>
#include <math/math.h>
#include <optional>
#include <vector>

namespace wiwa
{
	// Turns per-frame timing samples into averages that change only every windowSeconds, so the numbers
	// stay readable in the UI. GPU and CPU samples are separate series: GPU samples arrive a few frames
	// late and can be dropped, CPU samples arrive every frame. No GPU code here (tested on its own).
	class TimingAverager
	{
	public:
		explicit TimingAverager(size_t seriesCount = 0, float windowSeconds = 0.5f)
			: m_WindowSeconds(windowSeconds)
		{
			reset(seriesCount);
		}

		// Drops the sums, the published values and the window start.
		void reset(size_t seriesCount)
		{
			m_Gpu.assign(seriesCount, Series{});
			m_Cpu.assign(seriesCount, Series{});
			m_WindowStart.reset();
		}

		// A GPU sample of 0 is invalid, not fast: NVRHI reports a D3D11 disjoint interval (clock change) as exactly 0.
		void add_gpu(size_t index, float milliseconds)
		{
			if (milliseconds > 0.0f)
			{
				add(m_Gpu, index, milliseconds);
			}
		}
		void add_cpu(size_t index, float milliseconds) { add(m_Cpu, index, milliseconds); }

		// The first call only starts the window. Publishes the means once the window has elapsed;
		// a series without samples in the window publishes no value.
		void update(float nowSeconds)
		{
			if (!m_WindowStart)
			{
				m_WindowStart = nowSeconds;
				return;
			}
			if (nowSeconds - *m_WindowStart < m_WindowSeconds)
			{
				return;
			}

			publish(m_Gpu);
			publish(m_Cpu);
			m_WindowStart = nowSeconds;
		}

		// The last published mean; empty if none yet or the index is out of range.
		std::optional<float> get_gpu(size_t index) const { return get(m_Gpu, index); }
		std::optional<float> get_cpu(size_t index) const { return get(m_Cpu, index); }

	private:
		struct Series
		{
			double Sum{};
			u32 Count{};
			std::optional<float> Published{};
		};

		static void add(std::vector<Series>& series, size_t index, float milliseconds)
		{
			if (index < series.size())
			{
				series[index].Sum += milliseconds;
				series[index].Count++;
			}
		}

		static void publish(std::vector<Series>& series)
		{
			for (Series& entry : series)
			{
				entry.Published = (entry.Count > 0) ? std::optional<float>(static_cast<float>(entry.Sum / entry.Count)) : std::nullopt;
				entry.Sum = 0.0;
				entry.Count = 0;
			}
		}

		static std::optional<float> get(const std::vector<Series>& series, size_t index)
		{
			return (index < series.size()) ? series[index].Published : std::nullopt;
		}

	private:
		float m_WindowSeconds{};
		std::optional<float> m_WindowStart{};
		std::vector<Series> m_Gpu{};
		std::vector<Series> m_Cpu{};
	};
}
