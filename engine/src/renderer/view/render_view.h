#pragma once

#include <functional>
#include <memory>

#include <core/common.h>

#include "../scene/render_scene.h"
#include "view_types.h"

namespace wiwa
{
	class RenderModule;
	class Scene;

	// A handle to an offscreen view of a scene (views.md): a scene of its own, a camera, and a texture the render thread keeps up to date, on
	// every frame or when asked. Made by RenderModule::create_view; destroying the handle destroys the view. Main thread only.
	class RenderView
	{
	public:
		RenderView() = default;
		~RenderView();
		RenderView(RenderView&& other) noexcept;
		RenderView& operator=(RenderView&& other) noexcept;
		RenderView(const RenderView&) = delete;
		RenderView& operator=(const RenderView&) = delete;

		// False for a handle that was never made (the view limit was reached) or that was moved from.
		bool valid() const { return m_Id != INVALID_VIEW; }
		ViewId id() const { return m_Id; }

		// What the view shows. Only a weak reference is kept: whoever made the scene keeps it alive. A scene that is gone leaves the view with
		// its last image.
		void set_scene(const Ref<Scene>& scene);
		// Where the view looks from. Without one the scene's primary Camera component is used. The projection's aspect is the caller's business:
		// set it again after a resize().
		void set_camera(const RenderCamera& camera);
		// The size in pixels (at least 1; a profile may cap it, keeping the aspect). 0 in either dimension changes nothing.
		void resize(u32 width, u32 height);
		// OneShot views: draws the scene once (a few frames later when many views are waiting). Continuous views ignore it.
		void request_render();

		// The view could not make its target; it shows magenta until resized.
		bool failed() const;
		// The id to draw the view with in ImGui (ImGui::Image) or the game UI; 0 for an invalid handle.
		u32 imgui_texture_id() const { return valid() ? view_texture_id(m_Id) : 0; }
		// Reads the image back to the CPU: done runs on the main thread a few frames later with RGBA8 pixels (straight alpha unless premultiplied),
		// and never runs when the view is destroyed first. An empty ImageData reports a failure.
		void read_back(std::function<void(ImageData)> done, bool premultiplied = false);

	private:
		friend class RenderModule;
		RenderView(RenderModule* module, std::weak_ptr<bool> alive, ViewId id) : m_Module(module), m_Alive(std::move(alive)), m_Id(id) {}
		RenderModule* live_module() const;
		void reset();

		RenderModule* m_Module = nullptr;
		std::weak_ptr<bool> m_Alive{}; // expires when the module shuts down: the handle then does nothing
		ViewId m_Id = INVALID_VIEW;
	};
}
