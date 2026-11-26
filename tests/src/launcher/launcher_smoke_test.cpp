#include <editor_common.h>
#include "launcher_smoke_test.h"

#include <assets/asset_catalog.h>
#include <core/launch.h>
#include <core/project_creator.h>
#include <core/project_registry.h>
#include <core/project_settings.h>

#include <test_kit.h>

#include <algorithm>
#include <cctype>

#include <windows.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Tests";
			static constexpr const char* Prefix = "Launcher smoke test";
		};
		using Check = BasicCheck<CheckLog>;

		std::filesystem::path make_dir(const std::filesystem::path& dir)
		{
			std::error_code error;
			std::filesystem::create_directories(dir, error);
			return dir;
		}

		// The same folder written another way: lower case, forward slashes, a trailing separator.
		std::filesystem::path respelled(const std::filesystem::path& dir)
		{
			std::string text = dir.string();
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::replace(text.begin(), text.end(), '\\', '/');
			return std::filesystem::path(text + "/");
		}

		// The path of the n-th listed project; empty when the list is shorter (a failed add must not crash the checks after it).
		std::filesystem::path listed(const ProjectRegistry& registry, size_t n)
		{
			return n < registry.projects().size() ? registry.projects()[n].Path : std::filesystem::path{};
		}

		void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

		void registry_checks(Check& check, const std::filesystem::path& root)
		{
			std::error_code error;

			// ---- what a project folder is
			const std::filesystem::path withProject = make_dir(root / "WithProject");
			ProjectSettings settings;
			settings.Name = "Named Game";
			check(save_project_settings(withProject / PROJECT_SETTINGS_FILE, settings), "wrote a project.wproject");
			const std::filesystem::path withAssets = make_dir(root / "WithAssets");
			make_dir(withAssets / "assets");
			const std::filesystem::path empty = make_dir(root / "Empty");

			check(is_project_folder(withProject), "project.wproject alone makes a project");
			check(is_project_folder(withAssets), "assets/ alone makes a project");
			check(!is_project_folder(empty), "an empty folder is not a project");
			check(!is_project_folder(root / "Missing"), "a missing folder is not a project");

			check(project_display_name(withProject) == "Named Game", "the name comes from project.wproject");
			check(project_display_name(withAssets) == "WithAssets", "without project.wproject the name is the folder's");

			// ---- adding
			const std::filesystem::path file = root / "AppData" / "Wiwa" / "projects.json";
			ProjectRegistry registry(file);
			std::string message;
			check(registry.add(withProject, message) == AddResult::Added, "a project is added");
			check(registry.add(withProject, message) == AddResult::AlreadyListed, "the same project again is already listed");
			check(registry.add(respelled(withProject), message) == AddResult::AlreadyListed, "same project, different spelling");
			check(registry.projects().size() == 1, "a project is listed once");
			check(registry.index_of(respelled(withProject)) == std::optional<size_t>(0), "index_of finds another spelling");

			check(registry.add(empty, message) == AddResult::NotAProject, "an empty folder is refused");
			check(message == "Not a Wiwa project: no project.wproject or assets/ folder in " + empty.string(), "the refusal message: " + message);
			check(registry.projects().size() == 1, "a refused folder is not listed");
			check(!registry.projects().empty() && registry.projects()[0].Name == "Named Game", "the listed name");
			check(!registry.projects().empty() && !registry.projects()[0].LastOpened.has_value(), "an added project was never opened");

			// ---- ordering: newest first, a project never opened by its Added time
			pause();
			check(registry.add(withAssets, message) == AddResult::Added, "a second project is added");
			check(listed(registry, 0) == std::filesystem::weakly_canonical(withAssets, error), "the newest addition is first");
			pause();
			registry.mark_opened(withProject);
			check(listed(registry, 0) == std::filesystem::weakly_canonical(withProject, error), "an opened project moves to the top");
			check(!registry.projects().empty() && registry.projects()[0].LastOpened.has_value(), "mark_opened sets LastOpened");

			// ---- remove only edits the list
			registry.remove(withAssets);
			check(registry.projects().size() == 1, "remove takes it off the list");
			check(std::filesystem::is_directory(withAssets / "assets", error), "remove leaves the folder alone");
			check(registry.add(withAssets, message) == AddResult::Added, "a removed project can be added back");

			// ---- relocate
			const std::filesystem::path moved = make_dir(root / "Moved");
			make_dir(moved / "assets");
			registry.relocate(withAssets, moved);
			check(!registry.index_of(withAssets).has_value() && registry.index_of(moved).has_value(), "relocate changes the path");

			// ---- round trip, with a non-ASCII folder
			const std::filesystem::path accented = make_dir(root / std::filesystem::path(u8"Caf\u00e9 Game"));
			make_dir(accented / "assets");
			pause();
			check(registry.add(accented, message) == AddResult::Added, "a non-ASCII folder is added");
			check(registry.save(), "saved");
			ProjectRegistry loaded(file);
			std::string warning;
			check(loaded.load(warning) && warning.empty(), "loaded back: " + warning);
			check(loaded.projects().size() == registry.projects().size(), "the same number of projects");
			for (size_t i = 0; i < std::min(loaded.projects().size(), registry.projects().size()); i++)
			{
				const RecentProject& a = registry.projects()[i];
				const RecentProject& b = loaded.projects()[i];
				check(a.Path == b.Path, "path " + std::to_string(i) + " round trips");
				check(a.Name == b.Name, "name " + std::to_string(i) + " round trips");
				check(a.LastOpened.has_value() == b.LastOpened.has_value(), "LastOpened presence " + std::to_string(i) + " round trips");
				const auto seconds = [](std::chrono::system_clock::time_point t) { return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count(); };
				check(seconds(a.Added) == seconds(b.Added), "Added " + std::to_string(i) + " round trips to the second");
			}
			check(loaded.index_of(accented).has_value(), "the non-ASCII path round trips");

			// ---- bad and foreign files
			check(write_text(file, "{not json"), "wrote a corrupt list");
			ProjectRegistry corrupt(file);
			check(!corrupt.load(warning) && !warning.empty(), "a corrupt list is reported");
			check(corrupt.projects().empty(), "a corrupt list gives an empty list");

			const std::string withUnknown = R"({"future":1,"projects":[{"path":")" + withProject.generic_string() + R"(","name":"Named Game","added":"2026-01-02T03:04:05Z","colour":"red"}]})";
			check(write_text(file, withUnknown), "wrote a list with unknown keys");
			ProjectRegistry foreign(file);
			warning.clear();
			check(foreign.load(warning) && warning.empty(), "unknown keys are ignored: " + warning);
			check(foreign.projects().size() == 1 && foreign.index_of(withProject).has_value(), "the entry with unknown keys is read");

			// a known key with the wrong type: that entry falls back, the launcher does not throw
			const std::string mistyped = R"({"projects":[{"path":")" + withProject.generic_string() + R"(","name":null,"added":0,"lastOpened":5}]})";
			check(write_text(file, mistyped), "wrote a list with mistyped keys");
			ProjectRegistry typed(file);
			warning.clear();
			bool threw = false;
			try { typed.load(warning); } catch (...) { threw = true; }
			check(!threw, "mistyped keys do not throw");
			check(typed.projects().size() == 1 && !typed.projects()[0].Name.empty(), "a mistyped entry is still listed, with a name");

			ProjectRegistry missing(root / "nowhere" / "projects.json");
			warning.clear();
			check(missing.load(warning) && warning.empty() && missing.projects().empty(), "a missing list is an empty list");
		}
	}

	namespace
	{
		std::filesystem::path executable_folder()
		{
			wchar_t buffer[4096];
			const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
			return length > 0 && length < std::size(buffer) ? std::filesystem::path(buffer).parent_path() : std::filesystem::path{};
		}

		size_t count_entries(const std::filesystem::path& dir)
		{
			std::error_code error;
			size_t count = 0;
			for (auto it = std::filesystem::recursive_directory_iterator(dir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				count++;
			}
			return count;
		}

		void creator_checks(Check& check, const std::filesystem::path& root)
		{
			std::error_code error;
			std::string why;

			// ---- names
			for (const char* bad : { "", "  ", "a/b", "a\\b", "what?", "CON", "com1", "game.", "game " })
			{
				check(!valid_project_name(bad, why) && !why.empty(), std::string("refused name '") + bad + "'");
			}
			check(valid_project_name("My Game", why), "accepted 'My Game': " + why);
			const std::u8string cafe = u8"Caf\u00e9";
			check(valid_project_name(std::string(cafe.begin(), cafe.end()), why), "accepted a non-ASCII name: " + why);

			// ---- a stand-in template
			const std::filesystem::path templ = root / "Template";
			check(write_text(templ / "assets" / "models" / "shapes" / "shapes.glb", "glb"), "wrote the stand-in template");

			// ---- into a new folder
			const std::filesystem::path fresh = root / "Projects" / "Fresh Game";
			check(create_project(fresh, "Fresh Game", templ, why), "created a project in a new folder: " + why);
			std::string loadError;
			const std::optional<ProjectSettings> settings = load_project_settings(fresh / PROJECT_SETTINGS_FILE, loadError);
			check(settings && settings->Name == "Fresh Game", "project.wproject reads back with the name");
			check(std::filesystem::is_regular_file(fresh / "assets" / "models" / "shapes" / "shapes.glb", error), "the template was copied");
			check(std::filesystem::is_regular_file(fresh / ".gitignore", error), ".gitignore written");
			check(std::filesystem::is_regular_file(fresh / ".gitattributes", error), ".gitattributes written");
			check(std::filesystem::is_regular_file(fresh / NEW_PROJECT_MARKER, error), "the new-project marker written");
			check(is_project_folder(fresh), "the new folder is a project");
			ProjectRegistry registry(root / "created.json");
			std::string message;
			check(registry.add(fresh, message) == AddResult::Added, "the new project can be listed");

			// ---- into an existing empty folder
			const std::filesystem::path emptyDir = make_dir(root / "Projects" / "Empty Existing");
			check(create_project(emptyDir, "Empty Existing", templ, why), "created a project in an existing empty folder: " + why);

			// ---- never into a folder with something in it
			const std::filesystem::path busy = root / "Projects" / "Busy";
			check(write_text(busy / "keep.txt", "mine"), "wrote keep.txt");
			why.clear();
			check(!create_project(busy, "Busy", templ, why) && !why.empty(), "a non-empty folder is refused: " + why);
			check(count_entries(busy) == 1, "nothing was added to the non-empty folder");
			std::ifstream keep(busy / "keep.txt");
			std::string kept((std::istreambuf_iterator<char>(keep)), std::istreambuf_iterator<char>());
			check(kept == "mine", "keep.txt is unchanged");

			// ---- an error about a folder whose name is outside the system code page: a message in UTF-8, not an exception
			const std::filesystem::path japanese = root / "Projects" / std::filesystem::path(u8"\u30b2\u30fc\u30e0");
			check(write_text(japanese / "keep.txt", "mine"), "wrote a non-empty folder with a Japanese name");
			bool threw = false;
			why.clear();
			try { threw = create_project(japanese, "Game", templ, why); } catch (...) { threw = true; }
			check(!threw, "refusing a folder with a Japanese name does not throw");
			const std::u8string name = u8"\u30b2\u30fc\u30e0";
			check(why.find(std::string(name.begin(), name.end())) != std::string::npos, "the refusal names the folder in UTF-8: " + why);

			// ---- the real template, found from this executable
			const std::filesystem::path minimal = find_template(executable_folder(), "minimal");
			check(std::filesystem::is_regular_file(minimal / "assets" / "models" / "shapes" / "shapes.glb", error), "found launcher/templates/minimal with its shapes.glb: " + minimal.string());
			check(find_template(root / "nowhere", "minimal").empty() || std::getenv("WIWA_LAUNCHER_TEMPLATES"), "no template where there is none");
			check(!default_projects_folder().empty() && default_projects_folder().filename() == "Wiwa Projects", "the default folder is Documents\\Wiwa Projects");
		}
	}

	namespace
	{
		void launch_checks(Check& check, const std::filesystem::path& root)
		{
			const std::filesystem::path bin = root / "bin" / "Release-windows-x86_64";
			const std::filesystem::path launcher = bin / "wiwa_launcher";
			check(write_text(launcher / "wiwa_launcher.exe", ""), "wrote a stand-in launcher");
			check(find_sibling_executable(launcher, "wiwa_editor").empty(), "no editor anywhere: nothing found");
			check(write_text(launcher / "wiwa_editor.exe", ""), "wrote a stand-in editor next to the launcher");
			check(find_sibling_executable(launcher, "wiwa_editor") == launcher / "wiwa_editor.exe", "the editor next to the launcher is found");
			check(write_text(bin / "wiwa_editor" / "wiwa_editor.exe", ""), "wrote a stand-in editor in its build folder");
			check(find_sibling_executable(launcher, "wiwa_editor") == launcher / ".." / "wiwa_editor" / "wiwa_editor.exe"
				|| find_sibling_executable(launcher, "wiwa_editor") == bin / "wiwa_editor" / "wiwa_editor.exe", "the build folder wins over the copy");

			// the copies the builds make in the repository: launcher/wiwa_launcher.exe and editor/wiwa_editor.exe find each other
			const std::filesystem::path repo = root / "repo";
			check(write_text(repo / "launcher" / "wiwa_launcher.exe", "") && write_text(repo / "editor" / "wiwa_editor.exe", ""), "wrote the repository copies");
			check(find_sibling_executable(repo / "launcher", "wiwa_editor") == (repo / "editor" / "wiwa_editor.exe").lexically_normal(), "the launcher's repository copy finds the editor's");
			check(find_sibling_executable(repo / "editor", "wiwa_launcher") == (repo / "launcher" / "wiwa_launcher.exe").lexically_normal(), "the editor's repository copy finds the launcher's");

			const std::filesystem::path exe = LR"(C:\Engine\wiwa_editor.exe)";
			const std::filesystem::path project = std::filesystem::path(u8R"(D:\My Games\Caf)" u8"\u00e9");
			const std::wstring expected = LR"("C:\Engine\wiwa_editor.exe" --project "D:\My Games\Caf)" L"\u00e9" LR"(")";
			check(editor_command_line(exe, project) == expected, "the editor's command line quotes both paths");
		}
	}

	bool run_launcher_smoke_test(AssetPipelineModule&)
	{
		Check check;

		std::error_code error;
		const std::filesystem::path root = std::filesystem::temp_directory_path(error) / "wiwa_launcher_smoke";
		std::filesystem::remove_all(root, error);
		std::filesystem::create_directories(root, error);

		registry_checks(check, root);
		creator_checks(check, root);
		launch_checks(check, root);

		// ---- a new project's assets are catalogued under its folder name, assets/; the editor looks them up as "Assets/..." (entity templates)
		AssetCatalog catalog;
		catalog.Entries.push_back({ "assets/models/shapes/shapes.glb", AssetGuid{ 1, 2 } });
		check(catalog.find("Assets/models/shapes/shapes.glb").has_value(), "the catalog finds a path whatever its case (Windows paths)");
		check(!catalog.find("Assets/models/shapes/other.glb").has_value(), "the catalog still tells paths apart");

		std::filesystem::remove_all(root, error);
		if (check.Failures == 0)
		{
			WI_LOG(Info, "Tests", "Launcher smoke test passed");
		}
		return check.Failures == 0;
	}
}
