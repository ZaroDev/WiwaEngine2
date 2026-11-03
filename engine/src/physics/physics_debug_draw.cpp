#include <pch.h>
#include "physics_debug_draw.h"

#ifdef JPH_DEBUG_RENDERER

#include <atomic>
#include <vector>

namespace wiwa
{
	namespace
	{
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

		// The triangles of a batch the renderer made; the geometry the shapes draw themselves with holds these.
		class TriangleBatch final : public JPH::RefTargetVirtual
		{
		public:
			void AddRef() override { m_Count++; }
			void Release() override
			{
				if (--m_Count == 0)
				{
					delete this;
				}
			}

			std::vector<JPH::DebugRenderer::Triangle> Triangles{};

		private:
			std::atomic<JPH::uint32> m_Count{ 0 };
		};
	}

	RecordingDebugRenderer::RecordingDebugRenderer()
	{
		Initialize();
	}

	std::shared_ptr<PhysicsDebugDraw> RecordingDebugRenderer::take()
	{
		auto drawn = std::make_shared<PhysicsDebugDraw>(std::move(m_Drawn));
		m_Drawn = PhysicsDebugDraw{};
		return drawn;
	}

	void RecordingDebugRenderer::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color)
	{
		m_Drawn.Lines.push_back(PhysicsDebugLine{ to_glm(from), to_glm(to), color.GetUInt32() });
	}

	void RecordingDebugRenderer::DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg color, ECastShadow)
	{
		m_Drawn.Triangles.push_back(PhysicsDebugTriangle{ to_glm(v1), to_glm(v2), to_glm(v3), color.GetUInt32() });
	}

	JPH::DebugRenderer::Batch RecordingDebugRenderer::CreateTriangleBatch(const Triangle* triangles, int triangleCount)
	{
		TriangleBatch* batch = new TriangleBatch();
		if (triangles != nullptr && triangleCount > 0)
		{
			batch->Triangles.assign(triangles, triangles + triangleCount);
		}
		return batch;
	}

	JPH::DebugRenderer::Batch RecordingDebugRenderer::CreateTriangleBatch(const Vertex* vertices, int vertexCount, const JPH::uint32* indices, int indexCount)
	{
		TriangleBatch* batch = new TriangleBatch();
		if (vertices != nullptr && indices != nullptr)
		{
			for (int i = 0; i + 2 < indexCount; i += 3)
			{
				if (indices[i] < static_cast<JPH::uint32>(vertexCount) && indices[i + 1] < static_cast<JPH::uint32>(vertexCount) && indices[i + 2] < static_cast<JPH::uint32>(vertexCount))
				{
					Triangle triangle;
					triangle.mV[0] = vertices[indices[i]];
					triangle.mV[1] = vertices[indices[i + 1]];
					triangle.mV[2] = vertices[indices[i + 2]];
					batch->Triangles.push_back(triangle);
				}
			}
		}
		return batch;
	}

	void RecordingDebugRenderer::DrawGeometry(JPH::RMat44Arg model, const JPH::AABox&, float, JPH::ColorArg color, const GeometryRef& geometry, ECullMode, ECastShadow, EDrawMode drawMode)
	{
		if (geometry == nullptr || geometry->mLODs.empty())
		{
			return;
		}

		// the coarsest level of detail: this is a debug view of the colliders, not of the models
		const TriangleBatch* batch = static_cast<const TriangleBatch*>(geometry->mLODs.back().mTriangleBatch.GetPtr());
		if (batch == nullptr)
		{
			return;
		}
		for (const Triangle& triangle : batch->Triangles)
		{
			JPH::Vec3 corner[3];
			for (int i = 0; i < 3; i++)
			{
				corner[i] = model * JPH::Vec3(triangle.mV[i].mPosition);
			}
			if (drawMode == EDrawMode::Wireframe)
			{
				for (int i = 0; i < 3; i++)
				{
					DrawLine(corner[i], corner[(i + 1) % 3], color);
				}
			}
			else
			{
				DrawTriangle(corner[0], corner[1], corner[2], color, ECastShadow::Off);
			}
		}
	}
}

#endif
