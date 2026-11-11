#include <editor_common.h>
#include "nav_geometry.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

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

		// A mesh of an agent (the entity has a NavAgent, or an ancestor does) is not ground: it walks on it.
		bool belongs_to_agent(entt::registry& registry, entt::entity entity)
		{
			for (entt::entity current = entity; current != entt::null && registry.valid(current);)
			{
				if (registry.all_of<NavAgent>(current))
				{
					return true;
				}
				const Hierarchy* hierarchy = registry.try_get<Hierarchy>(current);
				current = hierarchy != nullptr ? hierarchy->Parent : entt::null;
			}
			return false;
		}

		bool boxes_touch(const glm::vec3& minA, const glm::vec3& maxA, const glm::vec3& minB, const glm::vec3& maxB)
		{
			return minA.x <= maxB.x && maxA.x >= minB.x && minA.y <= maxB.y && maxA.y >= minB.y && minA.z <= maxB.z && maxA.z >= minB.z;
		}
	}

	const MeshData* AssetNavMeshSource::get(const AssetRef& ref, const std::atomic<bool>* cancel)
	{
		auto it = m_Handles.find(ref.resolved());
		if (it == m_Handles.end())
		{
			it = m_Handles.emplace(ref.resolved(), m_Assets.load<MeshData>(ref)).first;
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		while (it->second.state() == LoadState::Loading)
		{
			if ((cancel != nullptr && cancel->load()) || std::chrono::steady_clock::now() > deadline)
			{
				return nullptr;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return it->second.get();
	}

	NavSnapshot capture_nav_snapshot(Scene& scene, entt::entity volume, std::string* error)
	{
		NavSnapshot snapshot;
		entt::registry& registry = scene.registry();
		if (!registry.valid(volume) || !registry.all_of<NavMeshVolume>(volume))
		{
			if (error != nullptr)
			{
				*error = "the entity is not a NavMeshVolume";
			}
			return snapshot;
		}

		scene.update_world_transforms();
		const NavMeshVolume& settings = registry.get<NavMeshVolume>(volume);
		const WorldTransform* volumeWorld = registry.try_get<WorldTransform>(volume);
		const glm::vec3 centre = volumeWorld != nullptr ? glm::vec3(volumeWorld->Matrix[3]) : glm::vec3(0.0f);
		snapshot.BoundsMin = centre - settings.Size * 0.5f;
		snapshot.BoundsMax = centre + settings.Size * 0.5f;
		snapshot.Build = settings.build();

		for (auto [entity, renderer, world] : registry.view<MeshRenderer, WorldTransform>().each())
		{
			if (!renderer.Mesh.valid() || belongs_to_agent(registry, entity))
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
			snapshot.Items.push_back({ renderer.Mesh, world.Matrix, area_of(registry, entity) });
		}
		return snapshot;
	}

	NavBakeInput build_nav_input(const NavSnapshot& snapshot, INavMeshSource& meshes, const std::atomic<bool>* cancel, std::string* error)
	{
		NavBakeInput input;
		input.BoundsMin = snapshot.BoundsMin;
		input.BoundsMax = snapshot.BoundsMax;
		input.Build = snapshot.Build;

		for (const NavSnapshotItem& item : snapshot.Items)
		{
			if (cancel != nullptr && cancel->load())
			{
				break;
			}
			const MeshData* mesh = meshes.get(item.Mesh, cancel);
			if (mesh == nullptr)
			{
				if (cancel == nullptr || !cancel->load())
				{
					WI_NAV_LOG(Warning, "Navigation: a mesh did not load and is left out of the bake");
				}
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
					input.TriangleAreas.push_back(item.Area);
				}
			}
		}

		if (error != nullptr && input.Indices.empty())
		{
			*error = "no geometry touches the volume";
		}
		return input;
	}

	NavBakeInput gather_nav_input(Scene& scene, entt::entity volume, AssetManager& assets, std::string* error)
	{
		const NavSnapshot snapshot = capture_nav_snapshot(scene, volume, error);
		AssetNavMeshSource source(assets);
		return build_nav_input(snapshot, source, nullptr, error);
	}
}
