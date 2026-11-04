#pragma once

#include "raw_input_state.h"

namespace wiwa
{
	enum class CursorMode : u8 { Normal, Hidden, Locked };

	// Source of raw input. begin_frame clears the edges and deltas in `out`, then applies every event received since the last call.
	class IInputBackend
	{
	public:
		virtual ~IInputBackend() = default;

		virtual void begin_frame(RawInputState& out) = 0;
		virtual void set_cursor_mode(CursorMode) {}
	};
}
