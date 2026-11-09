#include <editor_common.h>
#include "game_smoke.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/reflection/component_registry.h>

namespace wiwa
{
	namespace
	{
		const char* const GAME_SCENE = "assets/game/scenes/main_menu.wscene";

		entt::entity find_named(Scene& scene, const std::string& name)
		{
			for (const entt::entity entity : scene.registry().view<Name>())
			{
				if (scene.registry().get<Name>(entity).Value == name)
				{
					return entity;
				}
			}
			return entt::null;
		}
	}

	GameSmoke::GameSmoke(SceneEditor& editor, PlayMode& play) : m_Editor(editor), m_Play(play)
	{
		std::error_code error;
		m_LogStart = std::filesystem::exists("wiwa.log", error) ? std::filesystem::file_size("wiwa.log", error) : 0;
	}

	bool GameSmoke::wait(u32 frames, const char* what, std::string& failure)
	{
		if (m_PhaseFrames == 0)
		{
			m_PhaseStart = std::chrono::steady_clock::now();
		}
		// real time as well as frames: the arena scene loads in the background and the whole self test takes tens of seconds
		const bool tooLong = std::chrono::steady_clock::now() - m_PhaseStart > std::chrono::seconds(m_Phase == Phase::Wait ? 150 : 30);
		if (++m_PhaseFrames > frames && tooLong)
		{
			failure = std::string("timed out waiting for ") + what;
			m_Phase = Phase::Done;
			return false;
		}
		return true;
	}

	// The self test's line in the log: empty while there is none; decided is true once there is one.
	std::string GameSmoke::verdict(bool& decided) const
	{
		decided = false;
		std::ifstream log("wiwa.log");
		log.seekg(static_cast<std::streamoff>(m_LogStart));
		std::string line;
		while (std::getline(log, line))
		{
			if (line.find("GAMESMOKE PASS") != std::string::npos)
			{
				decided = true;
				return {};
			}
			if (const size_t at = line.find("GAMESMOKE FAIL"); at != std::string::npos)
			{
				decided = true;
				return line.substr(at);
			}
		}
		return {};
	}

	std::string GameSmoke::finish_checks()
	{
		m_Play.stop();

		std::ifstream log("wiwa.log");
		log.seekg(static_cast<std::streamoff>(m_LogStart));
		std::string line;
		std::string errors;
		while (std::getline(log, line))
		{
			if (line.find("[error]") != std::string::npos || line.find("[critical]") != std::string::npos)
			{
				errors += line + "\n";
			}
		}
		return errors.empty() ? std::string() : "errors were logged:\n" + errors;
	}

	// Whatever ends the run, play stops and the errors the scripts logged are reported with the reason.
	bool GameSmoke::update(std::string& failure)
	{
		if (!update_phase(failure))
		{
			return false;
		}
		if (!m_Finished)
		{
			m_Finished = true;
			const std::string errors = finish_checks();
			if (!errors.empty())
			{
				failure += std::string(failure.empty() ? "" : "\n") + errors;
			}
		}
		return true;
	}

	bool GameSmoke::update_phase(std::string& failure)
	{
		switch (m_Phase)
		{
		case Phase::Open:
		{
			if (m_PhaseFrames == 0)
			{
				m_Editor.open(GAME_SCENE);
			}
			if (!wait(1800, "the main menu scene to open", failure))
			{
				return true;
			}
			if (!m_Editor.is_opening() && m_Editor.get_scene() != nullptr && find_named(*m_Editor.get_scene(), "Stage Warrior") != entt::null)
			{
				m_Phase = Phase::Play;
				m_PhaseFrames = 0;
			}
			return false;
		}

		case Phase::Play:
		{
			if (!wait(600, "play to start", failure))
			{
				return true;
			}
			m_Play.play(); // refused while a prefab expansion or a load is still going: tried again next frame
			if (m_Play.state() == PlayState::Playing)
			{
				m_Phase = Phase::Signal;
				m_PhaseFrames = 0;
			}
			return false;
		}

		case Phase::Signal:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			// a few seconds for the prefab expansion, the panel and the animation graph to load
			if (!wait(240, "the level to settle", failure))
			{
				return true;
			}
			if (m_PhaseFrames < 240)
			{
				return false;
			}
			Scene& play = *m_Play.play_scene();
			const entt::entity signal = find_named(play, "SmokeSignal");
			if (signal == entt::null)
			{
				failure = "the entity SmokeSignal is missing in the running main menu scene";
				return true;
			}
			play.registry().get<Transform>(signal).Position.x = 1.0f;
			m_Phase = Phase::Wait;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::Wait:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			if (!wait(1, "the game self test", failure))
			{
				return true;
			}
			bool decided = false;
			const std::string result = verdict(decided);
			if (decided)
			{
				failure = result;
				return true;
			}
			return false;
		}

		case Phase::Done:
			return true;
		}
		return true;
	}
}
