#include <editor_common.h>
#include "nav_geometry.h"

#include <algorithm>

#include <scene/components.h>

namespace wiwa
{
	namespace
	{
		// The area a mesh entity is tagged with: its own tag, else the nearest ancestor's.
		u8 area_of(entt::registry& registry, entt::entity entity)
		{
			for (entt::entity current = entity; current != entt::null && registry.valid(current);)
			{
				if (const NavAreaTag* tag = registry.try_get<NavAreaTag>(current))
				{
					return static_cast<u8>(tag->Area);
				}
				const Hierarchy* hierarchy = registry.try_get<Hierarchy>(current);
				current = hierarchy != nullptr ? hierarchy->Parent : entt::null;
			}
			return static_cast<u8>(NavArea::Walkable);
		}

		bool boxes_touch(const glm::vec3& minA, const glm::vec3& maxA, const glm::vec3& minB, const glm::vec3& maxB)
		{
			return minA.x <= maxB.x && maxA.x >= minB.x && minA.y <= maxB.y && maxA.y >= minB.y && minA.z <= maxB.z && maxA.z >= minB.z;
		}
	}

	NavBakeInput gather_nav_input(Scene& scene, entt::entity volume, AssetManager& assets, std::string* error)
	{
		NavBakeInput input;
		entt::registry& registry = scene.registry();
		if (!registry.valid(volume) || !registry.all_of<NavMeshVolume>(volume))
		{
			if (error != nullptr)
			{
				*error = "the entity is not a NavMeshVolume";
			}
			return input;
		}

		scene.update_world_transforms();
		const NavMeshVolume& settings = registry.get<NavMeshVolume>(volume);
		const WorldTransform* volumeWorld = registry.try_get<WorldTransform>(volume);
		const glm::vec3 centre = volumeWorld != nullptr ? glm::vec3(volumeWorld->Matrix[3]) : glm::vec3(0.0f);
		input.BoundsMin = centre - settings.Size * 0.5f;
		input.BoundsMax = centre + settings.Size * 0.5f;
		input.Build = settings.build();

		struct Pending
		{
			entt::entity Entity = entt::null;
			glm::mat4 World{ 1.0f };
			Handle<MeshData> Mesh{};
		};
		std::vector<Pending> pending;
		for (auto [entity, renderer, world] : registry.view<MeshRenderer, WorldTransform>().each())
		{
			if (!renderer.Mesh.valid())
			{
				continue;
			}
			if (const Collider* collider = registry.try_get<Collider>(entity))
			{
				const i32 layer = std::clamp(collider->Layer, 0, 31);
				if (((static_cast<u32>(settings.LayerMask) >> static_cast<u32>(layer)) & 1u) == 0)
				{
					continue;
				}
			}
			pending.push_back({ entity, world.Matrix, assets.load<MeshData>(renderer.Mesh) });
		}
		assets.wait_idle();

		for (const Pending& item : pending)
		{
			const MeshData* mesh = item.Mesh.get();
			if (mesh == nullptr)
			{
				WI_NAV_LOG(Warning, "Navigation: a mesh did not load and is left out of the bake");
				continue;
			}

			// The box of the mesh in the world, from the corners of its local box.
			glm::vec3 worldMin(std::numeric_limits<float>::max());
			glm::vec3 worldMax(std::numeric_limits<float>::lowest());
			for (int corner = 0; corner < 8; corner++)
			{
				const glm::vec3 local((corner & 1) ? mesh->BoundsMax.x : mesh->BoundsMin.x, (corner & 2) ? mesh->BoundsMax.y : mesh->BoundsMin.y, (corner & 4) ? mesh->BoundsMax.z : mesh->BoundsMin.z);
				const glm::vec3 world = glm::vec3(item.World * glm::vec4(local, 1.0f));
				worldMin = glm::min(worldMin, world);
				worldMax = glm::max(worldMax, world);
			}
			if (!boxes_touch(worldMin, worldMax, input.BoundsMin, input.BoundsMax))
			{
				continue;
			}

			const u32 base = static_cast<u32>(input.Vertices.size());
			for (const MeshVertex& vertex : mesh->Vertices)
			{
				input.Vertices.push_back(glm::vec3(item.World * glm::vec4(vertex.Position[0], vertex.Position[1], vertex.Position[2], 1.0f)));
			}

			const bool mirrored = glm::determinant(glm::mat3(item.World)) < 0.0f;
			const u8 area = area_of(registry, item.Entity);
			for (const MeshPrimitive& primitive : mesh->Primitives)
			{
				if (static_cast<size_t>(primitive.FirstIndex) + primitive.IndexCount > mesh->Indices.size())
				{
					continue;
				}
				for (u32 i = 0; i + 2 < primitive.IndexCount; i += 3)
				{
					const i64 a = static_cast<i64>(primitive.BaseVertex) + mesh->Indices[primitive.FirstIndex + i + 0];
					const i64 b = static_cast<i64>(primitive.BaseVertex) + mesh->Indices[primitive.FirstIndex + i + (mirrored ? 2 : 1)];
					const i64 c = static_cast<i64>(primitive.BaseVertex) + mesh->Indices[primitive.FirstIndex + i + (mirrored ? 1 : 2)];
					if (a < 0 || b < 0 || c < 0 || a >= static_cast<i64>(mesh->Vertices.size()) || b >= static_cast<i64>(mesh->Vertices.size()) || c >= static_cast<i64>(mesh->Vertices.size()))
					{
						continue;
					}
					const glm::vec3& va = input.Vertices[base + static_cast<u32>(a)];
					const glm::vec3& vb = input.Vertices[base + static_cast<u32>(b)];
					const glm::vec3& vc = input.Vertices[base + static_cast<u32>(c)];
					if (!boxes_touch(glm::min(va, glm::min(vb, vc)), glm::max(va, glm::max(vb, vc)), input.BoundsMin, input.BoundsMax))
					{
						continue;
					}
					input.Indices.push_back(base + static_cast<u32>(a));
					input.Indices.push_back(base + static_cast<u32>(b));
					input.Indices.push_back(base + static_cast<u32>(c));
					input.TriangleAreas.push_back(area);
				}
			}
		}

		if (error != nullptr && input.Indices.empty())
		{
			*error = "no geometry touches the volume";
		}
		return input;
	}
}
