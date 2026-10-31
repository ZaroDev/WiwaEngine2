#pragma once
#include <chrono>
#include <core/log.h>

namespace wiwa 
{
	struct Clock
	{
		std::chrono::time_point<std::chrono::steady_clock> Timer;
		std::chrono::time_point<std::chrono::steady_clock> LastTime;

		std::chrono::duration<float> TimeSinceClock;
		std::chrono::duration<float> DeltaTime;

		float TimeScale;
		uint32_t FrameCount;

		bool IsPlaying;
		
		void start()
		{
			Timer = std::chrono::high_resolution_clock::now();
			LastTime = Timer; // the first delta is the time since the start, not since the clock's epoch
			FrameCount = 0;
			TimeScale = 1.0f;
			IsPlaying = true;
		}

		void update()
		{
			if (IsPlaying)
			{
				FrameCount++;
				DeltaTime = (std::chrono::high_resolution_clock::now() - LastTime) * TimeScale;
				LastTime = std::chrono::high_resolution_clock::now();
				TimeSinceClock = (std::chrono::high_resolution_clock::now() - Timer) * TimeScale;
			}
		}

		void stop()
		{
			IsPlaying = false;
			TimeScale = 0.f;
		}

		float get_time_since_startup() const { return static_cast<float>(Timer.time_since_epoch().count()); }
		std::chrono::time_point<std::chrono::steady_clock> get_last_time() const { return LastTime; }
	};

	class Time final
	{
	public:
		Time() = default;
		~Time() = default;

		void init();
		void pre_update();
		void update();
		void shutdown();
		
		void play_game();
		void stop_game();
		void pause_game();
		void unpause_game();
		void set_game_pause(bool pause = true);
		void set_game_time_scale(float value = 1.0f);

		// Milliseconds functions

		// Delta time
		float get_delta_time() const { return m_GameClock.DeltaTime.count() * 1000.0f; }
		float get_real_delta_time() const { return m_RealTimeClock.DeltaTime.count() * 1000.0f; }
		float get_engine_delta_time() const { return m_EngineTimeClock.DeltaTime.count() * 1000.0f; }

		// Seconds functions
		float get_delta_time_seconds() const { return m_GameClock.DeltaTime.count(); }
		float get_real_delta_time_seconds() const { return m_RealTimeClock.DeltaTime.count(); }
		float get_engine_delta_time_seconds() const { return m_EngineTimeClock.DeltaTime.count(); }


		const int c_UnlockedFPS = -1;
		void set_target_fps(int fps)
		{ 
			if (fps == c_UnlockedFPS)
			{
				m_TargetFPS = c_UnlockedFPS;
				m_TargetDT = 0.0f;

				return;
			}

			m_TargetFPS = fps;
			m_TargetDT = (1.0f / static_cast<float>(m_TargetFPS));
			m_TargetDT *= 1000.0f;

			WI_INFO("Time", "Set target FPS to {}", fps);
		}

		int get_target_fps() const { return m_TargetFPS; }
		float get_target_delta_time() const { return m_TargetDT; }

		const Clock& get_game_clock() const { return m_GameClock; }
		const Clock& get_real_clock() const { return m_RealTimeClock; }
		const Clock& get_engine_clock() const { return m_EngineTimeClock; }
	private:
		Clock m_GameClock;
		Clock m_RealTimeClock;
		Clock m_EngineTimeClock;

		int m_TargetFPS = c_UnlockedFPS;
		float m_TargetDT = 0.f;
	};
}