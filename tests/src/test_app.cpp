#include <editor_common.h>
#include <editor_app.h>

#include "editor/editor_smoke_driver.h"
#include "render/render_test.h"

#include "ai/ai_smoke_test.h"
#include "animation/anim_smoke_test.h"
#include "asset_pipeline/asset_smoke_test.h"
#include "audio/audio_smoke_test.h"
#include "input/input_smoke_test.h"
#include "nav/nav_smoke_test.h"
#include "physics/physics_smoke_test.h"
#include "scene/scene_smoke_test.h"
#include "scripting/script_smoke_test.h"
#include "ui/ui_smoke_test.h"

#include <renderer/render_module.h>

// wiwa_tests: the editor engine with every check the project has. Run it from editor/ (it needs the editor's assets, Library and settings files) with a
// flag from the table in CLAUDE.md; the exit code is the result (0 = passed).
//
// Headless smokes run on a thread next to the editor loop (like --pack): the editor module is up, but nothing draws them. The panel, play, demo and
// VFX smokes are driven frame by frame from inside the editor module (EditorSmokeDriver). --render-test runs on the render thread through the
// runner installed in the constructor; --render-smoke is the render module's own.
class TestApp final : public Editor
{
public:
	TestApp(int argc, char** argv) : Editor(argc, argv), m_Driver(m_Args)
	{
		wiwa::set_render_check_runner([](wiwa::RenderWorld& world) { return wiwa::run_render_checks(world); });
	}

protected:
	virtual void add_editor_module() override
	{
		// A panel smoke edits its settings file: it gets a scratch file, never the project's own.
		const std::filesystem::path temp = std::filesystem::temp_directory_path();
		const auto scratch = [&temp](const char* name)
		{
			const std::filesystem::path path = temp / name;
			std::error_code removed;
			std::filesystem::remove(path, removed);
			return path;
		};

		EditorTestSetup setup;
		if (m_Driver.physics_panel_smoke())
		{
			setup.PhysicsSettings = scratch("wiwa_physics_panel_smoke.wsettings");
		}
		if (m_Driver.input_panel_smoke())
		{
			setup.InputSettings = temp / "wiwa_input_panel_smoke.wsettings";
		}
		if (m_Driver.nav_panel_smoke())
		{
			setup.NavSettings = scratch("wiwa_nav_panel_smoke.wsettings");
			setup.NavSettingsApplyToEngine = false; // only saved: the check must not touch the running module
			setup.OpenNavPanel = true;
		}
		setup.OpenAudioPanel = m_Driver.audio_panel_smoke();
		setup.OnFrame = [this](EditorModule& editor) { m_Driver.update(editor); };
		m_ModuleManager.add_module<EditorModule>(std::move(setup));

		if (!m_Driver.any_requested() && !has_argument("--render-test") && !has_argument("--render-smoke") && !has_headless_smoke())
		{
			WI_ERROR("Tests", "wiwa_tests needs a flag (--asset-smoke, --scene-smoke, --render-test, ...): see the table in CLAUDE.md");
			set_exit_code(1);
			request_quit();
		}
	}

	virtual void on_command_argument(const std::string& argument, wiwa::AssetPipelineModule* assets) override
	{
		using Run = bool (*)(wiwa::AssetPipelineModule&);
		static const std::pair<const char*, Run> smokes[] = {
			{ "--asset-smoke", &wiwa::run_asset_smoke_test },
			{ "--scene-smoke", &wiwa::run_scene_smoke_test },
			{ "--script-smoke", &wiwa::run_script_smoke_test },
			{ "--ui-smoke", &wiwa::run_ui_smoke_test },
			{ "--physics-smoke", &wiwa::run_physics_smoke_test },
			{ "--input-smoke", &wiwa::run_input_smoke_test },
			{ "--audio-smoke", &wiwa::run_audio_smoke_test },
			{ "--nav-smoke", &wiwa::run_nav_smoke_test },
			{ "--ai-smoke", &wiwa::run_ai_smoke_test },
			{ "--anim-smoke", &wiwa::run_anim_smoke_test },
		};
		for (const auto& [flag, run] : smokes)
		{
			if (argument == flag)
			{
				m_CommandThread = std::thread([this, assets, run]() { m_CommandResult = run(*assets) ? 0 : 1; });
				return;
			}
		}
	}

private:
	bool has_argument(const char* flag) const { return std::find(m_Args.begin(), m_Args.end(), flag) != m_Args.end(); }

	bool has_headless_smoke() const
	{
		for (const char* flag : { "--asset-smoke", "--scene-smoke", "--script-smoke", "--ui-smoke", "--physics-smoke", "--input-smoke", "--audio-smoke", "--nav-smoke", "--ai-smoke", "--anim-smoke" })
		{
			if (has_argument(flag))
			{
				return true;
			}
		}
		return false;
	}

	EditorSmokeDriver m_Driver;
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new TestApp(argc, argv);
}
