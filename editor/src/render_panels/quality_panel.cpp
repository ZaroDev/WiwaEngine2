#include <editor_common.h>
#include "quality_panel.h"

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>

void QualityPanel::draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(680.0f, 560.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 150.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Quality"))
	{
		wiwa::RenderQuality quality = render->get_quality();
		const wiwa::RenderQuality before = quality;

		ImGui::SeparatorText("Shadows");
		ImGui::DragFloat("Distance", &quality.ShadowDistance, 0.5f, 1.0f, 1000.0f);
		int cascades = static_cast<int>(quality.ShadowCascades);
		if (ImGui::SliderInt("Cascades", &cascades, 1, 4))
		{
			quality.ShadowCascades = static_cast<wiwa::u32>(cascades);
		}
		ImGui::SliderFloat("Split lambda", &quality.ShadowSplitLambda, 0.0f, 1.0f);
		ImGui::DragFloat("Depth bias", &quality.ShadowDepthBias, 0.00005f, 0.0f, 0.05f, "%.5f");
		ImGui::DragFloat("Normal bias (texels)", &quality.ShadowNormalBias, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Spot depth bias", &quality.SpotShadowBias, 0.00005f, 0.0f, 0.05f, "%.5f");

		ImGui::SeparatorText("Display");
		ImGui::Checkbox("Vertical sync", &quality.Vsync);
		ImGui::Checkbox("Upload on the copy queue", &quality.CopyQueue);

		if (!(quality == before))
		{
			render->set_quality(quality);
		}
	}
	ImGui::End();
}
