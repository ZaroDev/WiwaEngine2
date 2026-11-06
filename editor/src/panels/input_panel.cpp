#include <editor_common.h>
#include "input_panel.h"

#include <sstream>

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <input/input_map_io.h>
#include <input/input_module.h>

namespace
{
	wiwa::InputModule* input_module()
	{
		return wiwa::Engine::get().get_module<wiwa::InputModule>();
	}

	bool game_is_playing()
	{
		return wiwa::Engine::get().get_time().get_game_clock().IsPlaying;
	}

	std::string unique_name(const std::string& base, const std::vector<std::string>& taken)
	{
		for (int number = 1;; number++)
		{
			const std::string name = base + " " + std::to_string(number);
			if (std::find(taken.begin(), taken.end(), name) == taken.end())
			{
				return name;
			}
		}
	}

	wiwa::Binding default_binding(wiwa::Binding::Kind kind)
	{
		wiwa::Binding binding;
		binding.BindingKind = kind;
		switch (kind)
		{
		case wiwa::Binding::Kind::Control:
			binding.Parts[0] = wiwa::control(wiwa::Key::Space);
			break;
		case wiwa::Binding::Kind::Composite1D:
			binding.Parts[0] = wiwa::control(wiwa::Key::A);
			binding.Parts[1] = wiwa::control(wiwa::Key::D);
			break;
		case wiwa::Binding::Kind::Composite2D:
			binding.Parts[0] = wiwa::control(wiwa::Key::W);
			binding.Parts[1] = wiwa::control(wiwa::Key::S);
			binding.Parts[2] = wiwa::control(wiwa::Key::A);
			binding.Parts[3] = wiwa::control(wiwa::Key::D);
			break;
		case wiwa::Binding::Kind::Pair2D:
			binding.Parts[0] = wiwa::control(wiwa::PadAxis::LeftX);
			binding.Parts[1] = wiwa::control(wiwa::PadAxis::LeftY);
			binding.Deadzone = 0.2f;
			break;
		}
		return binding;
	}

	const char* kind_label(wiwa::Binding::Kind kind)
	{
		switch (kind)
		{
		case wiwa::Binding::Kind::Control: return "Control";
		case wiwa::Binding::Kind::Composite1D: return "Composite 1D";
		case wiwa::Binding::Kind::Composite2D: return "Composite 2D";
		case wiwa::Binding::Kind::Pair2D: return "Pair 2D";
		}
		return "";
	}

	// An InputText that applies when it is left or Enter is pressed, so that half a name is never an edit.
	bool commit_text(const char* id, std::string& text)
	{
		char buffer[128];
		std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
		ImGui::InputText(id, buffer, sizeof(buffer));
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			text = buffer;
			return true;
		}
		return false;
	}
}

InputPanel::InputPanel(std::filesystem::path settingsFile)
	: Panel("Input", PanelCategory::Window), m_Section(CreateRef<InputSection>(std::move(settingsFile)))
{
}

InputPanel::~InputPanel()
{
	stop_listening();
}

bool InputPanel::open_path(const std::filesystem::path& file)
{
	m_Error.clear();
	stop_listening();

	std::ifstream stream(file, std::ios::binary);
	if (!stream)
	{
		m_Error = "cannot read " + file.string();
		return false;
	}
	std::stringstream text;
	text << stream.rdbuf();

	wiwa::InputMapData data;
	std::string error;
	if (!wiwa::input_map_from_text(text.str(), data, error))
	{
		m_Error = file.filename().string() + ": " + error;
		return false;
	}

	m_Model = CreateScope<wiwa::InputEditModel>(std::move(data));
	m_File = file;
	return true;
}

bool InputPanel::open_map(const wiwa::AssetRef& ref)
{
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	if (assets == nullptr)
	{
		m_Error = "there is no asset manager";
		return false;
	}
	for (const wiwa::AssetCatalog::Entry& entry : assets->catalog_entries())
	{
		if (entry.Guid == ref.Guid)
		{
			return open_path(entry.Path);
		}
	}
	m_Error = "that input map is not in the project";
	return false;
}

