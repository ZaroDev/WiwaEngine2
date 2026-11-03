#pragma once

#include <math/math.h>

// What the UI needs from an input system, and nothing more. The game and the editor's play mode use the InputModule's InputUiSource
// (input/input_ui_source.h); tests and scripts have the scripted source, and a UI without any input has the null one.
namespace wiwa
{
	enum class UiNav : u32
	{
		Up,
		Down,
		Left,
		Right,
		Submit,
		Cancel,
	};

	constexpr u32 ui_nav_bit(UiNav nav) { return 1u << static_cast<u32>(nav); }

	constexpr u32 UI_POINTER_BUTTONS = 3; // 0 is the main button, then the secondary one, then the middle one

	// The input of one frame. Down is a state; Pressed and Released are edges that are true for the one frame in which they happened (a
	// quick click can have both in the same frame).
	struct UiInputFrame
	{
		glm::vec2 Pointer{ -1.0f };   // in canvas pixels
		bool PointerValid = false;    // false when there is no pointer, or it is outside the canvas
		bool Down[UI_POINTER_BUTTONS]{};
		bool Pressed[UI_POINTER_BUTTONS]{};
		bool Released[UI_POINTER_BUTTONS]{};
		float Scroll = 0.0f;
		u32 NavPressed = 0;           // bits of ui_nav_bit
	};

	class IUiInputSource
	{
	public:
		virtual ~IUiInputSource() = default;

		// Main thread, once per frame: the input since the last call.
		virtual UiInputFrame poll() = 0;
	};

	// No pointer, no keys.
	class NullInputSource final : public IUiInputSource
	{
	public:
		virtual UiInputFrame poll() override { return UiInputFrame{}; }
	};

	// Input made up by code: tests, smoke checks and Lua. The calls change the state that the next poll() reports.
	class ScriptedInputSource final : public IUiInputSource
	{
	public:
		void move_to(const glm::vec2& pointer);
		void clear_pointer();             // the pointer leaves the canvas
		void press(int button);           // an unknown button is ignored
		void release(int button);
		void nav(UiNav nav);
		void scroll(float amount);

		virtual UiInputFrame poll() override;

	private:
		UiInputFrame m_State{};
	};
}
