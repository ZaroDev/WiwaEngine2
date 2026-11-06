#include <editor_common.h>
#include "settings_panel.h"

#include <algorithm>
#include <cctype>

#include <assets/asset_manager.h>
#include <core/engine.h>

namespace
{
	std::string lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// An ImGui text box on a std::string (the value is cut at `capacity` characters).
	bool text_input(const char* label, std::string& value, size_t capacity = 128)
	{
		std::string buffer = value.substr(0, capacity - 1);
		buffer.resize(capacity);
		if (ImGui::InputText(label, buffer.data(), capacity))
		{
			value = buffer.c_str();
			return true;
		}
		return false;
	}

	bool count_input(const char* label, wiwa::u32& value, wiwa::u32 low, wiwa::u32 high, float speed = 1.0f)
	{
		int number = static_cast<int>(value);
		if (ImGui::DragInt(label, &number, speed, static_cast<int>(low), static_cast<int>(high)))
		{
			value = static_cast<wiwa::u32>(std::clamp(number, static_cast<int>(low), static_cast<int>(high)));
			return true;
		}
		return false;
	}
}

SettingsPanel::SettingsPanel(Ref<ProjectSection> project, Ref<RendererSection> renderer, Ref<PhysicsSection> physics, Ref<InputSection> input, Ref<EditorSection> editor)
	: Panel("Settings", PanelCategory::Window), m_Project(std::move(project)), m_Renderer(std::move(renderer)), m_Physics(std::move(physics)), m_Input(std::move(input)), m_Editor(std::move(editor))
{
	m_Open = false; // opened from the Window menu
}

const char* SettingsPanel::page_name(Page page)
{
	switch (page)
	{
	case Page::Project: return "Project";
	case Page::Launch: return "Launch";
	case Page::Performance: return "Performance";
	case Page::Renderer: return "Renderer";
	case Page::Physics: return "Physics";
	case Page::Input: return "Input";
	case Page::Editor: return "Editor";
	default: return "";
	}
}

bool SettingsPanel::matches(const char* text) const
{
	return m_Needle.empty() || lower(text).find(m_Needle) != std::string::npos;
}

void SettingsPanel::restart_note() const
{
	ImGui::SameLine();
	ImGui::TextDisabled("(restart needed)");
}

void SettingsPanel::show_error(const std::string& error) const
{
	if (!error.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
	}
}

void SettingsPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(300.0f, 80.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputTextWithHint("##settings_search", "Search settings", m_Search, sizeof(m_Search));
		ImGui::SameLine();
		if (ImGui::Button("x##settings_clear", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
		{
			m_Search[0] = '\0';
		}
		m_Needle = lower(m_Search);
		ImGui::Separator();

		if (!m_Needle.empty())
		{
			// searching: every page, only the rows that match
			ImGui::BeginChild("##settings_content");
			for (int page = 0; page < static_cast<int>(Page::Count); page++)
			{
				draw_page(static_cast<Page>(page));
			}
			ImGui::EndChild();
		}
		else
		{
			ImGui::BeginChild("##settings_pages", ImVec2(130.0f, 0.0f), ImGuiChildFlags_Borders);
			for (int page = 0; page < static_cast<int>(Page::Count); page++)
			{
				if (ImGui::Selectable(page_name(static_cast<Page>(page)), m_Page == static_cast<Page>(page)))
				{
					m_Page = static_cast<Page>(page);
				}
			}
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("##settings_content");
			draw_page(m_Page);
			ImGui::EndChild();
		}
	}
	ImGui::End();
}

void SettingsPanel::draw_page(Page page)
{
	m_Title = page_name(page);
	m_TitlePending = !m_Needle.empty();
	ImGui::PushID(m_Title);
	switch (page)
	{
	case Page::Project: project_rows(); break;
	case Page::Launch: launch_rows(); break;
	case Page::Performance: performance_rows(); break;
	case Page::Renderer: renderer_rows(); break;
	case Page::Physics: physics_rows(); break;
	case Page::Input: input_rows(); break;
	case Page::Editor: editor_rows(); break;
	default: break;
	}
	ImGui::PopID();
}

void SettingsPanel::project_rows()
{
	const wiwa::ProjectSettings defaults{};
	const wiwa::ProjectSettings before = m_Project->settings();
	wiwa::ProjectSettings s = before;

	field("Name", [&]()
		{
			text_input("Name", s.Name);
			reset_button("reset##name", s.Name, defaults.Name);
		});
	field("Company", [&]()
		{
			text_input("Company", s.Company);
			reset_button("reset##company", s.Company, defaults.Company);
		});
	field("Version", [&]()
		{
			ImGui::SetNextItemWidth(60.0f);
			count_input("##major", s.VersionMajor, 0, 9999, 0.1f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			count_input("##minor", s.VersionMinor, 0, 9999, 0.1f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			count_input("##patch", s.VersionPatch, 0, 9999, 0.1f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			text_input("##label", s.VersionLabel, 32);
			ImGui::SameLine();
			ImGui::Text("Version (%s)", wiwa::version_string(s).c_str());
		});
	field("Description", [&]()
		{
			text_input("Description", s.Description, 512);
			reset_button("reset##description", s.Description, defaults.Description);
		});

	if (!(s == before))
	{
		m_Project->set(s);
	}
	show_error(m_Project->last_error());
}

void SettingsPanel::launch_rows()
{
	const wiwa::ProjectSettings defaults{};
	const wiwa::ProjectSettings before = m_Project->settings();
	wiwa::ProjectSettings s = before;

	if (m_Needle.empty())
	{
		ImGui::TextDisabled("How a game starts. The editor keeps its own window; a command-line flag (--api=...) still wins.");
	}
	field("Window title", [&]()
		{
			text_input("Window title", s.WindowTitle);
			restart_note();
			reset_button("reset##title", s.WindowTitle, defaults.WindowTitle);
			if (s.WindowTitle.empty())
			{
				ImGui::TextDisabled("Empty: the project name.");
			}
		});
	field("Width", [&]()
		{
			count_input("Width", s.WindowWidth, 160, 16384, 4.0f);
			restart_note();
			reset_button("reset##width", s.WindowWidth, defaults.WindowWidth);
		});
	field("Height", [&]()
		{
			count_input("Height", s.WindowHeight, 160, 16384, 4.0f);
			restart_note();
			reset_button("reset##height", s.WindowHeight, defaults.WindowHeight);
		});
	field("Window mode", [&]()
		{
			int mode = static_cast<int>(s.Mode);
			if (ImGui::Combo("Window mode", &mode, "Windowed\0Borderless\0Fullscreen\0"))
			{
				s.Mode = static_cast<wiwa::WindowMode>(mode);
			}
			restart_note();
			reset_button("reset##mode", s.Mode, defaults.Mode);
		});
	field("Resizable", [&]()
		{
			ImGui::Checkbox("Resizable", &s.Resizable);
			restart_note();
			reset_button("reset##resizable", s.Resizable, defaults.Resizable);
		});
	field("Graphics API", [&]()
		{
			int api = static_cast<int>(s.Api);
			if (ImGui::Combo("Graphics API", &api, "Vulkan\0D3D11\0D3D12\0"))
			{
				s.Api = static_cast<wiwa::LaunchApi>(api);
			}
			restart_note();
			reset_button("reset##api", s.Api, defaults.Api);
		});
	field("Debug draw on start", [&]()
		{
			ImGui::Checkbox("Debug draw on start", &s.DebugDrawOnStart);
			restart_note();
			reset_button("reset##debugdraw", s.DebugDrawOnStart, defaults.DebugDrawOnStart);
		});
	field("GPU validation", [&]()
		{
			ImGui::Checkbox("GPU validation", &s.GpuValidation);
			restart_note();
			reset_button("reset##validation", s.GpuValidation, defaults.GpuValidation);
		});

	if (!(s == before))
	{
		m_Project->set(s);
	}
	show_error(m_Project->last_error());
}

void SettingsPanel::performance_rows()
{
	const wiwa::ProjectSettings defaults{};
	const wiwa::ProjectSettings before = m_Project->settings();
	wiwa::ProjectSettings s = before;

	field("FPS cap", [&]()
		{
			count_input("FPS cap", s.FpsCap, 0, 1000);
			restart_note();
			reset_button("reset##fps", s.FpsCap, defaults.FpsCap);
			ImGui::TextDisabled("0 is uncapped. A game only; vertical sync is in Renderer.");
		});
	field("Fixed rate (Hz)", [&]()
		{
			count_input("Fixed rate (Hz)", s.FixedRate, 0, 240);
			reset_button("reset##rate", s.FixedRate, defaults.FixedRate);
			ImGui::TextDisabled("Physics steps per second a new scene starts with; 0 is the built-in default. Existing scenes keep theirs.");
		});

	if (!(s == before))
	{
		m_Project->set(s);
	}
	show_error(m_Project->last_error());
}

void SettingsPanel::renderer_rows()
{
	if (!m_Renderer->ready())
	{
		field("Renderer not ready", [&]() { ImGui::TextDisabled("Renderer not ready"); });
		return;
	}

	wiwa::RenderQuality q = m_Renderer->quality();
	const wiwa::RenderQuality before = q;
	const wiwa::RenderQuality defaults{};
	auto group = [&](const char* name)
	{
		if (m_Needle.empty())
		{
			ImGui::SeparatorText(name);
		}
	};

	group("Shadows");
	field("Distance", [&]() { ImGui::DragFloat("Distance", &q.ShadowDistance, 0.5f, 1.0f, 1000.0f); });
	field("Cascades", [&]()
		{
			int cascades = static_cast<int>(q.ShadowCascades);
			if (ImGui::SliderInt("Cascades", &cascades, 1, 4))
			{
				q.ShadowCascades = static_cast<wiwa::u32>(cascades);
			}
		});
	field("Split lambda", [&]() { ImGui::SliderFloat("Split lambda", &q.ShadowSplitLambda, 0.0f, 1.0f); });
	field("Depth bias", [&]() { ImGui::DragFloat("Depth bias", &q.ShadowDepthBias, 0.00005f, 0.0f, 0.05f, "%.5f"); });
	field("Normal bias (texels)", [&]() { ImGui::DragFloat("Normal bias (texels)", &q.ShadowNormalBias, 0.05f, 0.0f, 10.0f); });
	field("Spot depth bias", [&]() { ImGui::DragFloat("Spot depth bias", &q.SpotShadowBias, 0.00005f, 0.0f, 0.05f, "%.5f"); });

	group("Post-processing");
	field("Anti-aliasing", [&]()
		{
			int antiAliasing = q.Taa ? 2 : (q.Fxaa ? 1 : 0);
			if (ImGui::Combo("Anti-aliasing", &antiAliasing, "Off\0FXAA\0TAA\0"))
			{
				q.Fxaa = antiAliasing == 1;
				q.Taa = antiAliasing == 2;
			}
		});
	field("Bloom levels", [&]()
		{
			int bloomMips = static_cast<int>(q.BloomMips);
			if (ImGui::SliderInt("Bloom levels", &bloomMips, 2, 6))
			{
				q.BloomMips = static_cast<wiwa::u32>(bloomMips);
			}
		});
	field("AO directions", [&]()
		{
			int aoSlices = static_cast<int>(q.AoSlices);
			if (ImGui::SliderInt("AO directions", &aoSlices, 1, 4))
			{
				q.AoSlices = static_cast<wiwa::u32>(aoSlices);
			}
		});
	field("AO steps", [&]()
		{
			int aoSteps = static_cast<int>(q.AoSteps);
			if (ImGui::SliderInt("AO steps", &aoSteps, 4, 16))
			{
				q.AoSteps = static_cast<wiwa::u32>(aoSteps);
			}
		});

	group("Display");
	field("Vertical sync", [&]() { ImGui::Checkbox("Vertical sync", &q.Vsync); });
	field("Upload on the copy queue", [&]() { ImGui::Checkbox("Upload on the copy queue", &q.CopyQueue); });

	if (m_Needle.empty())
	{
		ImGui::Spacing();
		if (ImGui::Button("Reset quality to the defaults"))
		{
			q = defaults;
		}
		ImGui::TextDisabled("Saved to render_quality.json next to the program.");
	}

	if (!(q == before))
	{
		m_Renderer->set_quality(q);
	}
}

void SettingsPanel::physics_rows()
{
	field("Layer names", [&]()
		{
			// the layers that have a name, and the first one without (to name next)
			wiwa::u32 shown = 1;
			for (wiwa::u32 i = 0; i < wiwa::PHYSICS_LAYER_COUNT; i++)
			{
				if (!m_Physics->layers().Names[i].empty())
				{
					shown = i + 1;
				}
			}
			shown = std::min(shown + 1, wiwa::PHYSICS_LAYER_COUNT);

			ImGui::TextDisabled("Layer 0 is the default of a collider. The matrix says which layers collide.");
			for (wiwa::u32 i = 0; i < shown; i++)
			{
				std::string name = m_Physics->layers().Names[i];
				ImGui::SetNextItemWidth(220.0f);
				const std::string label = std::to_string(i) + "##layer_name_" + std::to_string(i);
				if (text_input(label.c_str(), name, 64) && (i != 0 || !name.empty()))
				{
					m_Physics->set_layer_name(i, name);
				}
			}
		});

	field("Collision matrix", [&]()
		{
			wiwa::u32 shown = 1;
			for (wiwa::u32 i = 0; i < wiwa::PHYSICS_LAYER_COUNT; i++)
			{
				if (!m_Physics->layers().Names[i].empty())
				{
					shown = i + 1;
				}
			}
			shown = std::min(shown + 1, wiwa::PHYSICS_LAYER_COUNT);

			if (ImGui::BeginTable("##layer_matrix", static_cast<int>(shown) + 1, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders))
			{
				ImGui::TableSetupColumn("");
				for (wiwa::u32 b = 0; b < shown; b++)
				{
					ImGui::TableSetupColumn(std::to_string(b).c_str());
				}
				ImGui::TableHeadersRow();
				for (wiwa::u32 a = 0; a < shown; a++)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u %s", a, m_Physics->layers().Names[a].c_str());
					for (wiwa::u32 b = 0; b <= a; b++) // the matrix is symmetric: the lower triangle says it all
					{
						ImGui::TableSetColumnIndex(static_cast<int>(b) + 1);
						bool collides = m_Physics->layers().collides(a, b);
						const std::string id = "##m_" + std::to_string(a) + "_" + std::to_string(b);
						if (ImGui::Checkbox(id.c_str(), &collides))
						{
							m_Physics->set_collides(a, b, collides);
						}
					}
				}
				ImGui::EndTable();
			}
		});
	show_error(m_Physics->last_error());
}

void SettingsPanel::input_rows()
{
	field("Default input map", [&]()
		{
			wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
			const wiwa::AssetRef current = m_Input->settings().DefaultMap;
			std::string shown = "None";
			if (current.valid())
			{
				shown = current.Guid.to_string();
				if (assets != nullptr)
				{
					for (const wiwa::AssetCatalog::Entry& entry : assets->catalog_entries())
					{
						if (entry.Guid == current.Guid)
						{
							shown = entry.Path;
						}
					}
				}
			}

			if (ImGui::BeginCombo("Default input map", shown.c_str()))
			{
				if (ImGui::Selectable("None", !current.valid()))
				{
					m_Input->set_default_map(wiwa::AssetRef{});
				}
				if (assets != nullptr)
				{
					for (const wiwa::AssetCatalog::Entry& entry : assets->catalog_entries())
					{
						if (std::filesystem::path(entry.Path).extension() == ".winput" && ImGui::Selectable(entry.Path.c_str(), entry.Guid == current.Guid))
						{
							if (const std::optional<wiwa::AssetRef> ref = assets->find_asset(entry.Path))
							{
								m_Input->set_default_map(*ref);
							}
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::TextDisabled("The maps themselves (actions and bindings) are edited in the Input panel.");
		});
	show_error(m_Input->last_error());
}

void SettingsPanel::editor_rows()
{
	wiwa::GizmoSettings& g = m_Editor->gizmos_mutable();
	const wiwa::GizmoSettings defaults{};
	// one row per value: the widget, then a reset button that shows when the value differs
	auto toggle = [&](const char* label, bool& value, bool defaultValue)
	{
		field(label, [&]()
			{
				bool changed = ImGui::Checkbox(label, &value);
				changed |= reset_button("reset##r", value, defaultValue);
				if (changed)
				{
					m_Editor->touch();
				}
			});
	};
	toggle("Transform handles of the selection", g.TransformHandles, defaults.TransformHandles);
	toggle("Grid", g.Grid, defaults.Grid);
	toggle("World axes", g.Axes, defaults.Axes);
	toggle("Lights", g.Lights, defaults.Lights);
	toggle("Cameras", g.Cameras, defaults.Cameras);
	toggle("Bounds of the selection", g.Bounds, defaults.Bounds);
	toggle("Outline the selection", g.Outline.Enabled, defaults.Outline.Enabled);
	field("Outline color", [&]()
		{
			bool changed = ImGui::ColorEdit3("Outline color", &g.Outline.Color.x, ImGuiColorEditFlags_NoInputs);
			changed |= reset_button("reset##r", g.Outline.Color, defaults.Outline.Color);
			if (changed)
			{
				m_Editor->touch();
			}
		});
	field("Outline width", [&]()
		{
			bool changed = ImGui::SliderFloat("Outline width", &g.Outline.WidthPx, 1.0f, 8.0f, "%.0f px");
			changed |= reset_button("reset##r", g.Outline.WidthPx, defaults.Outline.WidthPx);
			if (changed)
			{
				m_Editor->touch();
			}
		});
	field("Outline hidden part", [&]()
		{
			bool changed = ImGui::SliderFloat("Outline hidden part", &g.Outline.OccludedAlpha, 0.0f, 1.0f, "%.2f");
			changed |= reset_button("reset##r", g.Outline.OccludedAlpha, defaults.Outline.OccludedAlpha);
			if (changed)
			{
				m_Editor->touch();
			}
		});
	// saved when the mouse is let go; the Viewport's Gizmos popup edits the same value
	m_Editor->flush(ImGui::IsMouseDown(ImGuiMouseButton_Left));
	show_error(m_Editor->last_error());
}