bool InputPanel::save()
{
	if (m_Model == nullptr || m_File.empty())
	{
		m_Error = "there is no input map open";
		return false;
	}

	std::string error;
	if (!wiwa::input_map_valid(m_Model->data(), error))
	{
		m_Error = error;
		return false;
	}

	std::filesystem::path temp = m_File;
	temp += ".tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out << wiwa::input_map_to_text(m_Model->data());
		if (!out)
		{
			m_Error = "cannot write " + temp.string();
			return false;
		}
	}
	std::error_code rename;
	std::filesystem::rename(temp, m_File, rename);
	if (rename)
	{
		m_Error = "cannot replace " + m_File.string();
		return false;
	}

	m_Error.clear();
	m_Model->mark_saved();
	return true;
}

bool InputPanel::set_default_map(const wiwa::AssetRef& ref)
{
	if (!m_Section->set_default_map(ref))
	{
		m_Error = m_Section->last_error();
		return false;
	}
	m_Error.clear();
	return true;
}

void InputPanel::stop_listening()
{
	if (m_Listening)
	{
		if (wiwa::InputModule* module = input_module())
		{
			module->actions().cancel_listen();
		}
	}
	m_Listening.reset();
	m_Heard.reset();
}

void InputPanel::apply_heard_control()
{
	if (m_Heard && m_Listening && m_Model)
	{
		if (!m_Model->set_binding_part(m_Listening->Map, m_Listening->Action, m_Listening->Binding, m_Listening->Part, *m_Heard))
		{
			m_Error = "that control cannot be used there";
		}
	}
	if (m_Heard)
	{
		m_Heard.reset();
		m_Listening.reset();
	}
	else if (m_Listening)
	{
		// Escape cancelled it (the module is no longer listening and called nothing)
		wiwa::InputModule* module = input_module();
		if (module == nullptr || !module->actions().listening())
		{
			m_Listening.reset();
		}
	}
}

void InputPanel::on_draw()
{
	m_Frames++; // counted even while the window is a hidden tab: the smoke check wants to know the panel was run
	apply_heard_control();

	if (m_Focus)
	{
		ImGui::SetNextWindowFocus();
		m_Focus = false;
	}
	ImGui::SetNextWindowPos(ImVec2(984.0f, 60.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		toolbar();
		ImGui::TextDisabled("The default input map is a project setting: see the Settings panel.");
		ImGui::Separator();
		if (m_Model != nullptr)
		{
			maps_tree();
		}
		else
		{
			ImGui::TextDisabled("Pick an input map above (or make one with Create > Input Map in the Asset Browser)");
		}
		if (!m_Error.empty())
		{
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());
		}
	}
	ImGui::End();

	// While a control is awaited the editor's own keyboard and mouse go to the input module (outside play mode ImGui owns them).
	if (m_Listening && !game_is_playing())
	{
		if (wiwa::InputModule* module = input_module())
		{
			module->set_capture(true);
		}
	}
}

