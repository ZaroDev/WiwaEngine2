#include <editor_common.h>
#include "world_panel.h"

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/render_module.h>

namespace
{
	std::string lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}
}

bool WorldPanel::matches(const char* text) const
{
	return m_Needle.empty() || lower(text).find(m_Needle) != std::string::npos;
}

void WorldPanel::group(const char* name)
{
	m_Group = name;
	m_GroupPending = !m_Collecting && !m_Group.empty();
}

void WorldPanel::note(const char* text)
{
	if (!m_Collecting && m_LastShown)
	{
		ImGui::TextDisabled("%s", text);
	}
}

void WorldPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(984.0f, 60.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(340.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open) && m_Editor.get_scene() != nullptr)
	{
		ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputTextWithHint("##world_search", "Search settings", m_Search, sizeof(m_Search));
		ImGui::SameLine();
		if (ImGui::Button("x##world_clear", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
		{
			m_Search[0] = '\0';
		}
		m_Needle = lower(m_Search);
		m_Filtering = !m_Needle.empty();
		m_AnySection = false;
		ImGui::Separator();

		wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
		const wiwa::SceneSettings before = m_Editor.settings();
		wiwa::SceneSettings settings = before;

		ImGui::BeginChild("##world_sections");
		section("Environment", true, [&]() { environment_rows(settings); });
		section("Lighting", true, [&]() { lighting_rows(settings); });
		section("Exposure", false, [&]() { exposure_rows(settings); });
		section("Tonemap", false, [&]() { tonemap_rows(settings); });
		section("Bloom", false, [&]() { bloom_rows(settings); });
		section("Ambient occlusion", false, [&]() { ao_rows(settings); });
		section("Temporal AA", false, [&]() { taa_rows(settings, render); });
		section("Motion blur", false, [&]() { motion_blur_rows(settings, render); });
		section("Scripts", false, [&]() { scripts_rows(settings); });
			section("Quality (global, not saved with the scene)", false, [&]() { quality_rows(render); });
		if (m_Filtering && !m_AnySection)
		{
			ImGui::TextDisabled("No settings match");
		}
		ImGui::EndChild();

		if (!(settings == before))
		{
			m_Editor.edit_settings(settings);
		}
	}
	ImGui::End();
}

void WorldPanel::environment_rows(wiwa::SceneSettings& settings)
{
	field("Source", [&]()
		{
			const char* sources[] = { "None", "Environment asset", "Atmosphere" };
			int source = static_cast<int>(settings.Source);
			if (ImGui::Combo("Source", &source, sources, 3))
			{
				settings.Source = static_cast<wiwa::EnvironmentSource>(source);
			}
		});

	if (settings.Source == wiwa::EnvironmentSource::File)
	{
		field("Asset", [&]()
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
			});
	}

	if (settings.Source != wiwa::EnvironmentSource::None)
	{
		field("Rotation", [&]() { ImGui::SliderFloat("Rotation", &settings.RotationDegrees, 0.0f, 360.0f, "%.0f deg"); });
		field("Brightness", [&]() { ImGui::DragFloat("Brightness", &settings.Brightness, 0.01f, 0.0f, 64.0f); });
	}

	if (settings.Source == wiwa::EnvironmentSource::Atmosphere)
	{
		group("Atmosphere");
		wiwa::AtmosphereParameters& a = settings.Atmosphere;
		field("Sun source", [&]() { ImGui::TextDisabled("The sun is the first directional light."); });
		field("Rayleigh", [&]() { ImGui::SliderFloat("Rayleigh", &a.RayleighScale, 0.0f, 4.0f); });
		field("Mie", [&]() { ImGui::SliderFloat("Mie", &a.MieScale, 0.0f, 4.0f); });
		field("Ozone", [&]() { ImGui::SliderFloat("Ozone", &a.OzoneScale, 0.0f, 4.0f); });
		field("Mie anisotropy", [&]() { ImGui::SliderFloat("Mie anisotropy", &a.MieAnisotropy, 0.0f, 0.99f); });
		field("Multiple scattering", [&]() { ImGui::SliderFloat("Multiple scattering", &a.MultipleScattering, 0.0f, 1.0f); });
		field("Ground albedo", [&]() { ImGui::ColorEdit3("Ground albedo", &a.GroundAlbedo.x); });
		field("Sun illuminance", [&]() { ImGui::DragFloat("Sun illuminance", &a.SunIlluminance, 0.05f, 0.0f, 64.0f); });
		field("Sky brightness", [&]() { ImGui::DragFloat("Sky brightness", &a.SkyBrightness, 0.05f, 0.0f, 64.0f); });
		field("Sun disc boost", [&]() { ImGui::DragFloat("Sun disc boost", &a.SunDiscBoost, 1.0f, 0.0f, 10000.0f); });
	}

	group("");
	field("Status", [&]()
		{
			wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
			if (render != nullptr && render->is_ready())
			{
				const std::string status = render->get_snapshot().EnvironmentStatus;
				const bool failed = status.rfind("failed", 0) == 0;
				ImGui::TextColored(failed ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "Status: %s", status.empty() ? "none" : status.c_str());
			}
		});
}

