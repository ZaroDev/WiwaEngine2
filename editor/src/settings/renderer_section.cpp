#include <editor_common.h>
#include "renderer_section.h"

#include <core/engine.h>
#include <renderer/render_module.h>

bool RendererSection::ready() const
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	return render != nullptr && render->is_ready();
}

wiwa::RenderQuality RendererSection::quality() const
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	return render != nullptr ? render->get_quality() : wiwa::RenderQuality{};
}

void RendererSection::set_quality(const wiwa::RenderQuality& quality)
{
	if (wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>())
	{
		render->set_quality(quality);
	}
}
