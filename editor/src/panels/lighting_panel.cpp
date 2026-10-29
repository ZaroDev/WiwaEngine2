#include <editor_common.h>
#include "lighting_panel.h"

#include <imgui.h>

void LightingPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 560.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 160.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;

		ImGui::DragFloat("Ambient intensity", &settings.AmbientIntensity, 0.01f, 0.0f, 16.0f);
		ImGui::ColorEdit3("Hemisphere sky", &settings.HemisphereSky.x);
		ImGui::ColorEdit3("Hemisphere ground", &settings.HemisphereGround.x);
		ImGui::TextDisabled("The hemisphere lights the scene when there is no environment.");

		const char* backgrounds[] = { "Sky", "Solid color" };
		int background = static_cast<int>(settings.Background);
		if (ImGui::Combo("Background", &background, backgrounds, 2))
		{
			settings.Background = static_cast<wiwa::BackgroundMode>(background);
		}
		ImGui::ColorEdit3("Background color", &settings.BackgroundColor.x);
		ImGui::TextDisabled("The exposure is in the Exposure panel.");

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
