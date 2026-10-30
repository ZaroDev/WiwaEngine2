#pragma once

#include <vector>

#include "render_queue.h"

struct GLFWwindow;

namespace wiwa
{
	// The main-thread half of ImGui: the context, the GLFW input backend, and handing each finished frame to the render
	// thread as a copy. Any module can build UI between begin_frame() and end_frame() by calling ImGui:: directly; the
	// RenderModule opens the frame before the modules update and closes it after.
	class ImGuiLayer
	{
	public:
		bool init(GLFWwindow* window);
		void shutdown();

		// Starts the ImGui frame (input, display size).
		void begin_frame();
		// Finishes the frame and copies its draw data into the batch.
		void end_frame(RenderBatch& batch);

		// The font atlas as RGBA8, once: true the first time, so the render thread can create its texture.
		bool take_font_atlas(u32& width, u32& height, std::vector<u8>& pixels);

		// Re-sends the atlas to the render thread at the next frame; call after adding fonts and building the atlas.
		void rebuild_fonts();

	private:
		void upload_font_atlas();

		bool m_Initialized = false;
		bool m_FrameOpen = false;
		bool m_FontPending = false;
		bool m_FontDirty = false;
		u32 m_FontWidth = 0;
		u32 m_FontHeight = 0;
		std::vector<u8> m_FontPixels{};
	};
}
