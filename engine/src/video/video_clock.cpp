#include <pch.h>
#include "video_clock.h"

namespace wiwa
{
	size_t choose_frame(std::span<const i64> timestampsUs, i64 clockUs)
	{
		size_t chosen = static_cast<size_t>(-1);
		for (size_t i = 0; i < timestampsUs.size(); i++)
		{
			if (timestampsUs[i] > clockUs)
			{
				break;
			}
			chosen = i;
		}
		return chosen;
	}

	void VideoClock::start(i64 wallUs)
	{
		m_BaseUs = 0;
		m_ResumeWallUs = wallUs;
		m_OriginFrames = m_AudioFrames.load(std::memory_order_acquire);
		m_Running = true;
	}

	void VideoClock::pause(i64 wallUs)
	{
		if (!m_Running)
		{
			return;
		}
		m_BaseUs = now_us(wallUs);
		m_OriginFrames = m_AudioFrames.load(std::memory_order_acquire);
		m_Running = false;
	}

	void VideoClock::resume(i64 wallUs)
	{
		if (m_Running)
		{
			return;
		}
		m_ResumeWallUs = wallUs;
		m_OriginFrames = m_AudioFrames.load(std::memory_order_acquire);
		m_Running = true;
	}

	void VideoClock::hold_at(i64 us)
	{
		m_BaseUs = us;
		m_OriginFrames = m_AudioFrames.load(std::memory_order_acquire);
		m_Running = false;
	}

	i64 VideoClock::now_us(i64 wallUs) const
	{
		if (m_AudioMaster)
		{
			const u32 rate = m_Rate.load(std::memory_order_relaxed);
			if (!m_Running || rate == 0)
			{
				return m_BaseUs;
			}
			const u64 played = m_AudioFrames.load(std::memory_order_acquire) - m_OriginFrames;
			return m_BaseUs + static_cast<i64>(played * 1'000'000ull / rate);
		}
		return m_Running ? m_BaseUs + (wallUs - m_ResumeWallUs) : m_BaseUs;
	}
}
