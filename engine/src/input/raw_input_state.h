#pragma once

#include "input_codes.h"

#include <bitset>

namespace wiwa
{
	constexpr u32 MAX_GAMEPADS = 4;

	struct PadState
	{
		bool Connected = false;
		std::bitset<16> Down, Pressed, Released;
		// Sticks in [-1, 1] with up = +1, triggers in [0, 1]; indexed by PadAxis.
		float Axes[PAD_AXIS_COUNT] = {};
	};

	// Plain data filled once per frame by an IInputBackend. Edges (Pressed / Released) cover everything that happened since the
	// previous frame, so a press and release inside one frame gives both edges and leaves Down false.
	struct RawInputState
	{
		std::bitset<KEY_COUNT> KeyDown, KeyPressed, KeyReleased;
		std::bitset<8> MouseDown, MousePressed, MouseReleased;
		glm::vec2 Cursor{ 0.0f };      // window pixels
		glm::vec2 CursorDelta{ 0.0f };
		glm::vec2 Scroll{ 0.0f };
		PadState Pads[MAX_GAMEPADS];
	};
}
