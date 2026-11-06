#pragma once

#include <imgui.h>

// The Viewport (scene) and Game panels share one render: the renderer draws one view per frame. The Game panel stamps the frame it was drawn
// visible; the Viewport panel gives way while that stamp is this frame's or the last one's, so the result does not depend on which panel
// is drawn first (the switch takes a frame). Main thread.
class ViewOwner
{
public:
	void game_visible() { m_GameFrame = ImGui::GetFrameCount(); }
	bool game_owns_render() const { return m_GameFrame >= ImGui::GetFrameCount() - 1; }

private:
	int m_GameFrame = -1000;
};
