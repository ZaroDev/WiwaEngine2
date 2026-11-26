#include <editor_common.h>
#include "new_project_smoke.h"

#include <asset_pipeline/build_settings.h>
#include <core/engine.h>
#include <core/launch.h>
#include <core/project_creator.h>
#include <editor_module.h>
#include <project/starter_scene.h>

#include <nlohmann/json.hpp>
#include <test_kit.h>

#include <windows.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Tests";
			static constexpr const char* Prefix = "New project smoke test";
		};
		using Check = BasicCheck<CheckLog>;

		constexpr int CHILD_TIMEOUT_SECONDS = 900; // a cold project cooks every shader before its first scan finishes

		std::filesystem::path executable()
		{
			wchar_t buffer[4096];
			const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
			return length > 0 && length < std::size(buffer) ? std::filesystem::path(buffer) : std::filesystem::path{};
		}

		// Runs the child and waits for it; its exit code, or -1 when it did not start or did not end in time (then it is killed).
		int run_child(const std::filesystem::path& exe, const std::filesystem::path& project)
		{
			const std::wstring line = L"\"" + exe.wstring() + L"\" --project \"" + project.wstring() + L"\" --starter-scene-child";
			std::vector<wchar_t> buffer(line.begin(), line.end());
			buffer.push_back(L'\0');
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, project.c_str(), &startup, &process))
			{
				return -1;
			}
			CloseHandle(process.hThread);
			int result = -1;
			if (WaitForSingleObject(process.hProcess, CHILD_TIMEOUT_SECONDS * 1000) == WAIT_OBJECT_0)
			{
				DWORD code = 1;
				GetExitCodeProcess(process.hProcess, &code);
				result = static_cast<int>(code);
			}
			else
			{
				TerminateProcess(process.hProcess, 2);
			}
			CloseHandle(process.hProcess);
			return result;
		}

		// The Name component of every entity of a .wscene ({"entities": [{"components": {"Name": {"Value": ...}}}]}).
		std::vector<std::string> entity_names(const nlohmann::json& scene)
		{
			std::vector<std::string> names;
			if (scene.is_object() && scene.contains("entities") && scene["entities"].is_array())
			{
				for (const nlohmann::json& entity : scene["entities"])
				{
					const nlohmann::json name = entity.value("components", nlohmann::json::object()).value("Name", nlohmann::json::object());
					if (name.contains("Value") && name["Value"].is_string())
					{
						names.push_back(name["Value"].get<std::string>());
					}
				}
			}
			return names;
		}
	}

	bool run_new_project_smoke_test(AssetPipelineModule&)
	{
		Check check;
		std::error_code error;
		const std::filesystem::path root = std::filesystem::temp_directory_path(error) / "wiwa_new_project_smoke";
		std::filesystem::remove_all(root, error);
		const std::filesystem::path project = root / "Starter Game";

		const std::filesystem::path exe = executable();
		const std::filesystem::path templ = find_template(exe.parent_path(), "minimal");
		std::string why;
		check(!templ.empty(), "found the Minimal template");
		check(create_project(project, "Starter Game", templ, why), "created the scratch project: " + why);

		// ---- the step waits for the scan, and touches nothing before it
		EditorModule* editor = Engine::get().get_module<EditorModule>();
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		check(editor && assets, "the editor and asset modules are up");
		if (editor && assets)
		{
			const auto noImport = [](const std::filesystem::path&) {};
			check(build_starter_scene(project, false, *assets, editor->scene_editor(), noImport, why) == StarterSceneStep::NotYet, "before the scan: not yet");
			check(!std::filesystem::exists(project / STARTER_SCENE, error), "before the scan: no scene written");
			check(std::filesystem::exists(project / NEW_PROJECT_MARKER, error), "before the scan: the marker stays");
		}

		// ---- a real editor opens the project
		const int child = run_child(exe, project);
		check(child == 0, "the editor built the starter scene (child exit " + std::to_string(child) + ", log in " + (project / "wiwa.log").string() + ")");

		check(std::filesystem::is_regular_file(project / STARTER_SCENE, error), "assets/scenes/main.wscene written");
		std::ifstream in(project / STARTER_SCENE, std::ios::binary);
		const nlohmann::json scene = nlohmann::json::parse(in, nullptr, false);
		const std::vector<std::string> names = entity_names(scene);
		for (const char* name : { "Plane", "Directional light", "Camera" })
		{
			check(std::count(names.begin(), names.end(), name) == 1, std::string("the scene has one '") + name + "'");
		}

		std::string loadError;
		const std::optional<BuildSettings> build = BuildSettings::load(project / BUILD_SETTINGS_FILE, loadError);
		check(build.has_value(), "build_settings.json written: " + loadError);
		if (build)
		{
			check(build->DefaultScene != AssetGuid{}, "a default scene is set");
			check(build->Scenes.size() == 1 && build->Scenes[0] == build->DefaultScene, "the starter scene is the only scene");
		}
		check(!std::filesystem::exists(project / NEW_PROJECT_MARKER, error), "the marker is gone");

		// ---- once done, never again
		if (editor && assets)
		{
			const auto noImport = [](const std::filesystem::path&) {};
			check(build_starter_scene(project, true, *assets, editor->scene_editor(), noImport, why) == StarterSceneStep::NotNeeded, "a second time: not needed");
		}

		if (check.Failures == 0)
		{
			std::filesystem::remove_all(root, error); // kept on a failure: the child's wiwa.log says why
			WI_LOG(Info, "Tests", "New project smoke test passed");
		}
		return check.Failures == 0;
	}

	void starter_scene_child_frame(EditorModule& editor)
	{
		static const auto start = std::chrono::steady_clock::now();
		const StarterSceneStep step = editor.starter_scene_step();
		const bool late = std::chrono::steady_clock::now() - start > std::chrono::seconds(CHILD_TIMEOUT_SECONDS - 30);
		if (step == StarterSceneStep::NotYet && !late)
		{
			return;
		}
		if (step != StarterSceneStep::Done)
		{
			WI_LOG(Error, "Tests", "Starter scene child: the step ended as {0}", late ? "a timeout" : step == StarterSceneStep::Failed ? "Failed" : "NotNeeded");
		}
		Engine::get().set_exit_code(step == StarterSceneStep::Done ? 0 : 1);
		Engine::get().request_quit();
	}
}
