#include <pch.h>
#include "input_module.h"
#include <core/file_io.h>

#include <fstream>
#include <sstream>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/window.h>

#include "glfw_input_backend.h"
#include "input_overrides.h"
#include "input_project_settings.h"

#define WI_INPUT_LOG(level, ...) WI_LOG(level, "Input", __VA_ARGS__)

namespace wiwa
{
	void InputModule::init()
	{
		// The window module is added before this one. Without a window (nothing to listen to) the module still works with set_backend.
		Window* window = Engine::get().get_module<Window>();
		if (window && window->get_window())
		{
			m_Glfw = CreateScope<GlfwInputBackend>(window->get_window());
		}

		// A game: the default map the editor packed with it (the editor has none in its Library and sets it itself).
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets && assets->get_source())
		{
			const std::optional<InputProjectSettings> settings = read_input_settings(*assets->get_source());
			if (settings && settings->DefaultMap.valid())
			{
				set_default_map(settings->DefaultMap);
			}
		}

		add_ui_nav_map();
		m_OverridesPending = true;
	}

	void InputModule::shutdown()
	{
		m_DefaultMap = {};
		m_External = nullptr;
		m_Glfw.reset();
	}

	void InputModule::update(float deltaTime)
	{
		if (IInputBackend* source = backend())
		{
			m_State.update(*source);
		}

		apply_pending();
		m_Actions.evaluate(m_State);
	}

	void InputModule::apply_pending()
	{
		if (m_DefaultPending)
		{
			switch (m_DefaultMap.state())
			{
			case LoadState::Loading:
				return;
			case LoadState::Failed:
				WI_INPUT_LOG(Warning, "The default input map could not be loaded, keeping the maps there are");
				m_DefaultPending = false;
				break;
			case LoadState::Ready:
			{
				std::vector<ActionMap> maps = m_DefaultMap.get()->Maps;
				for (const ActionMap& registered : m_Registered)
				{
					const bool taken = std::any_of(maps.begin(), maps.end(), [&](const ActionMap& map) { return map.Name == registered.Name; });
					if (!taken) maps.push_back(registered);
				}
				m_Actions.set_maps(std::move(maps));
				m_DefaultPending = false;
				// set_maps went back to the defaults: the rebinds go on top again.
				m_OverridesPending = true;
				break;
			}
			}
		}

		if (m_OverridesPending)
		{
			m_OverridesPending = false;
			load_overrides();
		}
	}

	void InputModule::set_backend(IInputBackend* backend)
	{
		m_External = backend;
		if (IInputBackend* source = this->backend())
		{
			source->set_cursor_mode(m_CursorMode);
		}
	}

	void InputModule::set_cursor_mode(CursorMode mode)
	{
		m_CursorMode = mode;
		if (IInputBackend* source = backend())
		{
			source->set_cursor_mode(mode);
		}
	}

	void InputModule::add_ui_nav_map()
	{
		add_maps({ ui_nav_action_map() });
		m_Actions.push_map(UI_NAV_MAP);
	}

	void InputModule::add_maps(std::vector<ActionMap> maps)
	{
		m_Registered.insert(m_Registered.end(), maps.begin(), maps.end());
		m_Actions.add_maps(std::move(maps));
	}

	void InputModule::set_default_map(const AssetRef& ref)
	{
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (!assets || !ref.valid())
		{
			return;
		}
		m_DefaultMap = assets->load<InputMapData>(ref);
		m_DefaultPending = true;
	}

	bool InputModule::save_overrides()
	{
		const std::string text = overrides_to_json(m_Actions).dump(1, '\t') + "\n";

		std::string error;
		if (!write_file_atomic(m_OverridesPath, text.data(), text.size(), error))
		{
			WI_INPUT_LOG(Error, "{0}", error);
			return false;
		}
		return true;
	}

	bool InputModule::load_overrides()
	{
		std::error_code error;
		if (!std::filesystem::exists(m_OverridesPath, error))
		{
			return false;
		}

		std::ifstream file(m_OverridesPath, std::ios::binary);
		if (!file)
		{
			WI_INPUT_LOG(Warning, "Cannot read {0}", m_OverridesPath.string());
			return false;
		}
		std::stringstream text;
		text << file.rdbuf();

		const nlohmann::json json = nlohmann::json::parse(text.str(), nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			WI_INPUT_LOG(Warning, "{0} is not a usable input overrides file, using the default bindings", m_OverridesPath.string());
			return false;
		}

		std::string warnings;
		apply_overrides(m_Actions, json, warnings);
		if (!warnings.empty())
		{
			WI_INPUT_LOG(Warning, "{0}: {1}", m_OverridesPath.string(), warnings);
		}
		return true;
	}
}
