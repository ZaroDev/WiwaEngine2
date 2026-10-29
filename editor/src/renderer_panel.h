#pragma once

// The "Renderer" panel: what the render thread reports (frame rate, backend, adapter, shader status, pass timings, errors)
// and the renderer settings that exist so far (vertical sync). Main thread, between the ImGui frame's begin and end.
class RendererPanel
{
public:
	void draw();
};
