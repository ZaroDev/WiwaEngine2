#pragma once
#include "panel.h"

#include <array>
#include <vector>

// The "Performance" panel: what each thread (main, renderer, physics) spends per frame, a frame-time graph, process memory and VRAM graphs, and the
// renderer's counters (draw calls, GPU asset cache, uploads). The numbers come from Engine::get_perf() and the render snapshot. Main thread, between
// the ImGui frame's begin and end.
class PerformancePanel : public Panel
{
public:
	PerformancePanel() : Panel("Performance", PanelCategory::Info) {}

	virtual void on_draw() override;

private:
	// The frame-time graph while paused: main, renderer, physics, copied when the pause was ticked.
	std::array<std::vector<float>, 3> m_Frozen{};
	bool m_Paused = false;
};
