#pragma once
#include <core/engine.h>

#include "editor_module.h"

class Editor final : public wiwa::Engine
{
public:
	Editor(int argc, char** argv) : wiwa::Engine(argc, argv)
	{
		
	}

	

	virtual ~Editor() override
	{

	}

protected:
	virtual void on_init() override
	{
		m_ModuleManager.add_module<EditorModule>();
	}
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Editor(argc, argv);
}