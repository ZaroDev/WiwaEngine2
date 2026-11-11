#include <editor_common.h>
#include "nav_smoke_test.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

#include <nav/nav_mesh_asset.h>
#include <nav/nav_project_settings.h>
#include <scene/components.h>
#include <scene/reflection/component_registry.h>
#include <scene/scene.h>
#include <scene/scene_clone.h>
#include <scene/scene_components.h>
#include "../asset_pipeline/nav_bake.h"
#include "../asset_pipeline/nav_settings_file.h"

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_NAV_LOG(Error, "Nav smoke test FAILED: {0}", what);
				}
			}
		};

		NavMeshData sample_mesh()
		{
			NavMeshData data;
			data.Build.CellSize = 0.5f;
			data.Build.AgentRadius = 0.25f;
			data.InputHash = 0xABCD;
			data.Origin = glm::vec3(-10.0f, 0.0f, -10.0f);
			data.TileWidth = 32.0f;
			data.TileHeight = 32.0f;
			data.MaxTiles = 4;
			data.MaxPolys = 64;
			for (int tile = 0; tile < 2; tile++)
			{
				NavTileBlob blob;
				for (int i = 0; i < 16; i++)
				{
					blob.Data.push_back(static_cast<std::byte>(tile * 16 + i + 1));
				}
				data.Tiles.push_back(std::move(blob));
			}
			return data;
		}

		void wnav_format(Check& check)
		{
			const NavMeshData data = sample_mesh();
			check(data.valid(), "wnav: the sample mesh is valid");

			const std::vector<std::byte> bytes = nav_mesh_to_bytes(data);
			NavMeshData back;
			std::string error;
			const bool ok = nav_mesh_from_bytes(bytes, back, error);
			check(ok, "wnav: a mesh reads back (" + error + ")");
			check(ok && back == data, "wnav: the mesh read back equals the one written (settings, hash, tiles)");
			check(ok && back.InputHash == 0xABCD && back.Build.CellSize == 0.5f, "wnav: the hash and the build settings survive");

			std::vector<std::byte> wrongMagic = bytes;
			wrongMagic[0] = static_cast<std::byte>(~static_cast<unsigned>(wrongMagic[0]));
			NavMeshData rejected;
			error.clear();
			check(!nav_mesh_from_bytes(wrongMagic, rejected, error) && !error.empty(), "wnav: a wrong magic is refused with a reason");

			std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
			error.clear();
			check(!nav_mesh_from_bytes(truncated, rejected, error) && !error.empty(), "wnav: a truncated file is refused with a reason");

			std::vector<std::byte> trailing = bytes;
			trailing.push_back(std::byte{ 0 });
			error.clear();
			check(!nav_mesh_from_bytes(trailing, rejected, error), "wnav: trailing bytes are refused");

			NavMeshData empty = data;
			empty.Tiles.clear();
			error.clear();
			check(!empty.valid() && !nav_mesh_from_bytes(nav_mesh_to_bytes(empty), rejected, error), "wnav: a mesh without tiles is refused");
		}
	}

	namespace
	{
		// Every field of a component takes a value that differs from its default and gives it back.
		void fields_round_trip(Check& check, const char* component)
		{
			const ComponentInfo* info = ComponentRegistry::get().find(component);
			check(info != nullptr, std::string("components: ") + component + " is registered");
			if (info == nullptr)
			{
				return;
			}

			entt::registry registry;
			const entt::entity entity = registry.create();
			info->Add(registry, entity);
			check(info->Has(registry, entity), std::string("components: ") + component + " is added");

			for (size_t i = 0; i < info->Fields.size(); i++)
			{
				const FieldDescriptor& field = info->Fields[i];
				FieldValue value = field.Default;
				switch (field.Type)
				{
				case FieldType::Bool: value = !std::get<bool>(field.Default); break;
				case FieldType::Int: value = std::get<i32>(field.Default) == 5 ? 6 : 5; break;
				case FieldType::Enum: value = std::get<i32>(field.Default) == 2 ? 3 : 2; break;
				case FieldType::Float: value = std::get<float>(field.Default) == 1.5f ? 2.5f : 1.5f; break;
				case FieldType::Vec3: value = glm::vec3(7.0f, 8.0f, 9.0f); break;
				case FieldType::Asset: value = field.Default; break; // an asset reference round trip is the scene checks' business
				default: break;
				}
				const bool set = info->Set(registry, entity, i, value);
				check(set, std::string("components: ") + component + "." + field.Name + " accepts a value");
				check(set && info->Get(registry, entity, i) == value, std::string("components: ") + component + "." + field.Name + " gives the value back");
			}
		}

		void components(Check& check)
		{
			register_scene_components();
			fields_round_trip(check, "NavMeshVolume");
			fields_round_trip(check, "NavAreaTag");
			fields_round_trip(check, "NavAgent");

			// A clone of a scene keeps the components and their values.
			Scene scene;
			const entt::entity entity = scene.create("Agent");
			NavAgent& agent = scene.registry().emplace<NavAgent>(entity);
			agent.Speed = 6.5f;
			agent.AreaMask = 0x0F;
			agent.DriveTransform = false;
			NavMeshVolume& volume = scene.registry().emplace<NavMeshVolume>(entity);
			volume.CellSize = 0.3f;
			volume.TileSize = 128;
			scene.registry().emplace<NavAreaTag>(entity).Area = NavArea::Mud;

			Ref<Scene> copy = clone_scene(scene);
			bool found = false;
			for (auto [id, clonedAgent] : copy->registry().view<NavAgent>().each())
			{
				found = true;
				const NavMeshVolume& clonedVolume = copy->registry().get<NavMeshVolume>(id);
				check(clonedAgent.Speed == 6.5f && clonedAgent.AreaMask == 0x0F && !clonedAgent.DriveTransform, "components: a clone keeps the NavAgent values");
				check(clonedVolume.CellSize == 0.3f && clonedVolume.TileSize == 128 && clonedVolume.build().CellSize == 0.3f, "components: a clone keeps the NavMeshVolume values, build() reads them");
				check(copy->registry().get<NavAreaTag>(id).Area == NavArea::Mud, "components: a clone keeps the NavAreaTag");
			}
			check(found, "components: a clone has the agent entity");
		}

		void settings(Check& check)
		{
			NavSettings costs;
			costs.AreaCost[static_cast<size_t>(NavArea::Mud)] = 3.0f;

			NavSettings back;
			std::string error;
			check(nav_settings_from_text(nav_settings_to_text(costs), back, error) && back == costs, "settings: the area costs round trip through the file text (" + error + ")");

			const std::vector<std::byte> blob = serialize(costs);
			std::optional<NavSettings> read = deserialize<NavSettings>(blob);
			check(read && *read == costs, "settings: the area costs round trip through the pack blob");

			NavSettings negative;
			check(!nav_settings_from_text("{ \"areaCost\": { \"Mud\": \"heavy\" } }", negative, error) && !error.empty(), "settings: a cost that is not a number is refused with a reason");
			check(!nav_settings_from_text("{ \"areaCost\": { \"Lava\": 2 } }", negative, error), "settings: an unknown area is refused");
			check(nav_settings_from_text("{ \"areaCost\": { \"Mud\": -4 } }", negative, error) && negative.AreaCost[static_cast<size_t>(NavArea::Mud)] == 1.0f, "settings: a cost below 1 is raised to 1");

			NavSettings bad = costs;
			bad.AreaCost[2] = 0.5f;
			check(!deserialize<NavSettings>(serialize(bad)).has_value(), "settings: a blob with a cost below 1 is refused");

			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_nav_smoke.wsettings";
			check(save_nav_settings(file, costs, error) && load_nav_settings(file, back, error) && back == costs, "settings: the file saves and loads");
			std::error_code remove;
			std::filesystem::remove(file, remove);
			check(load_nav_settings(file, back, error) && back == NavSettings{}, "settings: a missing file gives the defaults");
		}
	}

	namespace
	{
		// A quad whose normal points away from `outward` is turned around; the triangles are (a, b, c) and (a, c, d).
		void add_quad(NavBakeInput& input, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 outward, u8 area = static_cast<u8>(NavArea::Walkable))
		{
			if (glm::dot(glm::cross(b - a, c - a), outward) < 0.0f)
			{
				std::swap(b, d);
			}
			const u32 base = static_cast<u32>(input.Vertices.size());
			input.Vertices.insert(input.Vertices.end(), { a, b, c, d });
			input.Indices.insert(input.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			input.TriangleAreas.push_back(area);
			input.TriangleAreas.push_back(area);
		}

		void add_box(NavBakeInput& input, glm::vec3 lo, glm::vec3 hi)
		{
			const glm::vec3 p[8] = {
				{ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z },
				{ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z } };
			add_quad(input, p[0], p[1], p[2], p[3], { 0, 0, -1 });
			add_quad(input, p[4], p[5], p[6], p[7], { 0, 0, 1 });
			add_quad(input, p[0], p[3], p[7], p[4], { -1, 0, 0 });
			add_quad(input, p[1], p[2], p[6], p[5], { 1, 0, 0 });
			add_quad(input, p[0], p[1], p[5], p[4], { 0, -1, 0 });
			add_quad(input, p[3], p[2], p[6], p[7], { 0, 1, 0 });
		}

		// A 20 x 20 m floor at y = 0, with a 1 x 2 x 1 m box in the middle when asked, in a box that holds both.
		NavBakeInput floor_fixture(bool withBox, u8 floorArea = static_cast<u8>(NavArea::Walkable))
		{
			NavBakeInput input;
			add_quad(input, { -10, 0, -10 }, { -10, 0, 10 }, { 10, 0, 10 }, { 10, 0, -10 }, { 0, 1, 0 }, floorArea);
			if (withBox)
			{
				add_box(input, { -0.5f, 0.0f, -0.5f }, { 0.5f, 2.0f, 0.5f });
			}
			input.BoundsMin = { -10.0f, -1.0f, -10.0f };
			input.BoundsMax = { 10.0f, 4.0f, 10.0f };
			input.Build.CellSize = 0.25f;
			return input;
		}

		void bake(Check& check)
		{
			const NavBakeInput withBox = floor_fixture(true);
			const NavBakeResult floor = bake_navmesh(withBox);
			check(floor.Ok, "bake_floor: the floor bakes (" + floor.Error + ")");
			check(floor.Ok && floor.PolyCount > 0 && floor.VertCount > 0 && floor.TileCount >= 1 && floor.Mesh.valid(), "bake_floor: the mesh has polygons, vertices and tiles");
			check(floor.Ok && floor.Mesh.InputHash == nav_input_hash(withBox) && floor.Mesh.Build == withBox.Build, "bake_floor: the mesh records the input hash and the settings");
			check(floor.Ok && floor.TileCount > 1, "bake_floor: a 20 m floor at 0.25 m cells and 64 cell tiles needs several tiles");

			const NavBakeResult open = bake_navmesh(floor_fixture(false));
			check(open.Ok && floor.Ok && open.PolyCount != floor.PolyCount, "bake_box_hole: the box changes the polygons (it leaves a hole in the floor)");

			const NavBakeResult again = bake_navmesh(withBox);
			check(again.Ok && floor.Ok && nav_mesh_to_bytes(again.Mesh) == nav_mesh_to_bytes(floor.Mesh), "bake_deterministic: the same input gives the same bytes");

			NavBakeInput moved = withBox;
			moved.Vertices[0].x += 0.01f;
			check(nav_input_hash(moved) != nav_input_hash(withBox) && nav_input_hash(withBox) == nav_input_hash(floor_fixture(true)), "bake_hash: the hash follows the input");
			NavBakeInput otherSettings = withBox;
			otherSettings.Build.AgentRadius = 0.5f;
			check(nav_input_hash(otherSettings) != nav_input_hash(withBox), "bake_hash: the hash follows the settings");

			const NavBakeResult mud = bake_navmesh(floor_fixture(true, static_cast<u8>(NavArea::Mud)));
			check(mud.Ok && mud.AreaPolyCount[static_cast<size_t>(NavArea::Mud)] > 0 && mud.AreaPolyCount[static_cast<size_t>(NavArea::Mud)] == mud.PolyCount, "bake_area: a floor tagged Mud gives Mud polygons only");

			const NavBakeResult blocked = bake_navmesh(floor_fixture(false, static_cast<u8>(NavArea::NotWalkable)));
			check(!blocked.Ok && !blocked.Error.empty(), "bake_nothing_walkable: a floor tagged NotWalkable fails with a reason");

			NavBakeInput empty = floor_fixture(false);
			empty.Vertices.clear();
			empty.Indices.clear();
			empty.TriangleAreas.clear();
			const NavBakeResult none = bake_navmesh(empty);
			check(!none.Ok && !none.Error.empty(), "bake_empty_fails: no triangles fails with a reason");

			NavBakeInput badIndex = floor_fixture(false);
			badIndex.Indices[0] = 999;
			check(!bake_navmesh(badIndex).Ok, "bake_invalid: an index out of range fails");
			NavBakeInput badVertex = floor_fixture(false);
			badVertex.Vertices[1].y = std::numeric_limits<float>::quiet_NaN();
			check(!bake_navmesh(badVertex).Ok, "bake_invalid: a vertex that is not a number fails");
			NavBakeInput badBox = floor_fixture(false);
			badBox.BoundsMax = badBox.BoundsMin;
			check(!bake_navmesh(badBox).Ok, "bake_invalid: an empty volume fails");
			NavBakeInput hugeGrid = floor_fixture(false);
			hugeGrid.BoundsMin = { -5000.0f, -1.0f, -5000.0f };
			hugeGrid.BoundsMax = { 5000.0f, 4.0f, 5000.0f };
			check(!bake_navmesh(hugeGrid).Ok, "bake_invalid: a volume of too many tiles fails");

			std::atomic<bool> cancel{ true };
			const NavBakeResult stopped = bake_navmesh(withBox, &cancel);
			check(!stopped.Ok && stopped.Error == "cancelled", "bake_cancel: a cancelled bake fails with 'cancelled'");

			// A written file reads back, and a mesh that is not valid is not written.
			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_nav_smoke.wnav";
			std::string error;
			check(floor.Ok && write_nav_file(file, floor.Mesh, error), "bake_write: the .wnav is written (" + error + ")");
			std::ifstream stream(file, std::ios::binary);
			const std::vector<char> text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			stream.close();
			NavMeshData back;
			const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
			check(nav_mesh_from_bytes(bytes, back, error) && back == floor.Mesh, "bake_write: the file reads back as the baked mesh");
			std::error_code remove;
			std::filesystem::remove(file, remove);
			check(!write_nav_file(file, NavMeshData{}, error) && !std::filesystem::exists(file, remove), "bake_write: an invalid mesh is not written");
		}
	}

	bool run_nav_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		wnav_format(check);
		components(check);
		settings(check);
		bake(check);

		if (check.Failures == 0)
		{
			WI_NAV_LOG(Info, "Nav smoke test passed");
		}
		return check.Failures == 0;
	}
}