void WorldPanel::lighting_rows(wiwa::SceneSettings& settings)
{
	field("Ambient intensity", [&]() { ImGui::DragFloat("Ambient intensity", &settings.AmbientIntensity, 0.01f, 0.0f, 16.0f); });
	field("Hemisphere sky", [&]() { ImGui::ColorEdit3("Hemisphere sky", &settings.HemisphereSky.x); });
	field("Hemisphere ground", [&]() { ImGui::ColorEdit3("Hemisphere ground", &settings.HemisphereGround.x); });
	note("The hemisphere lights the scene when there is no environment.");

	field("Background", [&]()
		{
			const char* backgrounds[] = { "Sky", "Solid color" };
			int background = static_cast<int>(settings.Background);
			if (ImGui::Combo("Background", &background, backgrounds, 2))
			{
				settings.Background = static_cast<wiwa::BackgroundMode>(background);
			}
		});
	field("Background color", [&]() { ImGui::ColorEdit3("Background color", &settings.BackgroundColor.x); });
}

void WorldPanel::exposure_rows(wiwa::SceneSettings& settings)
{
	wiwa::ExposureSettings& exposure = settings.Exposure;

	field("Automatic", [&]() { ImGui::Checkbox("Automatic", &exposure.Auto); });
	if (!exposure.Auto)
	{
		field("Exposure (EV)", [&]() { ImGui::SliderFloat("Exposure (EV)", &settings.ExposureEv, -10.0f, 10.0f); });
		return;
	}

	field("Compensation (EV)", [&]() { ImGui::SliderFloat("Compensation (EV)", &exposure.CompensationEv, -6.0f, 6.0f); });
	field("Key", [&]() { ImGui::SliderFloat("Key", &exposure.Key, 0.02f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic); });
	note("The brightness the average of the scene is brought to.");
	field("Limits (EV)", [&]() { ImGui::DragFloatRange2("Limits (EV)", &exposure.MinEv, &exposure.MaxEv, 0.1f, -10.0f, 10.0f); });
	field("Adaptation speed", [&]() { ImGui::SliderFloat("Adaptation speed", &exposure.AdaptSpeed, 0.0f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic); });
	field("Measured range", [&]() { ImGui::DragFloatRange2("Measured range", &exposure.LowPercent, &exposure.HighPercent, 0.01f, 0.0f, 1.0f, "%.2f"); });
	note("The darkest and brightest pixels outside it do not count.");
}

void WorldPanel::tonemap_rows(wiwa::SceneSettings& settings)
{
	wiwa::TonemapSettings& tonemap = settings.Tonemap;

	field("Operator", [&]()
		{
			const char* operators[] = { "ACES", "Reinhard", "Neutral", "None (clamp)" };
			int current = static_cast<int>(tonemap.Operator);
			if (ImGui::Combo("Operator", &current, operators, 4))
			{
				tonemap.Operator = static_cast<wiwa::TonemapOperator>(current);
			}
		});

	group("Grading");
	field("Contrast", [&]() { ImGui::SliderFloat("Contrast", &tonemap.Contrast, 0.2f, 2.5f); });
	field("Saturation", [&]() { ImGui::SliderFloat("Saturation", &tonemap.Saturation, 0.0f, 2.5f); });
	field("Temperature", [&]() { ImGui::SliderFloat("Temperature", &tonemap.Temperature, -1.0f, 1.0f); });
	field("Tint", [&]() { ImGui::SliderFloat("Tint", &tonemap.Tint, -1.0f, 1.0f); });
	field("Vignette", [&]() { ImGui::SliderFloat("Vignette", &tonemap.Vignette, 0.0f, 1.0f); });
	field("Reset grading", [&]()
		{
			if (ImGui::Button("Reset grading"))
			{
				const wiwa::TonemapSettings neutral;
				tonemap.Contrast = neutral.Contrast;
				tonemap.Saturation = neutral.Saturation;
				tonemap.Temperature = neutral.Temperature;
				tonemap.Tint = neutral.Tint;
				tonemap.Vignette = neutral.Vignette;
			}
		});
}

