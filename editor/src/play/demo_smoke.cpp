#include <editor_common.h>
#include "demo_smoke.h"

#include <filesystem>
#include <fstream>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/reflection/component_registry.h>
#include <scripting/script_module.h>
#include <scripting/script_names.h>

namespace wiwa
{
	namespace
	{
		const char* const DEMO_SCENE = "assets/scenes/demo.wscene";
		const std::filesystem::path HOT_SCRIPT = "assets/scripts/zz_hot_smoke.lua";

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

		void write_hot_script(int value)
		{
			std::ofstream out(HOT_SCRIPT, std::ios::binary | std::ios::trunc);
			out << "return Script:extend{ on_update = function(self, dt) self.transform.position.x = " << value << " end }\n";
		}

		void remove_hot_script()
		{
			std::error_code error;
			std::filesystem::remove(HOT_SCRIPT, error);
			std::filesystem::remove(HOT_SCRIPT.string() + ".meta", error);
		}

		float position_x(Scene& scene, const EntityGuid& guid)
		{
			const entt::entity entity = scene.find(guid);
			return entity == entt::null ? -1.0f : scene.registry().get<Transform>(entity).Position.x;
		}
	}

	DemoSmoke::DemoSmoke(SceneEditor& editor, PlayMode& play) : m_Editor(editor), m_Play(play) {}

	DemoSmoke::~DemoSmoke()
	{
		remove_hot_script(); // also when the run ended early
	}

	// Waits are in seconds as well as frames: with vsync off a frame is a fraction of a millisecond, and the file watcher and the import need real time.
	bool DemoSmoke::wait(u32 frames, const char* what, std::string& failure)
	{
		if (m_PhaseFrames == 0)
		{
			m_PhaseStart = std::chrono::steady_clock::now();
		}
		const bool tooLong = std::chrono::steady_clock::now() - m_PhaseStart > std::chrono::seconds(30);
		if (++m_PhaseFrames > frames && tooLong)
		{
			failure = std::string("timed out waiting for ") + what;
			m_Phase = Phase::Done;
			return false;
		}
		return true;
	}

	bool DemoSmoke::update(std::string& failure)
	{
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		ScriptingModule* scripting = Engine::get().get_module<ScriptingModule>();

		if (!m_Started)
		{
			std::error_code error;
			m_LogStart = std::filesystem::exists("wiwa.log", error) ? std::filesystem::file_size("wiwa.log", error) : 0;
			m_Started = true;
		}

		switch (m_Phase)
		{
		case Phase::Open:
		{
			if (m_PhaseFrames == 0)
			{
				m_Editor.open(DEMO_SCENE);
			}
			if (!wait(1800, "the demo scene to open", failure))
			{
				return true;
			}
			if (!m_Editor.is_opening() && m_Editor.get_scene() != nullptr && find_named(*m_Editor.get_scene(), "Blue ring") != entt::null)
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
			Scene& edit = *m_Editor.get_scene();
			m_RingBefore = edit.registry().get<Transform>(find_named(edit, "Blue ring")).Rotation.x;
			m_Play.play(); // refused while a prefab expansion or a load is still going: tried again next frame
			if (m_Play.state() == PlayState::Playing)
			{
				m_Phase = Phase::Run;
				m_PhaseFrames = 0;
				m_RunFrames = 0;
			}
			return false;
		}

		case Phase::Run:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			Scene& play = *m_Play.play_scene();
			const entt::entity ring = find_named(play, "Blue ring");
			const entt::entity ball = find_named(play, "Neon cyan");
			const entt::entity camera = find_named(play, "Camera");
			const entt::entity spot = find_named(play, "Podium spot");
			if (ring == entt::null || ball == entt::null || camera == entt::null || spot == entt::null)
			{
				failure = "the demo entities are missing in the running scene";
				return true;
			}

			m_RingAfter = play.registry().get<Transform>(ring).Rotation.x;
			const float y = play.registry().get<Transform>(ball).Position.y;
			const float x = play.registry().get<Transform>(camera).Position.x;
			const float intensity = play.registry().get<Light>(spot).Intensity;
			m_BallLow = std::min(m_BallLow, y);
			m_BallHigh = std::max(m_BallHigh, y);
			m_CameraLow = std::min(m_CameraLow, x);
			m_CameraHigh = std::max(m_CameraHigh, x);
			m_LightLow = std::min(m_LightLow, intensity);
			m_LightHigh = std::max(m_LightHigh, intensity);

			if (++m_RunFrames < 150)
			{
				return false;
			}

			if (!scripting->running())
			{
				failure = "the scripts are not running";
				return true;
			}
			if (m_RingAfter == m_RingBefore)
			{
				failure = "spinner did not turn the ring";
				return true;
			}
			if (m_BallHigh - m_BallLow < 0.01f)
			{
				failure = "bobber did not move the neon ball";
				return true;
			}
			if (m_CameraHigh - m_CameraLow < 0.001f)
			{
				failure = "camera_sway did not move the camera";
				return true;
			}
			if (m_LightHigh - m_LightLow < 0.5f)
			{
				failure = "light_pulse did not change the spot light";
				return true;
			}
			m_Phase = Phase::HotCreate;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::HotCreate:
		{
			if (m_PhaseFrames == 0)
			{
				write_hot_script(111);
			}
			if (!wait(1800, "the new script to be imported", failure))
			{
				return true;
			}

			const std::optional<AssetRef> ref = assets->find_asset(HOT_SCRIPT.generic_string());
			if (!ref)
			{
				return false;
			}
			const ComponentInfo* info = ComponentRegistry::get().find(script_component_name(ref->Guid));
			if (info == nullptr)
			{
				return false;
			}

			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			Scene& play = *m_Play.play_scene();
			const entt::entity probe = play.create("hot probe");
			info->Add(play.registry(), probe);
			m_Probe = play.registry().get<Id>(probe).Guid;
			m_HotScript = *ref;
			m_Phase = Phase::HotFirst;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::HotFirst:
		{
			if (!wait(900, "the new script to run", failure))
			{
				return true;
			}
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			if (position_x(*m_Play.play_scene(), m_Probe) == 111.0f)
			{
				write_hot_script(222); // saved while the scene runs
				m_Phase = Phase::HotSecond;
				m_PhaseFrames = 0;
			}
			return false;
		}

		case Phase::HotSecond:
		{
			if (!wait(900, "the running script to pick up the new file", failure))
			{
				return true;
			}
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			if (position_x(*m_Play.play_scene(), m_Probe) == 222.0f)
			{
				failure = finish_checks();
				return true;
			}
			return false;
		}

		case Phase::Done:
			return true;
		}
		return true;
	}

	// The end of a good run: Stop, remove the files the test made, and see that nothing was logged as an error.
	std::string DemoSmoke::finish_checks()
	{
		m_Play.stop();

		remove_hot_script();

		std::ifstream log("wiwa.log");
		log.seekg(static_cast<std::streamoff>(m_LogStart));
		std::string line;
		std::string errors;
		while (std::getline(log, line))
		{
			if (line.find("[error]") != std::string::npos && line.find("zz_hot_smoke") == std::string::npos)
			{
				errors += line + "\n";
			}
		}
		return errors.empty() ? std::string() : "errors were logged:\n" + errors;
	}
}
