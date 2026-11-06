#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "panel.h"

#include <assets/asset_guid.h>
#include <input/input_codes.h>
#include <input/input_project_settings.h>

#include "../input/input_edit_model.h"
#include "../settings/input_section.h"

// The "Input" panel: edits a .winput asset (action maps -> actions -> bindings) through an InputEditModel, so every edit can be undone, and
// saves the text file back (the asset pipeline re-cooks it). It lets the player's press pick a control ("Listen"), and shows the live value of each action while its map is on the stack.
// Main thread, between the ImGui frame's begin and end.
class InputPanel final : public Panel
{
public:
	// `settingsFile`: where the default map is saved (input.wsettings in the editor's working directory); the Settings panel edits it.
	explicit InputPanel(std::filesystem::path settingsFile);
	virtual ~InputPanel() override;

	virtual void on_draw() override;

	// Loads a .winput source file for editing. False, and why in last_error, if it cannot be read or is not a valid map.
	bool open_path(const std::filesystem::path& file);
	// The same for an asset of the project (found through the asset catalog).
	bool open_map(const wiwa::AssetRef& ref);
	// Writes the open map to its file. False, and why in last_error, if there is none or the write failed.
	bool save();
	wiwa::InputEditModel* model() { return m_Model.get(); }
	const std::filesystem::path& file() const { return m_File; }

	// Makes the asset the project's default input map (InputSection): saves the settings file and tells the input module.
	bool set_default_map(const wiwa::AssetRef& ref);
	const wiwa::InputProjectSettings& settings() const { return m_Section->settings(); }
	const Ref<InputSection>& section() const { return m_Section; }

	const std::string& last_error() const { return m_Error; }
	wiwa::u32 frames_drawn() const { return m_Frames; }
	// Brings the window to the front of its dock on the next frame (the smoke check wants the contents drawn).
	void request_focus() { m_Focus = true; }

private:
	struct ListenTarget
	{
		size_t Map = 0;
		size_t Action = 0;
		size_t Binding = 0;
		size_t Part = 0;
	};

	void toolbar();
	void maps_tree();
	void action_row(size_t map, size_t action);
	void binding_row(size_t map, size_t action, size_t binding);
	void control_picker(const char* id, const wiwa::InputControl& current, const ListenTarget& target);
	void apply_heard_control();
	void stop_listening();

	Ref<InputSection> m_Section;
	std::filesystem::path m_File{};
	Scope<wiwa::InputEditModel> m_Model{};
	std::string m_Error{};
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;

	// "Listen": the module calls back from its update with the control that was pressed; it is applied at the next draw.
	std::optional<ListenTarget> m_Listening{};
	std::optional<wiwa::InputControl> m_Heard{};

	// A drag of a scale or deadzone is one undo step: the values live here while the widget is held.
	struct ParamDrag
	{
		bool Active = false;
		ListenTarget Where{};
		float Scale = 1.0f;
		float Deadzone = 0.0f;
	} m_Drag{};
};
