#pragma once

#include <ui/ui_input.h>

#include "action_types.h"

namespace wiwa
{
	class InputModule;

	// Name of the action map that drives the UI navigation, and its actions. The module registers it and keeps it on the stack below
	// the maps of the game. A project can define a map of this name in its .winput to change the bindings (it replaces the built-in one).
	constexpr const char* UI_NAV_MAP = "UI";

	// NavUp, NavDown, NavLeft, NavRight (arrows, d-pad, left stick), Submit (Enter, pad A), Cancel (Escape, pad B).
	ActionMap ui_nav_action_map();

	// The game UI's input from the input module: the cursor as the pointer, the mouse buttons and the scroll, and the navigation from
	// the "UI" action map (so it can be rebound). With keyboard and mouse capture off the pointer and the keys are silent; the gamepad
	// still navigates.
	class InputUiSource final : public IUiInputSource
	{
	public:
		explicit InputUiSource(InputModule& module) : m_Module(module) {}

		// Where the game image is in the window: the pointer is (cursor - origin) * scale, and only valid while `hovered`.
		// The default (a game that fills its window) is origin 0, scale 1, hovered.
		void set_viewport(const glm::vec2& origin, float scale, bool hovered);

		virtual UiInputFrame poll() override;

	private:
		InputModule& m_Module;
		glm::vec2 m_Origin{ 0.0f };
		float m_Scale = 1.0f;
		bool m_Hovered = true;
	};
}
