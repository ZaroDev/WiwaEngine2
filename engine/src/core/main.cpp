#include <pch.h>
#include "engine.h"
#include "project_dir.h"

#ifdef WI_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

#define USE_WIN_MAIN WI_SHIPPING && WI_PLATFORM_WINDOWS

// The running executable: GetModuleFileName on Windows (argv[0] may be a bare name when started through PATH), else argv[0] made absolute.
static std::filesystem::path executable_path(const char* argv0)
{
#ifdef WI_PLATFORM_WINDOWS
	wchar_t buffer[4096];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length > 0 && length < std::size(buffer))
	{
		return std::filesystem::path(buffer);
	}
#endif
	std::error_code ec;
	return std::filesystem::absolute(argv0, ec);
}

int wiwa_main(int argc, char** argv)
{
	// --project <dir>: work in that folder (before anything reads a relative path).
	{
		std::vector<std::string> arguments;
#ifdef WI_PLATFORM_WINDOWS
		// argv of main() is in the system code page: a project folder outside it (a user name or game name with accents or Japanese) would not survive. The wide
		// command line has it whole.
		int count = 0;
		if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count))
		{
			for (int i = 1; i < count; i++)
			{
				const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
				std::string text(static_cast<size_t>(std::max(size - 1, 0)), '\0');
				WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
				arguments.push_back(std::move(text));
			}
			LocalFree(wide);
		}
#else
		for (int i = 1; i < argc; i++)
		{
			arguments.emplace_back(argv[i]);
		}
#endif
		if (const std::optional<std::filesystem::path> project = wiwa::project_argument(arguments))
		{
			const std::filesystem::path executable = executable_path(argv[0]);
			std::string error, warning;
			if (!wiwa::enter_project(*project, wiwa::find_engine_assets(executable.parent_path()), error, warning))
			{
				std::fprintf(stderr, "--project: %s\n", error.c_str());
				return 1;
			}
			if (!warning.empty())
			{
				std::fprintf(stderr, "--project: %s\n", warning.c_str());
			}
		}
	}

	wiwa::Engine* engine = wiwa::create_engine(argc, argv);

	engine->init();
	engine->update();
	engine->shutdown();

	return engine->get_exit_code();
}
#if USE_WIN_MAIN
#include <windows.h>
#include <shellapi.h>

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	int argc = 0;
	LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

	// The strings have to live as long as the program runs: argv points into them.
	std::vector<std::string> arguments(static_cast<size_t>(argc));
	std::vector<char*> argv(static_cast<size_t>(argc) + 1, nullptr);
	for (int i = 0; i < argc; i++)
	{
		const int size = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
		arguments[i].assign(static_cast<size_t>(std::max(size - 1, 0)), '\0');
		WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, arguments[i].data(), size, nullptr, nullptr);
		argv[i] = arguments[i].data();
	}
	LocalFree(argvW);

	return wiwa_main(argc, argv.data());
}
#else
int main(int argc, char** argv)
{
	return wiwa_main(argc, argv);
}
#endif