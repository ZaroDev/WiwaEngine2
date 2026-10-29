#pragma once

#include <math/math.h>

namespace wiwa
{
	// A copy of one frame of ImGui draw data that the render thread can draw while the main thread builds the next frame.
	// Filled in by the ImGui layer (the contents arrive with it); an empty one means "nothing to draw".
	struct OwnedDrawData
	{
		bool Valid = false;
	};
}
