#pragma once

#include <nvrhi/nvrhi.h>

#include "../scene/render_scene.h"
#include "../scene/scene_render_data.h"
#include "../viewport_target.h"
#include "view_types.h"

namespace wiwa
{
	// Everything the render thread keeps for one view of a scene: the mirror of the scene, what the passes share for a frame, the target the
	// scene is drawn into, and the temporal state (kept in SceneRenderData: previous camera, history). The main view (the Viewport panel or
	// the window) is a ViewState like any other; the others are made for RenderViews. Render thread only.
	struct ViewState
	{
		ViewId Id = MAIN_VIEW;
		ViewDesc Desc{};
		ViewFlags Flags{};

		RenderScene Scene{};
		SceneRenderData Data{};
		ViewportTarget Target{};

		bool Failed = false;           // the target could not be made at the requested size; not tried again until the size changes
		bool Active = true;            // rendered this frame (a one-shot view is active until it has rendered once after a request)
		bool RenderRequested = false;  // a one-shot view was asked to render
		bool EverRendered = false;
		bool WarnedTaaAlpha = false;   // the TAA with transparent background warning was logged
		u64 LastRenderedFrame = 0;
		u32 RequestedWidth = 0;
		u32 RequestedHeight = 0;
		u32 TextureId = 0;             // TextureRegistry id of Target.ldr() (the main view uses VIEWPORT_TEXTURE_ID)

		bool is_main() const { return Id == MAIN_VIEW; }
	};
}
