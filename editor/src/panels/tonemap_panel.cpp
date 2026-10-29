#include <editor_common.h>
#include "tonemap_panel.h"

#include <imgui.h>

void TonemapPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 400.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 210.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;
		wiwa::TonemapSettings& tonemap = settings.Tonemap;

		const char* operators[] = { "ACES", "Reinhard", "Neutral", "None (clamp)" };
		int current = static_cast<int>(tonemap.Operator);
		if (ImGui::Combo("Operator", &current, operators, 4))
		{
			tonemap.Operator = static_cast<wiwa::TonemapOperator>(current);
		}

		ImGui::SeparatorText("Grading");
		ImGui::SliderFloat("Contrast", &tonemap.Contrast, 0.2f, 2.5f);
		ImGui::SliderFloat("Saturation", &tonemap.Saturation, 0.0f, 2.5f);
		ImGui::SliderFloat("Temperature", &tonemap.Temperature, -1.0f, 1.0f);
		ImGui::SliderFloat("Tint", &tonemap.Tint, -1.0f, 1.0f);
		ImGui::SliderFloat("Vignette", &tonemap.Vignette, 0.0f, 1.0f);
		if (ImGui::Button("Reset grading"))
		{
			const wiwa::TonemapSettings neutral;
			tonemap.Contrast = neutral.Contrast;
			tonemap.Saturation = neutral.Saturation;
			tonemap.Temperature = neutral.Temperature;
			tonemap.Tint = neutral.Tint;
			tonemap.Vignette = neutral.Vignette;
		}

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
