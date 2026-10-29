#include <editor_common.h>
#include "environment_panel.h"

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/render_module.h>

void EnvironmentPanel::draw()
{
	ImGui::SetNextWindowPos(ImVec2(984.0f, 320.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(284.0f, 340.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Environment"))
	{
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;

		const char* sources[] = { "None", "Environment asset", "Atmosphere" };
		int source = static_cast<int>(settings.Source);
		if (ImGui::Combo("Source", &source, sources, 3))
		{
			settings.Source = static_cast<wiwa::EnvironmentSource>(source);
		}

		if (settings.Source == wiwa::EnvironmentSource::File)
		{
			if (m_ShownGuid != settings.EnvironmentAsset.Guid)
			{
				m_ShownGuid = settings.EnvironmentAsset.Guid;
				m_PathStatus = settings.EnvironmentAsset.valid() ? "asset " + settings.EnvironmentAsset.Guid.to_string().substr(0, 8) : std::string();
			}

			const bool entered = ImGui::InputText("Asset", m_Path, sizeof(m_Path), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (ImGui::Button("Set") || entered)
			{
				wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
				const std::optional<wiwa::AssetRef> found = assets != nullptr ? assets->find_asset(m_Path) : std::nullopt;
				if (found)
				{
					settings.EnvironmentAsset = *found;
					m_ShownGuid = found->Guid;
					m_PathStatus = std::string("using ") + m_Path;
				}
				else
				{
					m_PathStatus = "no such asset";
				}
			}
			if (!m_PathStatus.empty())
			{
				const bool bad = m_PathStatus == "no such asset";
				ImGui::TextColored(bad ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", m_PathStatus.c_str());
			}
		}

		if (settings.Source != wiwa::EnvironmentSource::None)
		{
			ImGui::SliderFloat("Rotation", &settings.RotationDegrees, 0.0f, 360.0f, "%.0f deg");
			ImGui::DragFloat("Brightness", &settings.Brightness, 0.01f, 0.0f, 64.0f);
		}

		if (settings.Source == wiwa::EnvironmentSource::Atmosphere)
		{
			ImGui::SeparatorText("Atmosphere");
			ImGui::TextDisabled("The sun is the first directional light.");
			wiwa::AtmosphereParameters& a = settings.Atmosphere;
			ImGui::SliderFloat("Rayleigh", &a.RayleighScale, 0.0f, 4.0f);
			ImGui::SliderFloat("Mie", &a.MieScale, 0.0f, 4.0f);
			ImGui::SliderFloat("Ozone", &a.OzoneScale, 0.0f, 4.0f);
			ImGui::SliderFloat("Mie anisotropy", &a.MieAnisotropy, 0.0f, 0.99f);
			ImGui::SliderFloat("Multiple scattering", &a.MultipleScattering, 0.0f, 1.0f);
			ImGui::ColorEdit3("Ground albedo", &a.GroundAlbedo.x);
			ImGui::DragFloat("Sun illuminance", &a.SunIlluminance, 0.05f, 0.0f, 64.0f);
			ImGui::DragFloat("Sky brightness", &a.SkyBrightness, 0.05f, 0.0f, 64.0f);
			ImGui::DragFloat("Sun disc boost", &a.SunDiscBoost, 1.0f, 0.0f, 10000.0f);
		}

		wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
		if (render != nullptr && render->is_ready())
		{
			const std::string status = render->get_snapshot().EnvironmentStatus;
			const bool failed = status.rfind("failed", 0) == 0;
			ImGui::Separator();
			ImGui::TextColored(failed ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "Status: %s", status.empty() ? "none" : status.c_str());
		}

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}
