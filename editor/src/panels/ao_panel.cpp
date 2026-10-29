#include <editor_common.h>
#include "ao_panel.h"

#include <imgui.h>

void AoPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 400.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 120.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;
		wiwa::AoSettings& ao = settings.Ao;

		ImGui::Checkbox("Enabled", &ao.Enabled);
		ImGui::SliderFloat("Radius", &ao.Radius, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Power", &ao.Power, 0.1f, 8.0f);
		ImGui::TextDisabled("It darkens the ambient light only. The quality is in the Quality panel.");

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
