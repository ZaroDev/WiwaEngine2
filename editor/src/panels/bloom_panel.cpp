#include <editor_common.h>
#include "bloom_panel.h"

#include <imgui.h>

void BloomPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 400.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 130.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;
		wiwa::BloomSettings& bloom = settings.Bloom;

		ImGui::Checkbox("Enabled", &bloom.Enabled);
		ImGui::SliderFloat("Threshold", &bloom.Threshold, 0.0f, 4.0f);
		ImGui::SliderFloat("Knee", &bloom.Knee, 0.0f, 1.0f);
		ImGui::SliderFloat("Intensity", &bloom.Intensity, 0.0f, 0.3f, "%.3f");
		ImGui::TextDisabled("The reach of the glow is the bloom levels in the Quality panel.");

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
