#pragma once

#include <vector>

#include <imgui.h>

#include <math/math.h>

namespace wiwa
{
	// One draw call of the UI. The offsets are absolute: into the vertex and index arrays of the whole frame.
	struct OwnedDrawCommand
	{
		float ClipRect[4]{}; // x, y, z, w in ImGui's own coordinates
		u32 ElementCount = 0;
		u32 IndexOffset = 0;
		u32 VertexOffset = 0;
		u32 TextureId = 0; // a TextureRegistry id
	};

	// A copy of one frame of ImGui draw data that the render thread can draw while the main thread builds the next frame.
	// An invalid one means "nothing to draw".
	struct OwnedDrawData
	{
		bool Valid = false;
		float DisplayPos[2]{};
		float DisplaySize[2]{};
		float FramebufferScale[2]{ 1.0f, 1.0f };
		std::vector<ImDrawVert> Vertices{};
		std::vector<ImDrawIdx> Indices{};
		std::vector<OwnedDrawCommand> Commands{};
	};

	// Copies the draw lists of an ImGui frame (main thread, right after ImGui::Render) into one set of arrays.
	void copy_draw_data(const ImDrawData* source, OwnedDrawData& target);
}
