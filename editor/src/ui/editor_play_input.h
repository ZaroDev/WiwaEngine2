#pragma once

#include <ui/ui_input.h>

namespace wiwa
{
	// The mouse and keys as ImGui saw them in one frame, in the window's coordinates.
	struct UiPointerSample
	{
		glm::vec2 Pointer{ 0.0f };
		bool OverImage = false; // the pointer is over the Viewport image (and not over a widget drawn on it)
		bool Down[UI_POINTER_BUTTONS]{};
		bool Pressed[UI_POINTER_BUTTONS]{};
		bool Released[UI_POINTER_BUTTONS]{};
		float Scroll = 0.0f;
		u32 NavPressed = 0;
	};

	// Feeds the game UI from the editor's mouse and keyboard in play mode, until the game has an input system. The Viewport panel tells it
	// where the image is every frame. Only a press that starts on the image counts, and a drag that started there goes on (and ends) off it.
	class EditorPlayInputSource final : public IUiInputSource
	{
	public:
		// imageMin: the top left of the image in window points; scale: pixels of the image per point; hovered: the pointer is over it.
		void set_viewport(const glm::vec2& imageMin, float scale, bool hovered);

		// Reads ImGui's mouse and keys of this frame.
		virtual UiInputFrame poll() override;

		// The same without ImGui.
		UiInputFrame convert(const UiPointerSample& sample);

	private:
		glm::vec2 m_ImageMin{ 0.0f };
		float m_Scale = 1.0f;
		bool m_Hovered = false;
		bool m_Held[UI_POINTER_BUTTONS]{}; // the button went down on the image and has not come up
	};
}
