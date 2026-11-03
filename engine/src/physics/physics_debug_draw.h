#pragma once

#include "jolt_config.h"

// Compiled out in Shipping, like Jolt's debug renderer itself (JPH_DEBUG_RENDERER).
#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Renderer/DebugRenderer.h>

#include <memory>

#include "physics_snapshot.h"

namespace wiwa
{
	// A Jolt DebugRenderer that records what it is asked to draw instead of drawing it: the lines and triangles go into the step's
	// snapshot, and the main thread hands them to the engine's debug renderer. Physics thread only.
	class RecordingDebugRenderer final : public JPH::DebugRenderer
	{
	public:
		RecordingDebugRenderer();

		// What was drawn since the last call.
		std::shared_ptr<PhysicsDebugDraw> take();

		void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;
		void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg color, ECastShadow castShadow) override;
		Batch CreateTriangleBatch(const Triangle* triangles, int triangleCount) override;
		Batch CreateTriangleBatch(const Vertex* vertices, int vertexCount, const JPH::uint32* indices, int indexCount) override;
		void DrawGeometry(JPH::RMat44Arg model, const JPH::AABox& worldBounds, float lodScaleSq, JPH::ColorArg color, const GeometryRef& geometry, ECullMode cullMode,
			ECastShadow castShadow, EDrawMode drawMode) override;
		void DrawText3D(JPH::RVec3Arg, const JPH::string_view&, JPH::ColorArg, float) override {}

	private:
		PhysicsDebugDraw m_Drawn{};
	};
}

#endif
