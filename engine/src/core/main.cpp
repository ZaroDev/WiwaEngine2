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
#include <shellapi.h>
#include <atlstr.h>

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	int argc = 0;
	LPWSTR* argvW;

	argvW = CommandLineToArgvW(GetCommandLine(), &argc);

	char** argv = new char*[254];
	for (uint32_t i = 0; i < argc; i++)
	{
		argv[i] = CW2A(argvW[i]);
	}
	return wiwa_main(argc, argv);
}
#else
int main(int argc, char** argv)
{
	return wiwa_main(argc, argv);
}
#endif