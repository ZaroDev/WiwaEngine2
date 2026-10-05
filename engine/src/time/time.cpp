#include <pch.h>
#include "time.h"

#include <glm/glm.hpp>

namespace wiwa
{
	void Time::init()
	{
		m_RealTimeClock.start();
		m_EngineTimeClock.start();
	}

	void Time::pre_update()
	{
		m_RealTimeClock.update();
	}

	void Time::update()
	{
		m_EngineTimeClock.update();
		m_GameClock.update();
	}

	void Time::shutdown()
	{
		
	}

	void Time::play_game()
	{
		m_GameClock.start();
	}
	
	void Time::stop_game()
	{
		m_GameClock.stop();
	}
	void Time::pause_game()
	{
		set_game_pause(true);
	}

	void Time::unpause_game()
	{
		set_game_pause(false);
	}

	void Time::set_game_pause(bool pause)
	{
		m_GameClock.IsPlaying = !pause;
	}

	void Time::set_game_time_scale(float value)
	{
		m_GameClock.TimeScale = value;
		m_GameClock.TimeScale = glm::clamp(m_GameClock.TimeScale, 0.f, 5.f);
	}
}