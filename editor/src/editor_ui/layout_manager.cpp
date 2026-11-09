#include <editor_common.h>
#include "layout_manager.h"

#include <fstream>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>

#include <core/engine.h>

namespace
{
	struct Preset
	{
		const char* Name;
		float Left;   // the share of the width, 0: none
		float Right;
		float Bottom; // the share of the height under the middle
		std::vector<const char*> LeftWindows;
		std::vector<const char*> RightWindows;
		std::vector<const char*> BottomWindows;
		std::vector<const char*> CenterWindows;
	};

	// Panel names; the UI Editor is docked by its stable id ("###UiEditor", its title carries the file name).
	const std::vector<Preset>& presets()
	{
		static const std::vector<Preset> list = {
			{ "Default", 0.18f, 0.24f, 0.28f, { "Hierarchy" }, { "Inspector", "World" }, { "Asset Browser", "Console" }, { "Viewport", "Game" } },
			{ "Level Design", 0.16f, 0.22f, 0.22f, { "Hierarchy" }, { "Inspector", "World", "Physics" }, { "Asset Browser", "Console" }, { "Viewport", "Game" } },
			{ "Animation", 0.16f, 0.24f, 0.30f, { "Hierarchy" }, { "Inspector" }, { "Animation", "Console" }, { "Viewport" } },
			{ "UI Editing", 0.16f, 0.22f, 0.22f, { "Hierarchy" }, { "Inspector" }, { "Asset Browser", "Console" }, { "###UiEditor", "Viewport" } },
			{ "Debug", 0.16f, 0.24f, 0.30f, { "Hierarchy" }, { "Inspector", "Physics" }, { "Console", "Performance" }, { "Viewport", "Game" } },
		};
		return list;
	}

	bool lists(const std::vector<const char*>& windows, const char* name)
	{
		return std::any_of(windows.begin(), windows.end(), [name](const char* window) { return std::strcmp(window, name) == 0; });
	}

	bool preset_has(const Preset& preset, const char* window)
	{
		return lists(preset.LeftWindows, window) || lists(preset.RightWindows, window) || lists(preset.BottomWindows, window) || lists(preset.CenterWindows, window);
	}

	void build_preset(const Preset& preset, ImGuiID root)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(root);
		ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(root, viewport->WorkSize);

		ImGuiID center = root;
		ImGuiID left = 0;
		ImGuiID right = 0;
		ImGuiID bottom = 0;
		if (preset.Left > 0.0f)
		{
			left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, preset.Left, nullptr, &center);
		}
		if (preset.Right > 0.0f)
		{
			// the share is of what is left after the left column
			right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, preset.Right / (1.0f - preset.Left), nullptr, &center);
		}
		if (preset.Bottom > 0.0f)
		{
			bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, preset.Bottom, nullptr, &center);
		}

		const auto dock = [](const std::vector<const char*>& windows, ImGuiID node)
		{
			for (const char* window : windows)
			{
				ImGui::DockBuilderDockWindow(window, node);
			}
		};
		dock(preset.LeftWindows, left);
		dock(preset.RightWindows, right);
		dock(preset.BottomWindows, bottom);
		dock(preset.CenterWindows, center);
		ImGui::DockBuilderFinish(root);
	}

	ImGuiWindow* window_of(const Panel& panel)
	{
		return ImGui::FindWindowByName(panel.window_name());
	}
}

namespace
{
	bool persists_open_state(const Panel& panel)
	{
		return panel.get_category() != PanelCategory::None;
	}

	Panel* find_panel(std::vector<Ref<Panel>>& panels, const char* windowName)
	{
		for (const Ref<Panel>& panel : panels)
		{
			if (persists_open_state(*panel) && std::strcmp(panel->window_name(), windowName) == 0)
			{
				return panel.get();
			}
		}
		return nullptr;
	}
}

void LayoutManager::init(const std::filesystem::path& folder, std::vector<Ref<Panel>>& panels)
{
	m_Panels = &panels;
	// Panel open flags ride in the same ini text as the dock layout. Entries ("<window>=0|1") are read into the panel of that name; a panel with
	// no entry (an older ini) keeps its default.
	ImGuiSettingsHandler handler;
	handler.TypeName = "PanelState";
	handler.TypeHash = ImHashStr("PanelState");
	handler.UserData = this;
	handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* { return std::strcmp(name, "Panels") == 0 ? reinterpret_cast<void*>(1) : nullptr; };
	handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* self, void*, const char* line)
	{
		const char* equals = std::strrchr(line, '=');
		if (equals == nullptr || (equals[1] != '0' && equals[1] != '1'))
		{
			return;
		}
		const std::string name(line, equals);
		if (Panel* panel = find_panel(*static_cast<LayoutManager*>(self->UserData)->m_Panels, name.c_str()))
		{
			panel->set_open(equals[1] == '1');
		}
	};
	handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out)
	{
		out->appendf("[%s][Panels]\n", self->TypeName);
		for (const Ref<Panel>& panel : *static_cast<LayoutManager*>(self->UserData)->m_Panels)
		{
			if (persists_open_state(*panel))
			{
				out->appendf("%s=%d\n", panel->window_name(), panel->is_active() ? 1 : 0);
			}
		}
		out->append("\n");
	};
	ImGui::AddSettingsHandler(&handler);

	m_Folder = folder;
	std::error_code error;
	std::filesystem::create_directories(m_Folder, error);

	// the first run: ImGui has no layout of ours yet
	const char* iniFile = ImGui::GetIO().IniFilename;
	if (iniFile != nullptr && !std::filesystem::exists(iniFile, error))
	{
		m_PendingBuiltin = "Default";
	}
}

