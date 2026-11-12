#pragma once

#include <atomic>
#include <span>

#include <math/math.h>

namespace wiwa
{
	// The index of the frame to show: the newest one whose timestamp is not after the clock (the ones before it are dropped by the caller),
	// or npos when the clock is before every frame. A decoder that stalls leaves the last frame up however late it is. Timestamps ascend.
	size_t choose_frame(std::span<const i64> timestampsUs, i64 clockUs);

	// The time of the picture, in microseconds. Two sources: with an audio master the time is the played audio frames (advanced by the sound
	// device thread through add_audio_frames, so the picture follows what is heard); otherwise a monotonic wall time that the caller passes in
	// (so a check can drive it) and that stands still while paused. Everything except add_audio_frames is main thread.
	class VideoClock
	{
	public:
		void set_audio_master(bool on) { m_AudioMaster = on; }
		bool audio_master() const { return m_AudioMaster; }

		void start(i64 wallUs);
		void pause(i64 wallUs);
		void resume(i64 wallUs);
		// Sets the time and stops the clock (a seek: the picture waits for the first frame); resume() lets it run from here.
		void hold_at(i64 us);

		// Audio thread: this many frames of sound reached the device.
		void add_audio_frames(u64 frames, u32 sampleRate)
		{
			m_Rate.store(sampleRate, std::memory_order_relaxed);
			m_AudioFrames.fetch_add(frames, std::memory_order_release);
		}

		i64 now_us(i64 wallUs) const;
		bool running() const { return m_Running; }

	private:
		bool m_AudioMaster = false;
		bool m_Running = false;
		i64 m_BaseUs = 0;
		i64 m_ResumeWallUs = 0;
		u64 m_OriginFrames = 0;
		std::atomic<u64> m_AudioFrames{ 0 };
		std::atomic<u32> m_Rate{ 0 };
	};
}
