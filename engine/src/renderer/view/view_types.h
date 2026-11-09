#pragma once

#include <optional>
#include <vector>

#include <math/math.h>

// The vocabulary of offscreen views (views.md): what a view is asked to be, and the flags a profile expands to.
namespace wiwa
{
	using ViewId = u32;
	constexpr ViewId MAIN_VIEW = 0;           // the Viewport panel, or the window in a game
	constexpr ViewId INVALID_VIEW = ~0u;
	constexpr u32 MAX_VIEWS = 16;             // live views, the main one included
	constexpr u32 MAX_VIEWS_PER_FRAME = 4;    // non-main views rendered in one frame; the rest wait for a later one

	// The id a view's image has in the TextureRegistry (what ImGui and the game UI draw it by). Chosen from the view id, so the main thread knows
	// it without asking the render thread. Asset textures start at 1 << 20.
	constexpr u32 FIRST_VIEW_TEXTURE_ID = 1u << 22;
	constexpr u32 view_texture_id(ViewId id) { return FIRST_VIEW_TEXTURE_ID + id; }

	enum class ViewProfile : u32 { Full, Preview, Thumbnail };
	enum class ViewMode : u32 { Continuous, OneShot };

	// What a profile switches on. The main view is always Full: "per settings" means the project's quality and the scene's settings decide.
	struct ViewFlags
	{
		u32 ShadowCascades = 4;       // 0 = no directional shadows
		bool SpotShadows = true;
		bool VelocityPrepass = true;  // false: the prepass writes depth only
		bool LightCulling = true;     // false: the forward shader tests every light
		bool Ao = true;
		bool Taa = true;
		bool MotionBlur = true;
		bool Bloom = true;
		bool AutoExposure = true;
		bool Fxaa = true;
		u32 MaxDimension = 0;         // 0 = no cap
	};

	inline ViewFlags view_flags_for(ViewProfile profile)
	{
		ViewFlags flags;
		switch (profile)
		{
		case ViewProfile::Full:
			break;
		case ViewProfile::Preview:
			flags.ShadowCascades = 2;
			flags.SpotShadows = false;
			flags.Ao = false;
			flags.Taa = false;
			flags.MotionBlur = false;
			flags.AutoExposure = false;
			break;
		case ViewProfile::Thumbnail:
			flags.ShadowCascades = 0;
			flags.SpotShadows = false;
			flags.VelocityPrepass = false;
			flags.LightCulling = false;
			flags.Ao = false;
			flags.Taa = false;
			flags.MotionBlur = false;
			flags.Bloom = false;
			flags.AutoExposure = false;
			flags.Fxaa = false;
			flags.MaxDimension = 512;
			break;
		}
		return flags;
	}

	struct ViewDesc
	{
		u32 Width = 256;
		u32 Height = 256;
		ViewProfile Profile = ViewProfile::Preview;
		std::optional<ViewFlags> FlagOverride{}; // starts from the profile's flags; set to change any of them
		bool TransparentBackground = false;      // alpha 0 where nothing is drawn
		ViewMode Mode = ViewMode::Continuous;

		ViewFlags flags() const { return FlagOverride ? *FlagOverride : view_flags_for(Profile); }
	};

	// The pixels of a view read back to the CPU: RGBA8, row 0 on top.
	struct ImageData
	{
		u32 Width = 0;
		u32 Height = 0;
		bool Premultiplied = false;
		std::vector<u8> Rgba{};
	};
}
