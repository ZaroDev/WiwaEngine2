#include <editor_common.h>
#include "launcher_smoke_test.h"

#include <core/project_registry.h>
#include <core/project_settings.h>

#include <test_kit.h>

#include <algorithm>
#include <cctype>

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
			const std::filesystem::path accented = make_dir(root / std::filesystem::path(u8"Café Game"));
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

			ProjectRegistry missing(root / "nowhere" / "projects.json");
			warning.clear();
			check(missing.load(warning) && warning.empty() && missing.projects().empty(), "a missing list is an empty list");
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

		std::filesystem::remove_all(root, error);
		if (check.Failures == 0)
		{
			WI_LOG(Info, "Tests", "Launcher smoke test passed");
		}
		return check.Failures == 0;
	}
}
