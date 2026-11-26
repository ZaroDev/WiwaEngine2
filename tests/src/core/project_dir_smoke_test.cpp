#include <editor_common.h>
#include "project_dir_smoke_test.h"

#include <core/project_dir.h>

#include <test_kit.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Tests";
			static constexpr const char* Prefix = "Project dir smoke test";
		};
		using Check = BasicCheck<CheckLog>;
	}

	bool run_project_dir_smoke_test(AssetPipelineModule&)
	{
		Check check;

		// ---- the argument
		check(!project_argument({}).has_value(), "no argument: no project");
		check(!project_argument({ "--scene", "a.wscene" }).has_value(), "other arguments: no project");
		check(project_argument({ "--project", "C:/x/y" }) == std::filesystem::path("C:/x/y"), "--project <dir>");
		check(project_argument({ "--project=D:/z" }) == std::filesystem::path("D:/z"), "--project=<dir>");
		check(project_argument({ "--project", "a", "--project=b" }) == std::filesystem::path("b"), "the last one wins");
		check(!project_argument({ "--project" }).has_value(), "--project without a value: ignored");
		check(!project_argument({ "--project=" }).has_value(), "--project= without a value: ignored");
		check(!project_argument({ "--project-dir-smoke" }).has_value(), "a flag that only starts with --project is not it");
		const std::u8string cafe = u8"D:/My Games/Caf\u00e9";
		check(project_argument({ "--project", std::string(cafe.begin(), cafe.end()) }) == std::filesystem::path(cafe), "a UTF-8 argument is read as UTF-8");

		// ---- the directory link
		std::error_code error;
		const std::filesystem::path root = std::filesystem::temp_directory_path(error) / "wiwa_project_dir_smoke";
		std::filesystem::remove_all(root, error);
		const std::filesystem::path engine = root / "repo" / "editor" / "engine";
		check(write_text(engine / "shaders" / "marker.txt", "hello"), "wrote the stand-in engine assets");

		std::string linkError;
		const std::filesystem::path link = root / "project" / "engine";
		std::filesystem::create_directories(link.parent_path(), error);
		const bool linked = link_directory(link, engine, linkError);
		check(linked, "the link is made: " + linkError);
		check(std::filesystem::is_directory(link / "shaders", error), "the link shows the engine's folders");
		check(std::filesystem::exists(link / "shaders" / "marker.txt", error), "the link shows the engine's files");
		const bool again = link_directory(link, engine, linkError);
		check(!again, "a link over an existing folder is refused");
		std::filesystem::remove(link, error); // removes the link, not the target
		check(std::filesystem::exists(engine / "shaders" / "marker.txt", error), "removing the link leaves the engine's files");

		// ---- finding the engine's assets from an executable folder
		const std::filesystem::path exe = root / "repo" / "bin" / "Release-windows-x86_64" / "wiwa_editor";
		std::filesystem::create_directories(exe, error);
		check(find_engine_assets(exe) == engine, "found from bin/<config>/<project>");
		if (!std::getenv("WIWA_ENGINE_ASSETS"))
		{
			check(find_engine_assets(root / "nowhere").empty(), "nothing is found where there is nothing");
		}

		std::filesystem::remove_all(root, error);
		if (check.Failures == 0)
		{
			WI_LOG(Info, "Tests", "Project dir smoke test passed");
		}
		return check.Failures == 0;
	}
}