void WorldPanel::bloom_rows(wiwa::SceneSettings& settings)
{
	wiwa::BloomSettings& bloom = settings.Bloom;

	field("Enabled", [&]() { ImGui::Checkbox("Enabled", &bloom.Enabled); });
	field("Threshold", [&]() { ImGui::SliderFloat("Threshold", &bloom.Threshold, 0.0f, 4.0f); });
	field("Knee", [&]() { ImGui::SliderFloat("Knee", &bloom.Knee, 0.0f, 1.0f); });
	field("Intensity", [&]() { ImGui::SliderFloat("Intensity", &bloom.Intensity, 0.0f, 0.3f, "%.3f"); });
	note("The reach of the glow is the bloom levels in the Quality section.");
}

void WorldPanel::ao_rows(wiwa::SceneSettings& settings)
{
	wiwa::AoSettings& ao = settings.Ao;

	field("Enabled", [&]() { ImGui::Checkbox("Enabled", &ao.Enabled); });
	field("Radius", [&]() { ImGui::SliderFloat("Radius", &ao.Radius, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic); });
	field("Power", [&]() { ImGui::SliderFloat("Power", &ao.Power, 0.1f, 8.0f); });
	note("It darkens the ambient light only. The quality is in the Quality section.");
}

void WorldPanel::taa_rows(wiwa::SceneSettings& settings, wiwa::RenderModule* render)
{
	const bool taaOn = render != nullptr && render->get_quality().Taa;
	field("Anti-aliasing mode", [&]()
		{
			if (!taaOn)
			{
				ImGui::TextDisabled("Temporal anti-aliasing is off. Choose TAA under Anti-aliasing in the Quality section.");
			}
		});
	field("Feedback", [&]()
		{
			ImGui::BeginDisabled(!taaOn);
			ImGui::SliderFloat("Feedback", &settings.Taa.Feedback, 0.8f, 0.98f);
			ImGui::EndDisabled();
		});
	note("Higher is smoother, lower reacts faster.");
	field("Reset history", [&]()
		{
			ImGui::BeginDisabled(!taaOn);
			if (ImGui::Button("Reset history") && render != nullptr)
			{
				render->reset_temporal_history();
			}
			ImGui::EndDisabled();
		});
}

void WorldPanel::motion_blur_rows(wiwa::SceneSettings& settings, wiwa::RenderModule* render)
{
	wiwa::MotionBlurSettings& blur = settings.MotionBlur;

	field("Enabled", [&]() { ImGui::Checkbox("Enabled", &blur.Enabled); });
	field("Exposure time (s)", [&]() { ImGui::SliderFloat("Exposure time (s)", &blur.ExposureTime, 0.0005f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic); });
	field("Strength", [&]() { ImGui::SliderFloat("Strength", &blur.Strength, 0.0f, 4.0f); });
	field("Max blur (pixels)", [&]() { ImGui::SliderFloat("Max blur (pixels)", &blur.MaxBlurPixels, 1.0f, 100.0f); });
	field("Show velocity", [&]()
		{
			if (ImGui::Checkbox("Show velocity", &m_ShowVelocity) && render != nullptr)
			{
				render->set_debug_velocity(m_ShowVelocity);
			}
		});
	note("Replaces the image by the motion of every pixel (red: horizontal, green: vertical).");
}

