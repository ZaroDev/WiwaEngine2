#pragma once

#include <chrono>
#include <string>

#include <core/common.h>
#include <scene/scene.h>
#include <scene/scene_editor.h>

#include "play_mode.h"

namespace wiwa
{
	// wiwa_editor --game-smoke: opens the demo game's main menu scene, plays it and raises the signal of its own self test (assets/game/scripts/menu_selftest.lua:
	// the x of the entity "SmokeSignal"), which checks the menu, chooses a hero and loads the level with the flag prefs "smoke" that starts selftest.lua. The script walks the whole game (movement, both attacks, death and respawn, pause, the bridge, the
	// gate with its additive scene load, the boss phases and the win, restart) and logs GAMESMOKE PASS or GAMESMOKE FAIL; this class waits for that
	// line in wiwa.log and fails on any logged error.
	class GameSmoke
	{
	public:
		GameSmoke(SceneEditor& editor, PlayMode& play);

		// Once per frame (the editor's update). True when it is over; failure is empty if everything held.
		bool update(std::string& failure);

	private:
		bool update_phase(std::string& failure);
		enum class Phase
		{
			Open,
			Play,
			Signal,
			Wait,
			Done,
		};

		bool wait(u32 frames, const char* what, std::string& failure);
		std::string verdict(bool& decided) const;
		std::string finish_checks();

		SceneEditor& m_Editor;
		PlayMode& m_Play;
		Phase m_Phase = Phase::Open;
		u32 m_PhaseFrames = 0;
		std::chrono::steady_clock::time_point m_PhaseStart{};
		bool m_Finished = false;
		std::uintmax_t m_LogStart = 0;
	};
}
