#pragma once

#include <math/math.h>

namespace wiwa
{
	// What the project (not a scene) decides about how the renderer works: shadow reach and biases, vsync, uploads.
	struct RenderQuality
	{
		float ShadowDistance = 60.0f;     // the sun's cascades reach this far in front of the camera
		u32 ShadowCascades = 4;           // 1..4
		float ShadowSplitLambda = 0.8f;   // 0 = even splits, 1 = logarithmic
		float ShadowDepthBias = 0.0006f;  // in depth units of the shadow map, scaled up on surfaces turned away from the light
		float ShadowNormalBias = 1.5f;    // texels: receivers are moved along their normal by this much before they are looked up
		float SpotShadowBias = 0.0004f;
		bool Vsync = true;
		bool CopyQueue = true;

		bool operator==(const RenderQuality&) const = default;
	};
}
