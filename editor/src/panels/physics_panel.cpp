#include <editor_common.h>
#include "physics_panel.h"

#include <algorithm>

#include <imgui.h>

#include <core/engine.h>
#include <physics/physics_module.h>

#include <asset_pipeline/physics_settings_file.h>

PhysicsPanel::PhysicsPanel(wiwa::SceneEditor& editor, std::filesystem::path layersFile)
	: Panel("Physics", PanelCategory::Window), m_Editor(editor), m_File(std::move(layersFile))
{
	if (!wiwa::load_physics_layers(m_File, m_Layers, m_Error))
	{
		WI_WARN("Physics", "{0}: {1}; the default layers are used", m_File.string(), m_Error);
	}
	if (wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
	{
		physics->set_layers(m_Layers);
	}
}

bool PhysicsPanel::save_layers()
{
	m_Error.clear();
	if (wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
	{
		physics->set_layers(m_Layers);
	}
	return wiwa::save_physics_layers(m_File, m_Layers, m_Error);
}

bool PhysicsPanel::set_collides(wiwa::u32 a, wiwa::u32 b, bool collides)
{
	m_Layers.set_collides(a, b, collides);
	return save_layers();
}

bool PhysicsPanel::set_layer_name(wiwa::u32 layer, const std::string& name)
{
	if (layer >= wiwa::PHYSICS_LAYER_COUNT)
	{
		return false;
	}
	m_Layers.Names[layer] = name;
	return save_layers();
}

void PhysicsPanel::on_draw()
{
	m_Frames++; // counted even while the window is a hidden tab: the smoke check wants to know the panel was run
	if (m_Focus)
	{
		ImGui::SetNextWindowFocus();
		m_Focus = false;
	}
	ImGui::SetNextWindowPos(ImVec2(984.0f, 60.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		if (ImGui::BeginTabBar("##physics_tabs"))
		{
			if (ImGui::BeginTabItem("Settings"))
			{
				settings_tab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Layers"))
			{
				layers_tab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Stats"))
			{
				stats_tab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void PhysicsPanel::settings_tab()
{
	if (m_Editor.get_scene() == nullptr)
	{
		ImGui::TextDisabled("No scene");
		return;
	}

	if (wiwa::PhysicsModule* module = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
	{
		bool draw = module->debug_draw();
		if (ImGui::Checkbox("Draw colliders", &draw))
		{
			module->set_debug_draw(draw);
		}
		ImGui::TextDisabled("While a world runs (Play). Not available in Shipping builds.");
	}
	ImGui::Separator();
	const wiwa::SceneSettings before = m_Editor.settings();
	wiwa::SceneSettings settings = before;
	wiwa::PhysicsSettings& physics = settings.Physics;

	ImGui::DragFloat3("Gravity", &physics.Gravity.x, 0.05f);
	int rate = static_cast<int>(physics.FixedRateHz);
	if (ImGui::DragInt("Fixed rate (Hz)", &rate, 1.0f, 1, 240))
	{
		physics.FixedRateHz = static_cast<wiwa::u32>(std::max(rate, 1));
	}
	int maxSteps = static_cast<int>(physics.MaxStepsPerWake);
	if (ImGui::DragInt("Max steps per wake", &maxSteps, 0.1f, 1, 16))
	{
		physics.MaxStepsPerWake = static_cast<wiwa::u32>(std::max(maxSteps, 1));
	}
	ImGui::TextDisabled("Past this many steps in one go the simulation falls behind real time.");
	int velocity = static_cast<int>(physics.VelocityIterations);
	if (ImGui::DragInt("Velocity iterations", &velocity, 0.1f, 1, 64))
	{
		physics.VelocityIterations = static_cast<wiwa::u32>(std::max(velocity, 1));
	}
	int position = static_cast<int>(physics.PositionIterations);
	if (ImGui::DragInt("Position iterations", &position, 0.1f, 1, 64))
	{
		physics.PositionIterations = static_cast<wiwa::u32>(std::max(position, 1));
	}
	ImGui::DragFloat("Sleep threshold (m/s)", &physics.SleepThreshold, 0.001f, 0.0f, 5.0f, "%.3f");

	if (!(settings == before))
	{
		m_Editor.edit_settings(settings);
	}
}

void PhysicsPanel::layers_tab()
{
	// the layers that have a name, and the first one without (to name next)
	wiwa::u32 shown = 1;
	for (wiwa::u32 i = 0; i < wiwa::PHYSICS_LAYER_COUNT; i++)
	{
		if (!m_Layers.Names[i].empty())
		{
			shown = i + 1;
		}
	}
	shown = std::min(shown + 1, wiwa::PHYSICS_LAYER_COUNT);

	ImGui::TextDisabled("Names (layer 0 is the default of a collider). The matrix says which layers collide.");
	for (wiwa::u32 i = 0; i < shown; i++)
	{
		char text[64];
		std::snprintf(text, sizeof(text), "%s", m_Layers.Names[i].c_str());
		ImGui::SetNextItemWidth(220.0f);
		char label[32];
		std::snprintf(label, sizeof(label), "%u##layer_name_%u", i, i);
		if (ImGui::InputText(label, text, sizeof(text)) && (i != 0 || text[0] != '\0'))
		{
			set_layer_name(i, text);
		}
	}

	ImGui::Separator();
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
			ImGui::Text("%u %s", a, m_Layers.Names[a].c_str());
			for (wiwa::u32 b = 0; b <= a; b++) // the matrix is symmetric: the lower triangle says it all
			{
				ImGui::TableSetColumnIndex(static_cast<int>(b) + 1);
				bool collides = m_Layers.collides(a, b);
				char id[32];
				std::snprintf(id, sizeof(id), "##m_%u_%u", a, b);
				if (ImGui::Checkbox(id, &collides))
				{
					set_collides(a, b, collides);
				}
			}
		}
		ImGui::EndTable();
	}
	if (!m_Error.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());
	}
}

void PhysicsPanel::stats_tab()
{
	const wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>();
	if (physics == nullptr || !physics->running())
	{
		ImGui::TextDisabled("No physics world is running (press Play)");
		return;
	}
	const wiwa::PhysicsModule::Stats stats = physics->stats();
	ImGui::Text("Bodies: %u (%u awake)", stats.Bodies, stats.ActiveBodies);
	ImGui::Text("Joints: %u", stats.Joints);
	ImGui::Text("Characters: %u", stats.Characters);
	ImGui::Text("Step: %.3f ms", stats.StepMs);
	ImGui::Text("Steps last frame: %u", stats.StepsLastFrame);
	ImGui::Text("Dropped steps: %llu", static_cast<unsigned long long>(stats.DroppedSteps));
}
