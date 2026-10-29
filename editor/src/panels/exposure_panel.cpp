#include <editor_common.h>
#include "exposure_panel.h"

#include <imgui.h>

void ExposurePanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 400.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(296.0f, 230.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;
		wiwa::ExposureSettings& exposure = settings.Exposure;

		ImGui::Checkbox("Automatic", &exposure.Auto);
		if (!exposure.Auto)
		{
			ImGui::SliderFloat("Exposure (EV)", &settings.ExposureEv, -10.0f, 10.0f);
		}
		else
		{
			ImGui::SliderFloat("Compensation (EV)", &exposure.CompensationEv, -6.0f, 6.0f);
			ImGui::SliderFloat("Key", &exposure.Key, 0.02f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
			ImGui::TextDisabled("The brightness the average of the scene is brought to.");
			ImGui::DragFloatRange2("Limits (EV)", &exposure.MinEv, &exposure.MaxEv, 0.1f, -10.0f, 10.0f);
			ImGui::SliderFloat("Adaptation speed", &exposure.AdaptSpeed, 0.0f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
			ImGui::DragFloatRange2("Measured range", &exposure.LowPercent, &exposure.HighPercent, 0.01f, 0.0f, 1.0f, "%.2f");
			ImGui::TextDisabled("The darkest and brightest pixels outside it do not count.");
		}

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
