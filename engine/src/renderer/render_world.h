#pragma once

#include "renderer.h"

namespace wiwa
{

	// What a RenderCommand gets to work on, on the render thread: the render-side state. Only the renderer for now; the
	// scene mirror and the GPU resource cache are reached from here as they are added.
	class RenderWorld
	{
	public:
		explicit RenderWorld(Renderer& renderer) : m_Renderer(renderer) {}

		Renderer& get_renderer() { return m_Renderer; }
		RenderScene& get_scene() { return m_Renderer.get_scene(); }
		GpuResourceCache& get_cache() { return m_Renderer.get_cache(); }
		EnvironmentCache& get_environments() { return m_Renderer.get_environments(); }

	private:
		Renderer& m_Renderer;
	};
}
