#include <editor_common.h>
#include "post_effects_panel.h"

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>

void PostEffectsPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(376.0f, 400.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(320.0f, 200.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;

		if (ImGui::BeginTabBar("PostEffectsTabs"))
		{
			if (ImGui::BeginTabItem("Temporal AA"))
			{
				const bool taaOn = render != nullptr && render->get_quality().Taa;
				if (!taaOn)
				{
					ImGui::TextDisabled("Temporal anti-aliasing is off. Choose TAA under Anti-aliasing in the Quality panel.");
				}
				ImGui::BeginDisabled(!taaOn);
				ImGui::SliderFloat("Feedback", &settings.Taa.Feedback, 0.8f, 0.98f);
				ImGui::TextDisabled("Higher is smoother, lower reacts faster.");
				if (ImGui::Button("Reset history") && render != nullptr)
				{
					render->reset_temporal_history();
				}
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Motion blur"))
			{
				wiwa::MotionBlurSettings& blur = settings.MotionBlur;
				ImGui::Checkbox("Enabled", &blur.Enabled);
				ImGui::SliderFloat("Exposure time (s)", &blur.ExposureTime, 0.0005f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Strength", &blur.Strength, 0.0f, 4.0f);
				ImGui::SliderFloat("Max blur (pixels)", &blur.MaxBlurPixels, 1.0f, 100.0f);
				ImGui::Separator();
				if (ImGui::Checkbox("Show velocity", &m_ShowVelocity) && render != nullptr)
				{
					render->set_debug_velocity(m_ShowVelocity);
				}
				ImGui::TextDisabled("Replaces the image by the motion of every pixel (red: horizontal, green: vertical).");
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