const std::vector<std::string>& LayoutManager::builtin_names()
{
	static const std::vector<std::string> names = []()
	{
		std::vector<std::string> list;
		for (const Preset& preset : presets())
		{
			list.push_back(preset.Name);
		}
		return list;
	}();
	return names;
}

bool LayoutManager::valid_name(const std::string& name)
{
	if (name.empty() || name.size() > 64)
	{
		return false;
	}
	return name.find_first_of("\\/:*?\"<>|") == std::string::npos && name.front() != '.' && name.back() != '.' && name.back() != ' ';
}

std::vector<std::string> LayoutManager::user_names() const
{
	std::vector<std::string> names;
	std::error_code error;
	for (std::filesystem::directory_iterator it(m_Folder, error), end; !error && it != end; it.increment(error))
	{
		if (it->is_regular_file(error) && it->path().extension() == ".ini")
		{
			names.push_back(it->path().stem().string());
		}
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool LayoutManager::request_user(const std::string& name)
{
	if (!valid_name(name))
	{
		return false;
	}
	using wiwa::LogLevel;
	std::ifstream in(m_Folder / (name + ".ini"), std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	if (!in || text.str().empty())
	{
		WI_LOG(Warning, "Editor", "The layout {0} is missing or empty", name);
		return false;
	}
	m_PendingUserText = text.str();
	m_HasPendingUser = true;
	m_Maximized = nullptr; // the layout replaces whatever was maximized
	m_SavedLayout.clear();
	return true;
}

bool LayoutManager::save_user(const std::string& name)
{
	if (!valid_name(name))
	{
		return false;
	}
	size_t size = 0;
	const char* text = ImGui::SaveIniSettingsToMemory(&size);
	std::ofstream out(m_Folder / (name + ".ini"), std::ios::binary | std::ios::trunc);
	out.write(text, static_cast<std::streamsize>(size));
	return static_cast<bool>(out);
}

bool LayoutManager::delete_user(const std::string& name)
{
	if (!valid_name(name))
	{
		return false;
	}
	std::error_code error;
	return std::filesystem::remove(m_Folder / (name + ".ini"), error);
}

void LayoutManager::apply_pending(ImGuiID dockspace, const std::vector<Ref<Panel>>& panels)
{
	if (m_HasPendingUser)
	{
		m_HasPendingUser = false;
		ImGui::LoadIniSettingsFromMemory(m_PendingUserText.c_str(), m_PendingUserText.size());
		m_PendingUserText.clear();
	}

	if (m_PendingBuiltin.empty())
	{
		return;
	}
	const std::string name = std::move(m_PendingBuiltin);
	m_PendingBuiltin.clear();
	for (const Preset& preset : presets())
	{
		if (name != preset.Name)
		{
			continue;
		}
		m_Maximized = nullptr;
		m_SavedLayout.clear();
		build_preset(preset, dockspace);
		for (const Ref<Panel>& panel : panels)
		{
			if (any(panel->get_category(), PanelCategory::Window | PanelCategory::Info | PanelCategory::Renderer))
			{
				panel->set_open(preset_has(preset, panel->window_name()));
			}
		}
		return;
	}
}

// ---- maximize ----

Panel* LayoutManager::panel_at_cursor(const std::vector<Ref<Panel>>& panels)
{
	const ImGuiContext& context = *ImGui::GetCurrentContext();
	const ImGuiWindow* window = context.HoveredWindow != nullptr ? context.HoveredWindow : context.NavWindow;
	if (window == nullptr)
	{
		return nullptr;
	}
	for (const Ref<Panel>& panel : panels)
	{
		if (!panel->is_active())
		{
			continue;
		}
		const ImGuiWindow* panelWindow = window_of(*panel);
		if (panelWindow != nullptr && (panelWindow == window || panelWindow == window->RootWindow))
		{
			return panel.get();
		}
	}
	return nullptr;
}

void LayoutManager::toggle_maximize(Panel* panel)
{
	if (panel == nullptr)
	{
		return;
	}
	if (m_Maximized == panel)
	{
		restore_maximized(true);
		return;
	}
	if (m_Maximized != nullptr)
	{
		restore_maximized(true);
	}

	ImGuiWindow* window = window_of(*panel);
	if (window == nullptr)
	{
		return;
	}
	m_Maximized = panel;
	size_t size = 0;
	const char* text = ImGui::SaveIniSettingsToMemory(&size);
	m_SavedLayout.assign(text, size);
	ImGui::FocusWindow(window);
}

void LayoutManager::restore_maximized(bool reapply)
{
	if (m_Maximized == nullptr)
	{
		return;
	}
	if (reapply && !m_SavedLayout.empty())
	{
		// the dock nodes the undocked panel left may be gone (a panel alone in its node): the whole layout comes back from the ini text
		m_PendingUserText = m_SavedLayout;
		m_HasPendingUser = true;
	}
	m_Maximized = nullptr;
	m_SavedLayout.clear();
}

void LayoutManager::update_maximize(bool allowEscape)
{
	if (m_Maximized == nullptr)
	{
		return;
	}
	if (!m_Maximized->is_active())
	{
		restore_maximized(true); // closed while maximized: the layout comes back with the panel docked in its place
		return;
	}
	if (allowEscape && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
	{
		restore_maximized(true);
		return;
	}

	if (ImGuiWindow* window = window_of(*m_Maximized))
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
		ImGui::SetWindowPos(window, viewport->WorkPos, ImGuiCond_Always);
		ImGui::SetWindowSize(window, viewport->WorkSize, ImGuiCond_Always);
	}
}
