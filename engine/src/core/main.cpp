#include <pch.h>
#include "engine.h"

#define USE_WIN_MAIN WI_SHIPPING && WI_PLATFORM_WINDOWS

int wiwa_main(int argc, char** argv)
{
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