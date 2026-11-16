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

	// The widgets offer the range the sanitizer allows (scene_settings.h), so a value is never one it would change.
	bool slider(const wiwa::SceneSettings& settings, const char* label, float& value, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
	{
		const wiwa::SettingRange range = wiwa::settings_range(settings, value);
		return ImGui::SliderFloat(label, &value, range.Min, range.Max, format, flags);
	}

	bool drag(const wiwa::SceneSettings& settings, const char* label, float& value, float speed)
	{
		const wiwa::SettingRange range = wiwa::settings_range(settings, value);
		return ImGui::DragFloat(label, &value, speed, range.Min, range.Max);
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
				static_assert(IM_ARRAYSIZE(sources) == wiwa::ENVIRONMENT_SOURCE_NAMES.size());
			int source = static_cast<int>(settings.Source);
			if (ImGui::Combo("Source", &source, sources, IM_ARRAYSIZE(sources)))
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
		field("Rotation", [&]() { slider(settings, "Rotation", settings.RotationDegrees, "%.0f deg"); });
		field("Brightness", [&]() { drag(settings, "Brightness", settings.Brightness, 0.01f); });
	}

	if (settings.Source == wiwa::EnvironmentSource::Atmosphere)
	{
		group("Atmosphere");
		wiwa::AtmosphereParameters& a = settings.Atmosphere;
		field("Sun source", [&]() { ImGui::TextDisabled("The sun is the first directional light."); });
		field("Rayleigh", [&]() { slider(settings, "Rayleigh", a.RayleighScale); });
		field("Mie", [&]() { slider(settings, "Mie", a.MieScale); });
		field("Ozone", [&]() { slider(settings, "Ozone", a.OzoneScale); });
		field("Mie anisotropy", [&]() { slider(settings, "Mie anisotropy", a.MieAnisotropy); });
		field("Multiple scattering", [&]() { slider(settings, "Multiple scattering", a.MultipleScattering); });
		field("Ground albedo", [&]() { ImGui::ColorEdit3("Ground albedo", &a.GroundAlbedo.x); });
		field("Sun illuminance", [&]() { drag(settings, "Sun illuminance", a.SunIlluminance, 0.05f); });
		field("Sky brightness", [&]() { drag(settings, "Sky brightness", a.SkyBrightness, 0.05f); });
		field("Sun disc boost", [&]() { drag(settings, "Sun disc boost", a.SunDiscBoost, 1.0f); });
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
	field("Ambient intensity", [&]() { drag(settings, "Ambient intensity", settings.AmbientIntensity, 0.01f); });
	field("Hemisphere sky", [&]() { ImGui::ColorEdit3("Hemisphere sky", &settings.HemisphereSky.x); });
	field("Hemisphere ground", [&]() { ImGui::ColorEdit3("Hemisphere ground", &settings.HemisphereGround.x); });
	note("The hemisphere lights the scene when there is no environment.");

	field("Background", [&]()
		{
			const char* backgrounds[] = { "Sky", "Solid color" };
				static_assert(IM_ARRAYSIZE(backgrounds) == wiwa::BACKGROUND_MODE_NAMES.size());
			int background = static_cast<int>(settings.Background);
			if (ImGui::Combo("Background", &background, backgrounds, IM_ARRAYSIZE(backgrounds)))
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
		field("Exposure (EV)", [&]() { slider(settings, "Exposure (EV)", settings.ExposureEv); });
		return;
	}

	field("Compensation (EV)", [&]() { slider(settings, "Compensation (EV)", exposure.CompensationEv); });
	field("Key", [&]() { slider(settings, "Key", exposure.Key, "%.2f", ImGuiSliderFlags_Logarithmic); });
	note("The brightness the average of the scene is brought to.");
	field("Limits (EV)", [&]() { ImGui::DragFloatRange2("Limits (EV)", &exposure.MinEv, &exposure.MaxEv, 0.1f, wiwa::settings_range(settings, exposure.MinEv).Min, wiwa::settings_range(settings, exposure.MaxEv).Max); });
	field("Adaptation speed", [&]() { slider(settings, "Adaptation speed", exposure.AdaptSpeed, "%.1f", ImGuiSliderFlags_Logarithmic); });
	field("Measured range", [&]() { ImGui::DragFloatRange2("Measured range", &exposure.LowPercent, &exposure.HighPercent, 0.01f, wiwa::settings_range(settings, exposure.LowPercent).Min, wiwa::settings_range(settings, exposure.HighPercent).Max, "%.2f"); });
	note("The darkest and brightest pixels outside it do not count.");
}

void WorldPanel::tonemap_rows(wiwa::SceneSettings& settings)
{
	wiwa::TonemapSettings& tonemap = settings.Tonemap;

	field("Operator", [&]()
		{
			const char* operators[] = { "ACES", "Reinhard", "Neutral", "None (clamp)" };
				static_assert(IM_ARRAYSIZE(operators) == wiwa::TONEMAP_OPERATOR_NAMES.size());
			int current = static_cast<int>(tonemap.Operator);
			if (ImGui::Combo("Operator", &current, operators, IM_ARRAYSIZE(operators)))
			{
				tonemap.Operator = static_cast<wiwa::TonemapOperator>(current);
			}
		});

	group("Grading");
	field("Contrast", [&]() { slider(settings, "Contrast", tonemap.Contrast); });
	field("Saturation", [&]() { slider(settings, "Saturation", tonemap.Saturation); });
	field("Temperature", [&]() { slider(settings, "Temperature", tonemap.Temperature); });
	field("Tint", [&]() { slider(settings, "Tint", tonemap.Tint); });
	field("Vignette", [&]() { slider(settings, "Vignette", tonemap.Vignette); });
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
	field("Threshold", [&]() { slider(settings, "Threshold", bloom.Threshold); });
	field("Knee", [&]() { slider(settings, "Knee", bloom.Knee); });
	field("Intensity", [&]() { slider(settings, "Intensity", bloom.Intensity, "%.3f"); });
	note("The reach of the glow is the bloom levels in the Settings panel (Renderer).");
}

void WorldPanel::ao_rows(wiwa::SceneSettings& settings)
{
	wiwa::AoSettings& ao = settings.Ao;

	field("Enabled", [&]() { ImGui::Checkbox("Enabled", &ao.Enabled); });
	field("Radius", [&]() { slider(settings, "Radius", ao.Radius, "%.2f", ImGuiSliderFlags_Logarithmic); });
	field("Power", [&]() { slider(settings, "Power", ao.Power); });
	note("It darkens the ambient light only. The quality is in the Settings panel (Renderer).");
}

void WorldPanel::taa_rows(wiwa::SceneSettings& settings, wiwa::RenderModule* render)
{
	const bool taaOn = render != nullptr && render->get_quality().Taa;
	field("Anti-aliasing mode", [&]()
		{
			if (!taaOn)
			{
				ImGui::TextDisabled("Temporal anti-aliasing is off. Choose TAA under Anti-aliasing in the Settings panel (Renderer).");
			}
		});
	field("Feedback", [&]()
		{
			ImGui::BeginDisabled(!taaOn);
			slider(settings, "Feedback", settings.Taa.Feedback);
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
	field("Exposure time (s)", [&]() { slider(settings, "Exposure time (s)", blur.ExposureTime, "%.4f", ImGuiSliderFlags_Logarithmic); });
	field("Strength", [&]() { slider(settings, "Strength", blur.Strength); });
	field("Max blur (pixels)", [&]() { slider(settings, "Max blur (pixels)", blur.MaxBlurPixels); });
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
