#pragma once
#include "panel.h"
// The "Quality" panel: what the project decides about how the renderer works (shadow reach and biases, vsync, uploads). The values are
// saved to render_quality.json next to the program as soon as they change. Main thread, between the ImGui frame's begin and end.
class QualityPanel : public Panel
{
public:
	QualityPanel() : Panel("Quality", PanelCategory::Renderer) {}

	virtual void on_draw() override;
};
