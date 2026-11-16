#include "editor_app.h"

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Editor(argc, argv);
}