void WorldPanel::scripts_rows(wiwa::SceneSettings& settings)
{
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	std::vector<wiwa::AssetCatalog::Entry> catalog;
	if (!m_Collecting && assets != nullptr)
	{
		catalog = assets->catalog_entries();
	}

	// the path shown for a script, or its short id when the catalog does not list it
	const auto describe = [&](const wiwa::AssetRef& ref)
	{
		for (const wiwa::AssetCatalog::Entry& entry : catalog)
		{
			if (entry.Guid == ref.Guid)
			{
				return entry.Path;
			}
		}
		return ref.Guid.to_string().substr(0, 8);
	};

	const auto add = [&](const std::string& path)
	{
		const std::optional<wiwa::AssetRef> found = assets != nullptr ? assets->find_asset(path) : std::nullopt;
		if (!found)
		{
			m_ScriptStatus = "no such asset";
		}
		else if (path.size() < 4 || path.compare(path.size() - 4, 4, ".lua") != 0)
		{
			m_ScriptStatus = "not a script (.lua)";
		}
		else if (std::find(settings.Scripts.begin(), settings.Scripts.end(), *found) != settings.Scripts.end())
		{
			m_ScriptStatus = "already in the list";
		}
		else
		{
			settings.Scripts.push_back(*found);
			m_ScriptStatus.clear();
		}
	};

	field("Scripts", [&]()
		{
			if (settings.Scripts.empty())
			{
				ImGui::TextDisabled("No scripts");
			}

			int move = -1, direction = 0, remove = -1;
			for (size_t i = 0; i < settings.Scripts.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::BeginDisabled(i == 0);
				if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { move = static_cast<int>(i); direction = -1; }
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(i + 1 == settings.Scripts.size());
				if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { move = static_cast<int>(i); direction = 1; }
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) { remove = static_cast<int>(i); }
				ImGui::SameLine();
				ImGui::TextUnformatted(describe(settings.Scripts[i]).c_str());
				ImGui::PopID();
			}
			if (move >= 0)
			{
				std::swap(settings.Scripts[move], settings.Scripts[move + direction]);
			}
			if (remove >= 0)
			{
				settings.Scripts.erase(settings.Scripts.begin() + remove);
			}
		});
	note("Scene scripts run in this order while the scene plays. They have no entity.");

	field("Add script", [&]()
		{
			ImGui::Button("Drop a .lua script here", ImVec2(-FLT_MIN, 0.0f));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("WI_ASSET"))
				{
					add(reinterpret_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			const bool entered = ImGui::InputText("##script_path", m_ScriptPath, sizeof(m_ScriptPath), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (ImGui::Button("Add") || entered)
			{
				add(m_ScriptPath);
			}
			if (!m_ScriptStatus.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_ScriptStatus.c_str());
			}
		});
}

void WorldPanel::quality_rows(wiwa::RenderModule* render)
{
	if (render == nullptr || !render->is_ready())
	{
		field("Renderer not ready", [&]() { ImGui::TextDisabled("Renderer not ready"); });
		return;
	}

	wiwa::RenderQuality quality = render->get_quality();
	const wiwa::RenderQuality before = quality;

	group("Shadows");
	field("Distance", [&]() { ImGui::DragFloat("Distance", &quality.ShadowDistance, 0.5f, 1.0f, 1000.0f); });
	field("Cascades", [&]()
		{
			int cascades = static_cast<int>(quality.ShadowCascades);
			if (ImGui::SliderInt("Cascades", &cascades, 1, 4))
			{
				quality.ShadowCascades = static_cast<wiwa::u32>(cascades);
			}
		});
	field("Split lambda", [&]() { ImGui::SliderFloat("Split lambda", &quality.ShadowSplitLambda, 0.0f, 1.0f); });
	field("Depth bias", [&]() { ImGui::DragFloat("Depth bias", &quality.ShadowDepthBias, 0.00005f, 0.0f, 0.05f, "%.5f"); });
	field("Normal bias (texels)", [&]() { ImGui::DragFloat("Normal bias (texels)", &quality.ShadowNormalBias, 0.05f, 0.0f, 10.0f); });
	field("Spot depth bias", [&]() { ImGui::DragFloat("Spot depth bias", &quality.SpotShadowBias, 0.00005f, 0.0f, 0.05f, "%.5f"); });

	group("Post-processing");
	field("Anti-aliasing", [&]()
		{
			int antiAliasing = quality.Taa ? 2 : (quality.Fxaa ? 1 : 0);
			if (ImGui::Combo("Anti-aliasing", &antiAliasing, "Off\0FXAA\0TAA\0"))
			{
				quality.Fxaa = antiAliasing == 1;
				quality.Taa = antiAliasing == 2;
			}
		});
	field("Bloom levels", [&]()
		{
			int bloomMips = static_cast<int>(quality.BloomMips);
			if (ImGui::SliderInt("Bloom levels", &bloomMips, 2, 6))
			{
				quality.BloomMips = static_cast<wiwa::u32>(bloomMips);
			}
		});
	field("AO directions", [&]()
		{
			int aoSlices = static_cast<int>(quality.AoSlices);
			if (ImGui::SliderInt("AO directions", &aoSlices, 1, 4))
			{
				quality.AoSlices = static_cast<wiwa::u32>(aoSlices);
			}
		});
	field("AO steps", [&]()
		{
			int aoSteps = static_cast<int>(quality.AoSteps);
			if (ImGui::SliderInt("AO steps", &aoSteps, 4, 16))
			{
				quality.AoSteps = static_cast<wiwa::u32>(aoSteps);
			}
		});

	group("Display");
	field("Vertical sync", [&]() { ImGui::Checkbox("Vertical sync", &quality.Vsync); });
	field("Upload on the copy queue", [&]() { ImGui::Checkbox("Upload on the copy queue", &quality.CopyQueue); });

	if (!(quality == before))
	{
		render->set_quality(quality);
	}
}