void InputPanel::toolbar()
{
	const std::string label = m_Model != nullptr ? m_File.filename().string() + (m_Model->dirty() ? " *" : "") : std::string("No map open");
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("##open_map", label.c_str()))
	{
		if (wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>())
		{
			for (const wiwa::AssetCatalog::Entry& entry : assets->catalog_entries())
			{
				if (std::filesystem::path(entry.Path).extension() == ".winput" && ImGui::Selectable(entry.Path.c_str(), entry.Path == m_File.generic_string()))
				{
					open_path(entry.Path);
				}
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(m_Model == nullptr || !m_Model->can_undo());
	if (ImGui::Button("Undo"))
	{
		m_Model->undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(m_Model == nullptr || !m_Model->can_redo());
	if (ImGui::Button("Redo"))
	{
		m_Model->redo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(m_Model == nullptr || !m_Model->dirty());
	if (ImGui::Button("Save"))
	{
		save();
	}
	ImGui::EndDisabled();
}

void InputPanel::maps_tree()
{
	const wiwa::InputMapData& data = m_Model->data();
	for (size_t map = 0; map < data.Maps.size(); map++)
	{
		ImGui::PushID(static_cast<int>(map));
		const std::string header = data.Maps[map].Name + (data.Maps[map].Consume ? "  (consumes)" : "");
		const bool open = ImGui::TreeNodeEx("##map", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", header.c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
		bool removed = ImGui::SmallButton("Remove");
		if (open)
		{
			std::string name = data.Maps[map].Name;
			ImGui::SetNextItemWidth(180.0f);
			if (commit_text("Name##map", name) && !m_Model->rename_map(map, name))
			{
				m_Error = "'" + name + "' is not a usable map name (empty, spaces at an end, or taken)";
			}
			bool consume = data.Maps[map].Consume;
			if (ImGui::Checkbox("Consume (hides its controls from the maps below)", &consume))
			{
				m_Model->set_consume(map, consume);
			}

			for (size_t action = 0; action < data.Maps[map].Actions.size(); action++)
			{
				action_row(map, action);
			}

			if (ImGui::Button("Add action"))
			{
				std::vector<std::string> taken;
				for (const wiwa::Action& existing : data.Maps[map].Actions)
				{
					taken.push_back(existing.Name);
				}
				m_Model->add_action(map, unique_name("Action", taken), wiwa::ActionType::Button);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		if (removed)
		{
			m_Model->remove_map(map);
			break; // the indices changed
		}
	}

	if (ImGui::Button("Add map"))
	{
		std::vector<std::string> taken;
		for (const wiwa::ActionMap& existing : data.Maps)
		{
			taken.push_back(existing.Name);
		}
		m_Model->add_map(unique_name("Map", taken));
	}
}

void InputPanel::action_row(size_t map, size_t action)
{
	const wiwa::Action& current = m_Model->data().Maps[map].Actions[action];
	ImGui::PushID(static_cast<int>(action));

	const bool open = ImGui::TreeNodeEx("##action", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth, "%s  [%s]", current.Name.c_str(), wiwa::action_type_name(current.Type));

	// the live value, while the map is on the stack
	if (wiwa::InputModule* module = input_module())
	{
		const wiwa::ActionState state = module->actions().state(m_Model->data().Maps[map].Name, current.Name);
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f);
		if (current.Type == wiwa::ActionType::Axis2D)
		{
			ImGui::Text("%.2f, %.2f", state.Vector.x, state.Vector.y);
		}
		else
		{
			ImGui::Text("%.2f%s", state.Value, state.Down ? " *" : "");
		}
	}
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
	const bool removed = ImGui::SmallButton("Remove");

	if (open)
	{
		std::string name = current.Name;
		ImGui::SetNextItemWidth(180.0f);
		if (commit_text("Name##action", name) && !m_Model->rename_action(map, action, name))
		{
			m_Error = "'" + name + "' is not a usable action name (empty, spaces at an end, or taken)";
		}

		static const wiwa::ActionType types[] = { wiwa::ActionType::Button, wiwa::ActionType::Axis1D, wiwa::ActionType::Axis2D };
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("Type", wiwa::action_type_name(current.Type)))
		{
			for (wiwa::ActionType type : types)
			{
				if (ImGui::Selectable(wiwa::action_type_name(type), type == current.Type) && !m_Model->set_action_type(map, action, type))
				{
					m_Error = "remove the bindings that a " + std::string(wiwa::action_type_name(type)) + " action cannot use first";
				}
			}
			ImGui::EndCombo();
		}

		for (size_t binding = 0; binding < current.Bindings.size(); binding++)
		{
			binding_row(map, action, binding);
		}

		if (ImGui::BeginCombo("Add binding", "..."))
		{
			for (wiwa::Binding::Kind kind : { wiwa::Binding::Kind::Control, wiwa::Binding::Kind::Composite1D, wiwa::Binding::Kind::Composite2D, wiwa::Binding::Kind::Pair2D })
			{
				if (wiwa::action_accepts(current.Type, kind) && ImGui::Selectable(kind_label(kind)))
				{
					m_Model->add_binding(map, action, default_binding(kind));
				}
			}
			ImGui::EndCombo();
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	if (removed)
	{
		m_Model->remove_action(map, action);
	}
}

void InputPanel::binding_row(size_t map, size_t action, size_t binding)
{
	const wiwa::Binding& current = m_Model->data().Maps[map].Actions[action].Bindings[binding];
	ImGui::PushID(static_cast<int>(binding));
	ImGui::Separator();
	ImGui::TextUnformatted(kind_label(current.BindingKind));
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
	const bool removed = ImGui::SmallButton("Remove");

	for (size_t part = 0; part < wiwa::binding_part_count(current.BindingKind); part++)
	{
		ImGui::PushID(static_cast<int>(part));
		ImGui::TextDisabled("%s", wiwa::binding_part_name(current.BindingKind, part));
		ImGui::SameLine(90.0f);
		control_picker("##control", current.Parts[part], ListenTarget{ map, action, binding, part });
		ImGui::PopID();
	}

	// scale and deadzone: one undo step per drag
	const ListenTarget where{ map, action, binding, 0 };
	const bool dragging = m_Drag.Active && m_Drag.Where.Map == map && m_Drag.Where.Action == action && m_Drag.Where.Binding == binding;
	float scale = dragging ? m_Drag.Scale : current.Scale;
	float deadzone = dragging ? m_Drag.Deadzone : current.Deadzone;
	bool paramsChanged = false;

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Scale", &scale, 0.05f, -10.0f, 10.0f);
	paramsChanged = ImGui::IsItemDeactivatedAfterEdit() || paramsChanged;
	if (ImGui::IsItemActivated())
	{
		m_Drag = ParamDrag{ true, where, current.Scale, current.Deadzone };
	}
	if (ImGui::IsItemActive())
	{
		m_Drag.Scale = scale;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::SliderFloat("Deadzone", &deadzone, 0.0f, 0.99f);
	paramsChanged = ImGui::IsItemDeactivatedAfterEdit() || paramsChanged;
	if (ImGui::IsItemActivated())
	{
		m_Drag = ParamDrag{ true, where, current.Scale, current.Deadzone };
	}
	if (ImGui::IsItemActive())
	{
		m_Drag.Deadzone = deadzone;
	}
	ImGui::SameLine();
	bool invert = current.Invert;
	if (ImGui::Checkbox("Invert", &invert))
	{
		m_Model->set_binding_params(map, action, binding, current.Scale, current.Deadzone, invert);
	}
	if (paramsChanged)
	{
		m_Model->set_binding_params(map, action, binding, scale, deadzone, current.Invert);
		m_Drag.Active = false;
	}
	else if (dragging && !ImGui::IsAnyItemActive())
	{
		m_Drag.Active = false;
	}

	ImGui::PopID();
	if (removed)
	{
		m_Model->remove_binding(map, action, binding);
	}
}

void InputPanel::control_picker(const char* id, const wiwa::InputControl& current, const ListenTarget& target)
{
	const bool listeningHere = m_Listening && m_Listening->Map == target.Map && m_Listening->Action == target.Action &&
		m_Listening->Binding == target.Binding && m_Listening->Part == target.Part;

	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo(id, listeningHere ? "press a key... (Esc cancels)" : wiwa::control_name(current).c_str()))
	{
		for (const wiwa::InputControl& candidate : wiwa::all_controls())
		{
			if (ImGui::Selectable(wiwa::control_name(candidate).c_str(), candidate == current) && !m_Model->set_binding_part(target.Map, target.Action, target.Binding, target.Part, candidate))
			{
				m_Error = "that control cannot be used there";
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(game_is_playing());
	if (ImGui::SmallButton(listeningHere ? "Cancel" : "Listen"))
	{
		wiwa::InputModule* module = input_module();
		if (listeningHere)
		{
			stop_listening();
		}
		else if (module != nullptr)
		{
			m_Heard.reset();
			m_Listening = target;
			module->actions().listen_for_control([this](const wiwa::InputControl& heard) { m_Heard = heard; });
		}
	}
	ImGui::EndDisabled();
}
