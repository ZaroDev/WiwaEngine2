#include <pch.h>
#include "imgui_layer.h"

#include "platform/imgui/imgui_impl_glfw.h"
#include "texture_registry.h"

#define WI_IMGUI_LOG(level, ...) WI_LOG(level, "ImGui", __VA_ARGS__)

namespace wiwa
{
	void copy_draw_data(const ImDrawData* source, OwnedDrawData& target)
	{
		target.Valid = false;
		target.Vertices.clear();
		target.Indices.clear();
		target.Commands.clear();

		if (source == nullptr || source->CmdListsCount == 0)
		{
			return;
		}

		target.DisplayPos[0] = source->DisplayPos.x;
		target.DisplayPos[1] = source->DisplayPos.y;
		target.DisplaySize[0] = source->DisplaySize.x;
		target.DisplaySize[1] = source->DisplaySize.y;
		target.FramebufferScale[0] = source->FramebufferScale.x;
		target.FramebufferScale[1] = source->FramebufferScale.y;

		target.Vertices.reserve(static_cast<size_t>(source->TotalVtxCount));
		target.Indices.reserve(static_cast<size_t>(source->TotalIdxCount));

		for (const ImDrawList* list : source->CmdLists)
		{
			const u32 vertexBase = static_cast<u32>(target.Vertices.size());
			const u32 indexBase = static_cast<u32>(target.Indices.size());

			target.Vertices.insert(target.Vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
			target.Indices.insert(target.Indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());

			for (const ImDrawCmd& command : list->CmdBuffer)
			{
				if (command.UserCallback != nullptr)
				{
					continue; // custom callbacks (and the render-state reset) are not supported
				}

				OwnedDrawCommand copy;
				copy.ClipRect[0] = command.ClipRect.x;
				copy.ClipRect[1] = command.ClipRect.y;
				copy.ClipRect[2] = command.ClipRect.z;
				copy.ClipRect[3] = command.ClipRect.w;
				copy.ElementCount = command.ElemCount;
				copy.IndexOffset = indexBase + command.IdxOffset;
				copy.VertexOffset = vertexBase + command.VtxOffset;
				copy.TextureId = static_cast<u32>(command.GetTexID());
				target.Commands.push_back(copy);
			}
		}

		target.Valid = !target.Commands.empty();
	}

	bool ImGuiLayer::init(GLFWwindow* window)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendRendererName = "wiwa_nvrhi";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // the render thread honors ImDrawCmd::VtxOffset
		ImGui::StyleColorsDark();

		// "Other": the window was created with GLFW_NO_API, so the backend only handles input.
		if (!ImGui_ImplGlfw_InitForOther(window, true))
		{
			WI_IMGUI_LOG(Error, "The GLFW backend could not be initialized");
			ImGui::DestroyContext();
			return false;
		}

		// Build the atlas now so the render thread can get its pixels; the fonts must be added before this point.
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		m_FontWidth = static_cast<u32>(width);
		m_FontHeight = static_cast<u32>(height);
		m_FontPixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
		io.Fonts->SetTexID(static_cast<ImTextureID>(FONT_TEXTURE_ID));
		m_FontPending = true;

		m_Initialized = true;
		return true;
	}

	void ImGuiLayer::shutdown()
	{
		if (!m_Initialized)
		{
			return;
		}

		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		m_Initialized = false;
		m_FrameOpen = false;
	}

	void ImGuiLayer::begin_frame()
	{
		if (!m_Initialized || m_FrameOpen)
		{
			return;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		m_FrameOpen = true;
	}

	void ImGuiLayer::end_frame(RenderBatch& batch)
	{
		if (!m_Initialized || !m_FrameOpen)
		{
			return;
		}

		ImGui::Render();
		copy_draw_data(ImGui::GetDrawData(), batch.ImGui);
		m_FrameOpen = false;
	}

	bool ImGuiLayer::take_font_atlas(u32& width, u32& height, std::vector<u8>& pixels)
	{
		if (!m_FontPending)
		{
			return false;
		}

		width = m_FontWidth;
		height = m_FontHeight;
		pixels = std::move(m_FontPixels);
		m_FontPixels.clear();
		m_FontPending = false;
		return true;
	}
}
