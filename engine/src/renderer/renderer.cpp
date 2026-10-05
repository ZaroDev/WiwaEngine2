#include <pch.h>
#include "renderer.h"



#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	void Renderer::init()
	{
		

		WI_RENDER_LOG(Info, "Initializing renderer");
	}
	void Renderer::shutdown()
	{
		

		WI_RENDER_LOG(Info, "Shutdown");
	}
	void Renderer::pre_update(float deltaTime)
	{
	}
	void Renderer::update(float deltaTime)
	{
	}
	void Renderer::post_update(float deltaTime)
	{
	}
	void Renderer::pre_render_update(float deltaTime)
	{
	}
	void Renderer::render_update(float deltaTime)
	{
	}
	void Renderer::post_render_update(float deltaTime)
	{
	}
}