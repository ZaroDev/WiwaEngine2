#include <editor_common.h>
#include "nav_smoke_test.h"

#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

#include <nav/nav_data.h>
#include <nav/nav_mesh_asset.h>
#include <nav/nav_module.h>
#include <nav/nav_query.h>
#include <nav/nav_view.h>
#include <scripting/script_component.h>
#include <scripting/script_instances.h>
#include <scripting/script_runtime.h>
#include <scripting/script_source.h>
#include <nav/nav_project_settings.h>
#include <scene/components.h>
#include <scene/reflection/component_registry.h>
#include <scene/scene.h>
#include <scene/scene_clone.h>
#include <scene/scene_components.h>
#include "../asset_pipeline/lua_stubs.h"
#include "../asset_pipeline/nav_bake.h"
#include "../asset_pipeline/nav_geometry.h"
#include "nav_edit_model.h"
#include "nav_overlay.h"
#include "nav_path_tool.h"
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

	namespace
	{
		// A floor of 20 x 20 m with a wall (1 m thick, 2 m high) across the middle, half a length of halfZ either side of z = 0.
		NavBakeInput wall_fixture(float halfZ)
		{
			NavBakeInput input = floor_fixture(false);
			add_box(input, { -0.5f, 0.0f, -halfZ }, { 0.5f, 2.0f, halfZ });
			return input;
		}

		// A flat quad on the floor plane, facing up.
		void add_floor(NavBakeInput& input, float x0, float x1, float z0, float z1, NavArea area)
		{
			add_quad(input, { x0, 0, z0 }, { x0, 0, z1 }, { x1, 0, z1 }, { x1, 0, z0 }, { 0, 1, 0 }, static_cast<u8>(area));
		}

		Scope<NavData> data_of(Check& check, const NavBakeInput& input, const char* what)
		{
			const NavBakeResult result = bake_navmesh(input);
			check(result.Ok, std::string(what) + ": the fixture bakes (" + result.Error + ")");
			std::string error;
			Scope<NavData> data = result.Ok ? NavData::create(result.Mesh, error) : nullptr;
			check(data != nullptr, std::string(what) + ": the fixture loads (" + error + ")");
			return data;
		}

		void queries(Check& check)
		{
			const NavFilter filter;
			const glm::vec3 west{ -8.0f, 0.0f, 0.0f };
			const glm::vec3 east{ 8.0f, 0.0f, 0.0f };

			// A wall of 6 m across the way: the walk goes around its end.
			const Scope<NavData> wall = data_of(check, wall_fixture(3.0f), "path_around_wall");
			if (wall)
			{
				const NavQuery query(*wall);
				const NavPath path = query.find_path(west, east, filter);
				check(path.Status == NavPathStatus::Complete && path.Corners.size() >= 3, "path_around_wall: the route is complete and bends");
				check(!path.Corners.empty() && glm::distance(path.Corners.front(), west) < 0.5f && glm::distance(path.Corners.back(), east) < 0.5f, "path_around_wall: it starts and ends at the points asked");
				check(path.Length > 17.0f && path.Length < 19.5f, "path_around_wall: it is longer than the straight line, by the detour (length " + std::to_string(path.Length) + ")");

				glm::vec3 hit{ 0.0f };
				check(query.raycast(west, east, filter, &hit) && hit.x > -1.6f && hit.x < -0.4f, "raycast_hits_wall: the line is stopped at the wall");
				check(!query.raycast(west, glm::vec3(-8.0f, 0.0f, 6.0f), filter), "raycast_clear: a line along open floor is clear");
				check(query.is_walkable(west, filter) && !query.is_walkable({ 0.0f, 0.0f, 0.0f }, filter) && !query.is_walkable({ 500.0f, 0.0f, 500.0f }, filter), "walkable: open floor is, the wall and far away are not");

				const std::optional<glm::vec3> nearest = query.nearest_point({ 3.0f, 0.7f, 3.0f }, { 1.0f, 2.0f, 1.0f }, filter);
				check(nearest && std::abs(nearest->y) < 0.3f && glm::distance(glm::vec2(nearest->x, nearest->z), glm::vec2(3.0f, 3.0f)) < 0.5f, "nearest_point: a point above the floor snaps down to it");
				check(!query.nearest_point({ 500.0f, 0.0f, 500.0f }, { 1.0f, 2.0f, 1.0f }, filter).has_value(), "nearest_point: nothing within reach gives no point");

				NavQuery::seed_random(1234);
				int given = 0;
				int closeBy = 0;
				int onMesh = 0;
				for (int i = 0; i < 20; i++)
				{
					const std::optional<glm::vec3> point = query.random_point_around(west, 3.0f, filter);
					if (point)
					{
						given++;
						closeBy += glm::distance(glm::vec2(point->x, point->z), glm::vec2(west.x, west.z)) <= 3.3f ? 1 : 0;
						onMesh += query.nearest_point(*point, { 0.1f, 0.5f, 0.1f }, filter).has_value() ? 1 : 0;
					}
				}
				check(given == 20 && closeBy == 20 && onMesh == 20, "random_point: random points are on the mesh within the radius (given " + std::to_string(given) + ", near " + std::to_string(closeBy) + ", on mesh " + std::to_string(onMesh) + " of 20)");

				// Points that are not numbers, or not near the mesh, never crash and never give a route.
				const float nan = std::numeric_limits<float>::quiet_NaN();
				check(query.find_path({ nan, 0, 0 }, east, filter).Status == NavPathStatus::Failed, "query_invalid: a NaN start fails");
				check(query.find_path(west, { 0, std::numeric_limits<float>::infinity(), 0 }, filter).Status == NavPathStatus::Failed, "query_invalid: an infinite target fails");
				check(query.find_path(west, { 500.0f, 0.0f, 500.0f }, filter).Status == NavPathStatus::Failed, "query_invalid: a target far off the mesh fails");
				check(query.find_path(west, east, filter, { -1.0f, 1.0f, 1.0f }).Status == NavPathStatus::Failed, "query_invalid: negative extents fail");
				check(!query.nearest_point({ nan, 0, 0 }, { 1, 1, 1 }, filter).has_value() && !query.raycast({ nan, 0, 0 }, east, filter) && !query.random_point_around({ nan, 0, 0 }, 2.0f, filter).has_value(), "query_invalid: NaN gives no point, no hit");
			}

			// Without the wall the route is straight.
			const Scope<NavData> open = data_of(check, floor_fixture(false), "path_straight");
			if (open)
			{
				const NavQuery query(*open);
				const NavPath path = query.find_path(west, east, filter);
				check(path.Status == NavPathStatus::Complete && path.Corners.size() == 2 && std::abs(path.Length - 16.0f) < 0.3f, "path_straight_when_clear: an open floor gives a straight line (length " + std::to_string(path.Length) + ")");
			}

			// A wall across the whole floor cuts it in two.
			const Scope<NavData> cut = data_of(check, wall_fixture(10.0f), "path_blocked");
			if (cut)
			{
				const NavQuery query(*cut);
				check(query.find_path(west, east, filter).Status != NavPathStatus::Complete, "path_blocked: a wall across the floor leaves no complete route");
			}

			// A mud strip across the direct line, an open way round it: a costly mud sends the walk round, a cheap one does not.
			NavBakeInput mud;
			add_floor(mud, -10, -1, -10, 10, NavArea::Walkable);
			add_floor(mud, -1, 1, -10, 5, NavArea::Mud);
			add_floor(mud, -1, 1, 5, 10, NavArea::Walkable);
			add_floor(mud, 1, 10, -10, 10, NavArea::Walkable);
			mud.BoundsMin = { -10.0f, -1.0f, -10.0f };
			mud.BoundsMax = { 10.0f, 4.0f, 10.0f };
			mud.Build.CellSize = 0.25f;
			const Scope<NavData> mudData = data_of(check, mud, "area_cost");
			if (mudData)
			{
				const NavQuery query(*mudData);
				NavFilter cheap;
				NavFilter costly;
				costly.AreaCost[static_cast<size_t>(NavArea::Mud)] = 10.0f;
				const NavPath through = query.find_path(west, east, cheap);
				const NavPath around = query.find_path(west, east, costly);
				check(through.Status == NavPathStatus::Complete && through.Length < 16.6f, "area_cost: with an equal cost the walk crosses the mud (length " + std::to_string(through.Length) + ")");
				check(around.Status == NavPathStatus::Complete && around.Length > 18.0f, "area_cost: with a high mud cost the walk goes round (length " + std::to_string(around.Length) + ")");

				NavFilter noMud;
				noMud.AreaMask = ~nav_area_flag(NavArea::Mud);
				const NavPath avoided = query.find_path(west, east, noMud);
				check(avoided.Status == NavPathStatus::Complete && avoided.Length > 18.0f, "area_mask: an agent that may not enter mud goes round");
			}

			// A tile that Detour refuses fails the load with a reason.
			std::string error;
			check(NavData::create(sample_mesh(), error) == nullptr && !error.empty(), "load_rejects_bad_blob: a tile of garbage bytes is refused with a reason");
			check(NavData::create(NavMeshData{}, error) == nullptr, "load_rejects_bad_blob: an empty mesh is refused");
		}
	}

	namespace
	{
		NavMeshData baked(Check& check, const NavBakeInput& input)
		{
			const NavBakeResult result = bake_navmesh(input);
			check(result.Ok, "agents: the fixture bakes (" + result.Error + ")");
			return result.Mesh;
		}

		entt::entity add_agent(Scene& scene, glm::vec3 position)
		{
			const entt::entity entity = scene.create("Agent");
			scene.registry().get<Transform>(entity).Position = position;
			scene.registry().emplace<NavAgent>(entity);
			scene.update_world_transforms();
			return entity;
		}

		void run_frames(NavModule& module, int frames)
		{
			for (int i = 0; i < frames; i++)
			{
				module.tick(nullptr, true, 1.0f / 60.0f);
			}
		}

		glm::vec3 position_of(Scene& scene, entt::entity entity) { return scene.registry().get<Transform>(entity).Position; }

		void agents(Check& check)
		{
			const glm::vec3 west{ -8.0f, 0.0f, 0.0f };
			const glm::vec3 east{ 8.0f, 0.0f, 0.0f };

			// An agent walks round a wall to the other side and arrives.
			{
				const NavMeshData mesh = baked(check, wall_fixture(3.0f));
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity agent = add_agent(*scene, west);
				std::vector<NavAgentState> events;
				module.add_listener([&](entt::entity, NavAgentState state) { events.push_back(state); });

				check(module.start_world_with_mesh(scene, mesh) && module.has_navmesh(), "agent_world: a world starts with the mesh");
				run_frames(module, 2);
				check(module.active_agent_count() == 1, "agent_registered: the component becomes a crowd agent");
				check(module.set_destination(agent, east) && module.state(agent) == NavAgentState::Moving, "agent_destination: a destination on the mesh starts the walk");

				bool insideWall = false;
				float farthest = 0.0f;
				for (int i = 0; i < 1200 && module.state(agent) == NavAgentState::Moving; i++)
				{
					run_frames(module, 1);
					const glm::vec3 p = position_of(*scene, agent);
					insideWall = insideWall || (std::abs(p.x) < 0.45f && std::abs(p.z) < 3.0f);
					farthest = std::max(farthest, p.x);
				}
				check(module.state(agent) == NavAgentState::Arrived, "agent_reaches_target: the agent arrives");
				check(glm::distance(position_of(*scene, agent), east) < 0.5f && farthest > 7.0f, "agent_reaches_target: it stands at the target");
				check(!insideWall, "agent_reaches_target: it never walks through the wall");
				check(events.size() == 1 && events[0] == NavAgentState::Arrived, "agent_events: one Arrived event is sent");
				run_frames(module, 60); // it brakes after the arrival
				check(glm::length(module.velocity(agent)) < 0.1f && module.remaining_distance(agent) == 0.0f && glm::distance(position_of(*scene, agent), east) < 0.5f, "agent_arrived_still: an arrived agent stops and stays at the target");

				// Off the mesh: no walk.
				check(!module.set_destination(agent, { 500.0f, 0.0f, 500.0f }) && module.state(agent) == NavAgentState::NoPath, "agent_off_mesh: a destination far from the mesh is refused, the state is NoPath");

				// Warp, stop.
				check(module.warp(agent, west) && glm::distance(position_of(*scene, agent), west) < 0.5f, "agent_warp: a warp puts the agent at the point");
				module.set_destination(agent, east);
				run_frames(module, 30);
				module.stop(agent);
				check(module.state(agent) == NavAgentState::Idle, "agent_stop: a stopped agent is Idle");
				run_frames(module, 60); // it brakes
				const glm::vec3 stopped = position_of(*scene, agent);
				run_frames(module, 60);
				check(glm::distance(position_of(*scene, agent), stopped) < 0.1f && stopped.x < 6.0f, "agent_stop: a stopped agent stays put, short of the target");
			}

			// A wall across the whole floor: the walk goes as far as it can and ends NoPath.
			{
				const NavMeshData mesh = baked(check, wall_fixture(10.0f));
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity agent = add_agent(*scene, west);
				std::vector<NavAgentState> events;
				module.add_listener([&](entt::entity, NavAgentState state) { events.push_back(state); });
				module.start_world_with_mesh(scene, mesh);
				run_frames(module, 2);
				module.set_destination(agent, east);
				run_frames(module, 900);
				check(module.state(agent) == NavAgentState::NoPath, "agent_blocked_no_path: an unreachable destination ends in NoPath");
				check(position_of(*scene, agent).x < -0.4f, "agent_blocked_no_path: the agent stays on its side of the wall");
				check(!events.empty() && events.back() == NavAgentState::NoPath, "agent_blocked_no_path: a NoPath event is sent");
			}

			// An entity or a component that goes takes its agent with it; a clone that stops leaves nothing behind.
			{
				const NavMeshData mesh = baked(check, wall_fixture(3.0f));
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity gone = add_agent(*scene, west);
				const entt::entity stays = add_agent(*scene, glm::vec3(-8.0f, 0.0f, 4.0f));
				const entt::entity component = add_agent(*scene, glm::vec3(-8.0f, 0.0f, -4.0f));
				module.start_world_with_mesh(scene, mesh);
				run_frames(module, 2);
				module.set_destination(gone, east);
				module.set_destination(stays, east);
				run_frames(module, 30);
				check(module.active_agent_count() == 3, "agent_destroyed_cleans_up: three agents walk");
				scene->destroy(gone);
				scene->registry().remove<NavAgent>(component);
				run_frames(module, 2);
				check(module.active_agent_count() == 1 && module.state(gone) == NavAgentState::Idle, "agent_destroyed_cleans_up: a destroyed entity and a removed component leave the crowd");
				module.stop_world();
				check(module.active_agent_count() == 0 && !module.has_navmesh() && module.world_scene() == nullptr, "agent_stop_world: stopping the world leaves no agents and no mesh");
				(void)stays;

				// The Editor Play shape: a clone runs, then goes.
				Ref<Scene> copy = clone_scene(*scene);
				module.start_world_with_mesh(copy, mesh);
				run_frames(module, 5);
				check(module.active_agent_count() == 1, "nav_clone_stop_cleans_up: the clone agent is in the crowd");
				copy.reset();
				run_frames(module, 1);
				check(module.world_scene() == nullptr && module.active_agent_count() == 0, "nav_clone_stop_cleans_up: a clone that is gone takes its world with it");
			}

			// Auto start follows the active scene while the game plays.
			{
				const NavMeshData mesh = baked(check, wall_fixture(3.0f));
				NavModule module; // auto start is on, but a world with a mesh is the checks' to start
				Ref<Scene> scene = CreateRef<Scene>();
				add_agent(*scene, west);
				module.start_world_with_mesh(scene, mesh);
				module.tick(scene, true, 1.0f / 60.0f);
				check(module.world_scene() == scene && module.has_navmesh(), "auto_start: the world of the active scene stays");
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(module.world_scene() == nullptr && !module.has_navmesh(), "auto_start: another active scene ends the world");
			}

			// A parent that moved and turned, and DriveTransform off.
			{
				const NavMeshData mesh = baked(check, floor_fixture(false));
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity parent = scene->create("Parent");
				scene->registry().get<Transform>(parent).Position = glm::vec3(2.0f, 0.0f, 0.0f);
				const entt::entity child = scene->create("Child", parent);
				scene->registry().emplace<NavAgent>(child);
				scene->registry().get<Transform>(child).Position = glm::vec3(-4.0f, 0.0f, 0.0f); // the world point (-2, 0, 0)
				const entt::entity driven = add_agent(*scene, west);
				scene->registry().get<NavAgent>(driven).DriveTransform = false;
				scene->update_world_transforms();
				module.start_world_with_mesh(scene, mesh);
				run_frames(module, 2);
				module.set_destination(child, glm::vec3(2.0f, 0.0f, 5.0f)); // local (0, 0, 5)
				module.set_destination(driven, east);
				run_frames(module, 600);
				check(glm::distance(position_of(*scene, child), glm::vec3(0.0f, 0.0f, 5.0f)) < 0.6f, "agent_parented: a child of a moved parent ends at the destination (local " + std::to_string(position_of(*scene, child).x) + ", " + std::to_string(position_of(*scene, child).z) + ")");
				check(glm::distance(position_of(*scene, driven), west) < 0.01f && module.state(driven) == NavAgentState::Arrived, "agent_drive_off: DriveTransform off leaves the transform alone, the walk still runs");
			}

			// No volume: nothing to walk on, and it says so without crashing.
			{
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity agent = add_agent(*scene, west);
				check(module.start_world(scene) && !module.has_navmesh() && !module.loading(), "nav_no_volume: a scene without a volume has no mesh");
				run_frames(module, 3);
				check(!module.set_destination(agent, east) && module.state(agent) == NavAgentState::Idle && module.active_agent_count() == 0, "nav_no_volume: a destination fails cleanly");
				check(module.query() == nullptr, "nav_no_volume: there is no query");
				check(!module.start_world(nullptr), "nav_no_volume: no scene, no world");
			}

			// Several agents sent to one point all arrive (they cannot overlap, the crowd settles them near it).
			{
				const NavMeshData mesh = baked(check, floor_fixture(false));
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				std::vector<entt::entity> walkers;
				for (int i = 0; i < 4; i++)
				{
					walkers.push_back(add_agent(*scene, glm::vec3(-8.0f, 0.0f, -3.0f + 2.0f * static_cast<float>(i))));
				}
				module.start_world_with_mesh(scene, mesh);
				run_frames(module, 2);
				for (const entt::entity walker : walkers)
				{
					module.set_destination(walker, glm::vec3(6.0f, 0.0f, 0.0f));
				}
				for (int i = 0; i < 2400; i++)
				{
					run_frames(module, 1);
					bool all = true;
					for (const entt::entity walker : walkers)
					{
						all = all && module.state(walker) != NavAgentState::Moving;
					}
					if (all)
					{
						break;
					}
				}
				bool arrived = true;
				for (const entt::entity walker : walkers)
				{
					arrived = arrived && module.state(walker) == NavAgentState::Arrived;
				}
				check(arrived, "agent_shared_destination: four agents sent to one point all arrive");
			}

			// A destination asked for while the navmesh loads gets an answer when the load fails.
			{
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity agent = add_agent(*scene, west);
				const entt::entity volume = scene->create("Volume");
				scene->registry().emplace<NavMeshVolume>(volume).NavMesh.Guid = AssetGuid{ 0x3333u, 0x4444u };
				std::vector<NavAgentState> events;
				module.add_listener([&](entt::entity, NavAgentState state) { events.push_back(state); });
				module.start_world(scene);
				check(module.loading() && module.set_destination(agent, east), "queued_destination: a destination is accepted while the mesh loads");
				for (int i = 0; i < 600 && events.empty(); i++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
					module.tick(nullptr, true, 1.0f / 60.0f);
				}
				check(events.size() == 1 && events[0] == NavAgentState::NoPath && !module.loading(), "queued_destination: a mesh that fails to load tells the waiting agent NoPath");
			}

			// Two enabled volumes: the first wins, the other is counted.
			{
				NavModule module;
				module.set_auto_start(false);
				Ref<Scene> scene = CreateRef<Scene>();
				for (int i = 0; i < 2; i++)
				{
					const entt::entity volume = scene->create(i == 0 ? "First" : "Second");
					NavMeshVolume& component = scene->registry().emplace<NavMeshVolume>(volume);
					component.NavMesh.Guid = AssetGuid{ 0x1111u, 0x2222u + static_cast<u64>(i) };
				}
				check(module.start_world(scene) && module.ignored_volumes() == 1, "nav_two_volumes_first_wins: the second enabled volume is ignored");
				module.stop_world();
				check(!module.loading(), "nav_two_volumes_first_wins: stopping drops the load");
			}
		}
	}

	namespace
	{
		// One script, "Walker", whose hooks record what the agent's events did.
		class WalkerScript final : public IScriptSource
		{
		public:
			WalkerScript()
			{
				m_Script.Asset = AssetRef{ AssetGuid::generate(), 0 };
				m_Script.Path = "assets/scripts/walker.lua";
				m_Script.Source = "return Script:extend{ on_nav_arrived = function(self) record('arrived') end, on_nav_failed = function(self) record('failed') end }";
			}

			AssetRef asset() const { return m_Script.Asset; }

			virtual std::optional<LoadedScript> find_by_path(std::string_view path) override { return path == m_Script.Path ? std::optional<LoadedScript>(m_Script) : std::nullopt; }
			virtual std::optional<LoadedScript> find_by_asset(AssetRef ref) override { return ref == m_Script.Asset ? std::optional<LoadedScript>(m_Script) : std::nullopt; }

		private:
			LoadedScript m_Script{};
		};

		// A Lua runtime with the Nav API bound to a module, and a scene with the agent Walker (carrying the script) and a bare entity.
		struct LuaNavHarness
		{
			WalkerScript Source;
			ScriptRuntime Runtime{ Source };
			NavModule Module;
			Ref<Scene> TheScene = CreateRef<Scene>();
			ScriptInstances Instances{ Runtime };
			sol::environment Env{ Runtime.state(), sol::create, Runtime.sandbox() };
			std::vector<std::string> Events;
			std::string LoadError{};

			LuaNavHarness(const NavMeshData* mesh, bool withModule = true)
			{
				Runtime.sandbox().set_function("record", [this](const std::string& event) { Events.push_back(event); });
				ScriptClass* walker = Runtime.load(Source.asset(), LoadError);
				if (walker != nullptr)
				{
					register_script_component(*walker);
				}

				const entt::entity walkerEntity = add_agent(*TheScene, glm::vec3(-8.0f, 0.0f, 0.0f));
				TheScene->registry().get<Name>(walkerEntity).Value = "Walker";
				TheScene->create("Bare");
				if (walker != nullptr)
				{
					ComponentRegistry::get().find(script_component_name(Source.asset().Guid))->Add(TheScene->registry(), walkerEntity);
				}

				Module.set_auto_start(false);
				if (withModule)
				{
					Instances.context().Nav = &Module;
				}
				if (mesh != nullptr)
				{
					Module.start_world_with_mesh(TheScene, *mesh);
				}
				else
				{
					Module.start_world(TheScene);
				}
				Instances.start(TheScene);
			}

			// A frame: the scripts, then the navigation (the engine's order).
			void frames(int count)
			{
				for (int i = 0; i < count; i++)
				{
					Instances.update(1.0f / 60.0f);
					Module.tick(nullptr, true, 1.0f / 60.0f);
				}
			}

			std::string run(const std::string& chunk, std::string* error = nullptr)
			{
				sol::protected_function_result result = Runtime.state().safe_script(chunk, Env, sol::script_pass_on_error);
				if (!result.valid())
				{
					sol::error failure = result;
					if (error) *error = failure.what();
					return {};
				}
				if (error) error->clear();
				return result.get_type() == sol::type::none ? std::string() : Runtime.state()["tostring"](result.get<sol::object>()).get<std::string>();
			}

			bool fails(const std::string& chunk, const std::string& mention)
			{
				std::string error;
				run(chunk, &error);
				return !error.empty() && error.find(mention) != std::string::npos;
			}

			size_t count(const std::string& event) const { return static_cast<size_t>(std::count(Events.begin(), Events.end(), event)); }
		};

		void lua_nav(Check& check)
		{
			const NavMeshData wall = baked(check, wall_fixture(3.0f));
			{
				LuaNavHarness h(&wall);
				check(h.LoadError.empty(), "lua_harness: the walker script loads (" + h.LoadError + ")");
				check(h.run("return Nav.has_navmesh()") == "true", "lua_has_navmesh: the navmesh of the world is there");
				check(h.run("local r = Nav.find_path(vec3(-8,0,0), vec3(8,0,0)); return r.status .. ':' .. (#r.points >= 3 and 'bends' or 'straight') .. ':' .. (r.length > 17 and 'long' or 'short')") == "complete:bends:long", "lua_find_path: a path round the wall, with its points and length");
				check(h.run("return Nav.find_path(vec3(-8,0,0), vec3(500,0,500)).status") == "failed", "lua_find_path_off_mesh: a target far from the mesh is failed");
				check(h.run("return Nav.nearest_point(vec3(3,0.5,3)) ~= nil and Nav.nearest_point(vec3(500,0,500)) == nil") == "true", "lua_nearest_point: a vector near the mesh, nil far from it");
				check(h.run("local hit, p = Nav.raycast(vec3(-8,0,0), vec3(8,0,0)); return tostring(hit) .. ':' .. ((p ~= nil and p.x < -0.3 and p.x > -1.7) and 'wall' or 'none')") == "true:wall", "lua_raycast: the wall stops the line, and the point is where");
				check(h.run("local hit, p = Nav.raycast(vec3(-8,0,0), vec3(-8,0,6)); return tostring(hit) .. ':' .. tostring(p)") == "false:nil", "lua_raycast_clear: a clear line is false, nil");
				check(h.run("return Nav.random_point(vec3(-8,0,0), 3) ~= nil") == "true", "lua_random_point: a random point near a point of the mesh");

				check(h.fails("Nav.find_path(vec3(0/0,0,0), vec3(1,0,0))", "finite"), "lua_nan: a NaN vector is an error that says finite");
				check(h.fails("Nav.nearest_point(vec3(1,1/0,1))", "finite"), "lua_inf: an infinite vector is an error");
				check(h.fails("Nav.agent(find_entity('Walker')).state()", "colon") && h.fails("Nav.agent(find_entity('Walker')).stop()", "colon"), "lua_dot_call: an agent method called with a dot is a Lua error, not a crash");
				check(h.fails("Nav.agent(find_entity('Bare'))", "NavAgent"), "lua_no_agent: an entity without a NavAgent is an error that names the component");
				check(h.fails("Nav.find_path(5, 6)", "vec3") && h.fails("Nav.agent(5)", "entity") && h.fails("Nav.random_point(vec3(0,0,0), {})", "number") && h.fails("Nav.agent(find_entity('Walker')):warp('x')", "vec3"), "lua_bad_args: a number, a table or a string where a vector, an entity or a number goes is a Lua error, not a crash");

				// The agent walks, the handle tells, and the script hears.
				check(h.run("return Nav.agent(find_entity('Walker')):set_destination(vec3(8,0,0))") == "true", "lua_agent_set_destination: a destination on the mesh starts the walk");
				check(h.run("return Nav.agent(find_entity('Walker')):state()") == "moving", "lua_agent_state: the state is moving");
				h.frames(900);
				check(h.run("return Nav.agent(find_entity('Walker')):state()") == "arrived", "lua_agent_state: the agent arrived");
				check(h.count("arrived") == 1 && h.count("failed") == 0, "lua_nav_arrived: on_nav_arrived runs once for the entity");
				check(h.run("return Nav.agent(find_entity('Walker')):remaining_distance()") == "0.0" || h.run("return Nav.agent(find_entity('Walker')):remaining_distance()") == "0", "lua_agent_remaining: nothing remains");

				check(h.run("return Nav.agent(find_entity('Walker')):set_destination(vec3(500,0,500))") == "false" && h.run("return Nav.agent(find_entity('Walker')):state()") == "no_path", "lua_agent_off_mesh: an unreachable destination is false and no_path");
				check(h.fails("Nav.agent(find_entity('Walker')):set_destination(vec3(0/0,0,0))", "finite"), "lua_agent_nan: a NaN destination is an error");
				check(h.run("return Nav.agent(find_entity('Walker')):warp(vec3(-8,0,0))") == "true" && h.run("return Nav.agent(find_entity('Walker')):velocity().x") != "", "lua_agent_warp: warp and velocity");
			}

			// A walk that cannot finish tells the script.
			{
				const NavMeshData cut = baked(check, wall_fixture(10.0f));
				LuaNavHarness h(&cut);
				h.run("Nav.agent(find_entity('Walker')):set_destination(vec3(8,0,0))");
				h.frames(900);
				check(h.count("failed") == 1 && h.count("arrived") == 0, "lua_nav_failed: on_nav_failed runs once when the route ends short");
			}

			// No navmesh: the queries say so, nothing crashes.
			{
				LuaNavHarness h(nullptr);
				check(h.run("return Nav.has_navmesh()") == "false", "lua_no_navmesh: there is none");
				check(h.run("return Nav.find_path(vec3(0,0,0), vec3(1,0,0)).status .. ':' .. #Nav.find_path(vec3(0,0,0), vec3(1,0,0)).points") == "failed:0", "lua_no_navmesh: a path is failed, empty");
				check(h.run("return tostring(Nav.nearest_point(vec3(0,0,0))) .. ':' .. tostring((Nav.raycast(vec3(0,0,0), vec3(1,0,0)))) .. ':' .. tostring(Nav.random_point(vec3(0,0,0), 2))") == "nil:false:nil", "lua_no_navmesh: nearest, raycast and random are nil, false, nil");
				check(h.run("return Nav.agent(find_entity('Walker')):set_destination(vec3(1,0,0))") == "false", "lua_no_navmesh: a destination is false");
			}

			// Without the module the calls are Lua errors.
			{
				LuaNavHarness h(nullptr, false);
				check(h.fails("return Nav.has_navmesh()", "navigation module"), "lua_no_module: without a navigation module the call is a Lua error");
			}

			// The static stub declares the API.
			{
				std::ifstream file("engine/lua/wiwa.lua", std::ios::binary);
				std::stringstream text;
				text << file.rdbuf();
				const std::string stub = text.str();
				check(declared_globals(stub).contains("Nav") && stub.find("function Nav.find_path(") != std::string::npos && stub.find("function NavAgentHandle:set_destination(") != std::string::npos && stub.find("on_nav_arrived") != std::string::npos,
					"lua_stubs_have_nav: the static stub declares Nav, the agent handle and the hooks");
			}
		}
	}

	namespace
	{
		// A mesh made of the triangles of a bake input (the fixtures), in world coordinates.
		MeshData mesh_of(const NavBakeInput& input)
		{
			MeshData mesh;
			mesh.Name = "fixture";
			glm::vec3 lo(std::numeric_limits<float>::max());
			glm::vec3 hi(std::numeric_limits<float>::lowest());
			for (const glm::vec3& vertex : input.Vertices)
			{
				MeshVertex out;
				out.Position[0] = vertex.x;
				out.Position[1] = vertex.y;
				out.Position[2] = vertex.z;
				mesh.Vertices.push_back(out);
				lo = glm::min(lo, vertex);
				hi = glm::max(hi, vertex);
			}
			mesh.Indices = input.Indices;
			MeshPrimitive primitive;
			primitive.IndexCount = static_cast<u32>(input.Indices.size());
			primitive.VertexCount = static_cast<u32>(input.Vertices.size());
			mesh.Primitives.push_back(primitive);
			mesh.BoundsMin = lo;
			mesh.BoundsMax = hi;
			return mesh;
		}

		// Meshes from memory; with Gate set a get() waits until it is released (or the bake is cancelled).
		class MemoryMeshes final : public INavMeshSource
		{
		public:
			AssetRef add(const NavBakeInput& input)
			{
				const AssetRef ref{ AssetGuid::generate(), 0 };
				m_Meshes[ref.Guid] = mesh_of(input);
				return ref;
			}

			void set(const AssetRef& ref, const NavBakeInput& input) { m_Meshes[ref.Guid] = mesh_of(input); }

			virtual const MeshData* get(const AssetRef& ref, const std::atomic<bool>* cancel) override
			{
				while (Gate.load() && !(cancel != nullptr && cancel->load()))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				if (cancel != nullptr && cancel->load())
				{
					return nullptr;
				}
				const auto it = m_Meshes.find(ref.Guid);
				return it != m_Meshes.end() ? &it->second : nullptr;
			}

			std::atomic<bool> Gate{ false };

		private:
			std::map<AssetGuid, MeshData> m_Meshes{};
		};

		bool wait_for_bake(NavEditModel& model)
		{
			for (int i = 0; i < 20000 && model.state() == NavEditModel::BakeState::Running; i++)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			model.poll();
			return model.state() != NavEditModel::BakeState::Running;
		}

		std::vector<char> read_all(const std::filesystem::path& file)
		{
			std::ifstream stream(file, std::ios::binary);
			return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		}

		void edit_model(Check& check)
		{
			// The floor and the wall are two entities with meshes; the volume is a box round them.
			NavBakeInput floorOnly;
			add_floor(floorOnly, -10, 10, -10, 10, NavArea::Walkable);
			NavBakeInput wallOnly;
			add_box(wallOnly, { -0.5f, 0.0f, -3.0f }, { 0.5f, 2.0f, 3.0f });

			MemoryMeshes meshes;
			const AssetRef floorMesh = meshes.add(floorOnly);
			const AssetRef wallMesh = meshes.add(wallOnly);

			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity floor = scene->create("Floor");
			scene->registry().emplace<MeshRenderer>(floor).Mesh = floorMesh;
			const entt::entity wall = scene->create("Wall");
			scene->registry().emplace<MeshRenderer>(wall).Mesh = wallMesh;
			const entt::entity volume = scene->create("Volume");
			scene->registry().get<Transform>(volume).Position = glm::vec3(0.0f, 1.5f, 0.0f);
			NavMeshVolume& settings = scene->registry().emplace<NavMeshVolume>(volume);
			settings.Size = glm::vec3(20.0f, 5.0f, 20.0f);
			settings.CellSize = 0.25f;

			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_nav_model.wnav";
			std::error_code remove;
			std::filesystem::remove(file, remove);

			NavEditModel model(meshes);
			check(!model.start_bake(*scene, floor, file) && !model.error().empty(), "model_not_a_volume: an entity that is not a volume cannot be baked");

			// A bake on the worker writes the file and reports.
			check(model.start_bake(*scene, volume, file), "model_bake: a bake starts (" + model.error() + ")");
			check(wait_for_bake(model) && model.state() == NavEditModel::BakeState::Done, "model_bake: it ends Done (" + model.error() + ")");
			check(model.progress() == 1.0f && model.log().find("Baked") != std::string::npos && model.last().PolyCount > 0, "model_bake: progress, log and numbers are there");
			NavMeshData stored;
			std::string error;
			const std::vector<char> first = read_all(file);
			check(!first.empty() && nav_mesh_from_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(first.data()), first.size()), stored, error), "model_bake: the written file is a valid .wnav (" + error + ")");

			const NavBakeResult direct = bake_navmesh(build_nav_input(capture_nav_snapshot(*scene, volume), meshes));
			check(direct.Ok && stored == direct.Mesh, "model_bake: the worker bakes what a direct bake of the same scene gives");

			// The file is not stale until the scene changes.
			check(model.is_stale(*scene, volume, stored) == std::optional<bool>(false), "model_stale: a fresh bake is not stale");
			scene->registry().get<Transform>(wall).Position = glm::vec3(0.0f, 0.0f, 2.0f);
			check(model.is_stale(*scene, volume, stored) == std::optional<bool>(true), "model_stale: a moved wall makes the bake stale");
			scene->registry().get<Transform>(wall).Position = glm::vec3(0.0f);
			check(model.is_stale(*scene, volume, stored) == std::optional<bool>(false), "model_stale: putting it back makes it fresh again");
			scene->registry().get<NavMeshVolume>(volume).AgentRadius = 0.5f;
			check(model.is_stale(*scene, volume, stored) == std::optional<bool>(true), "model_stale: other settings make the bake stale");
			scene->registry().get<NavMeshVolume>(volume).AgentRadius = 0.4f;

			// The mesh of an agent is not ground: a wall that belongs to an agent (or to a child of one) does not take part in the bake.
			{
				const entt::entity agentBody = scene->create("Agent");
				scene->registry().emplace<NavAgent>(agentBody);
				const entt::entity agentWall = scene->create("Agent wall", agentBody);
				scene->registry().emplace<MeshRenderer>(agentWall).Mesh = wallMesh;
				const NavSnapshot withAgent = capture_nav_snapshot(*scene, volume);
				check(withAgent.Items.size() == 2, "model_agent_geometry: the meshes of an agent are left out of the snapshot (" + std::to_string(withAgent.Items.size()) + " meshes)");
				scene->destroy(agentWall);
				scene->destroy(agentBody);
			}

			// A volume with nothing in it fails and leaves the file alone.
			scene->registry().get<Transform>(volume).Position = glm::vec3(500.0f, 1.5f, 500.0f);
			check(model.start_bake(*scene, volume, file) && wait_for_bake(model) && model.state() == NavEditModel::BakeState::Failed && !model.error().empty(), "model_failure_keeps_asset: a volume without geometry fails with a reason");
			check(read_all(file) == first, "model_failure_keeps_asset: the file of the earlier bake is untouched");
			scene->registry().get<Transform>(volume).Position = glm::vec3(0.0f, 1.5f, 0.0f);

			// A bake that is cancelled while it waits for its meshes fails as cancelled and leaves the file alone.
			meshes.Gate = true;
			check(model.start_bake(*scene, volume, file), "model_cancel: a bake starts");
			check(!model.start_bake(*scene, volume, file) && model.error().find("running") != std::string::npos, "model_cancel: a second bake is refused while one runs");
			model.cancel();
			check(wait_for_bake(model) && model.state() == NavEditModel::BakeState::Failed && model.error() == "cancelled", "model_cancel: a cancelled bake ends Failed with 'cancelled'");
			check(read_all(file) == first, "model_cancel: the file is untouched");
			meshes.Gate = false;

			// The model can be destroyed while a bake runs.
			{
				NavEditModel other(meshes);
				meshes.Gate = true;
				other.start_bake(*scene, volume, file);
			}
			meshes.Gate = false;
			check(read_all(file) == first, "model_destroyed_running: destroying the model during a bake stops it and leaves the file");
			std::filesystem::remove(file, remove);
		}
	}

	namespace
	{
		void view_and_tools(Check& check)
		{
			const Scope<NavData> wall = data_of(check, wall_fixture(3.0f), "view");
			if (!wall)
			{
				return;
			}

			// The polygons as plain data.
			const NavMeshView view = extract_navmesh_view(*wall);
			check(!view.empty() && view.Vertices.size() >= 3 * view.Polys.size() && view.BoundaryEdge.size() == view.Vertices.size(), "view_extract: polygons, vertices and one boundary flag per vertex");
			bool shaped = true;
			bool inside = true;
			size_t boundary = 0;
			for (const NavViewPoly& poly : view.Polys)
			{
				shaped = shaped && poly.VertexCount >= 3 && poly.VertexCount <= 6 && poly.FirstVertex + poly.VertexCount <= view.Vertices.size() && poly.Area == static_cast<u8>(NavArea::Walkable);
			}
			for (size_t i = 0; i < view.Vertices.size(); i++)
			{
				inside = inside && glm::all(glm::greaterThanEqual(view.Vertices[i], view.BoundsMin - 0.01f)) && glm::all(glm::lessThanEqual(view.Vertices[i], view.BoundsMax + 0.01f));
				boundary += view.BoundaryEdge[i];
			}
			check(shaped && inside, "view_extract: every polygon has 3 to 6 vertices and an area, and lies inside the bounds");
			check(boundary > 0 && boundary < view.Vertices.size(), "view_extract: some edges are the border, not all");
			check(view.BoundsMin.x < -9.0f && view.BoundsMax.x > 9.0f && std::abs(view.BoundsMax.y) < 0.5f, "view_extract: the bounds are the floor");

			// The overlay shapes follow the options.
			NavOverlayOptions all;
			const NavOverlayShapes full = nav_overlay_shapes(view, all);
			check(!full.Triangles.empty() && !full.Lines.empty(), "overlay_shapes: triangles and lines");
			NavOverlayOptions onlyBoundary;
			onlyBoundary.Polys = false;
			onlyBoundary.Edges = false;
			const NavOverlayShapes border = nav_overlay_shapes(view, onlyBoundary);
			check(border.Triangles.empty() && border.Lines.size() == boundary, "overlay_shapes: only the boundary option gives exactly the border edges (" + std::to_string(border.Lines.size()) + " of " + std::to_string(boundary) + ")");
			NavOverlayOptions nothing;
			nothing.Polys = nothing.Edges = nothing.Boundary = false;
			const NavOverlayShapes none = nav_overlay_shapes(view, nothing);
			check(none.Triangles.empty() && none.Lines.empty(), "overlay_shapes: no option, no shapes");
			NavOverlayOptions noEdges;
			noEdges.Edges = false;
			check(nav_overlay_shapes(view, noEdges).Lines.size() == boundary, "overlay_shapes: without edges only the border is drawn");
			bool lifted = true;
			for (const NavOverlayTriangle& triangle : full.Triangles)
			{
				lifted = lifted && triangle.A.y > view.BoundsMin.y;
			}
			check(lifted, "overlay_shapes: shapes are lifted off the ground");
			check(nav_area_color(static_cast<u8>(NavArea::Mud), 1.0f) != nav_area_color(static_cast<u8>(NavArea::Walkable), 1.0f) && nav_area_color(static_cast<u8>(NavArea::Water), 1.0f) != nav_area_color(static_cast<u8>(NavArea::Custom0), 1.0f), "overlay_colors: areas have their own colours");

			// An area shows in the polygons.
			NavBakeInput split;
			add_floor(split, -10, 0, -10, 10, NavArea::Walkable);
			add_floor(split, 0, 10, -10, 10, NavArea::Mud);
			split.BoundsMin = { -10.0f, -1.0f, -10.0f };
			split.BoundsMax = { 10.0f, 4.0f, 10.0f };
			split.Build.CellSize = 0.25f;
			const Scope<NavData> twoAreas = data_of(check, split, "view_areas");
			if (twoAreas)
			{
				const NavMeshView areas = extract_navmesh_view(*twoAreas);
				bool mudEast = true;
				bool sawMud = false;
				bool sawWalkable = false;
				for (const NavViewPoly& poly : areas.Polys)
				{
					const glm::vec3 first = areas.Vertices[poly.FirstVertex];
					if (poly.Area == static_cast<u8>(NavArea::Mud))
					{
						sawMud = true;
						mudEast = mudEast && first.x > -0.5f;
					}
					if (poly.Area == static_cast<u8>(NavArea::Walkable))
					{
						sawWalkable = true;
					}
				}
				check(sawMud && sawWalkable && mudEast, "view_areas: the mud half of the floor gives Mud polygons, the other half Walkable");
			}

			// Picking a point on the mesh with a ray.
			const std::optional<glm::vec3> down = NavPathTool::pick(view, { -8.0f, 5.0f, 2.0f }, { 0.0f, -1.0f, 0.0f });
			check(down && glm::distance(*down, glm::vec3(-8.0f, 0.0f, 2.0f)) < 0.3f, "path_tool_pick: a ray straight down hits the floor under it");
			const std::optional<glm::vec3> slanted = NavPathTool::pick(view, { -8.0f, 5.0f, 8.0f }, { 0.5f, -1.0f, -0.5f });
			check(slanted && glm::distance(glm::vec2(slanted->x, slanted->z), glm::vec2(-5.5f, 5.5f)) < 0.5f && std::abs(slanted->y) < 0.3f, "path_tool_pick: a slanted ray hits where it should");
			check(!NavPathTool::pick(view, { 200.0f, 5.0f, 200.0f }, { 0.0f, -1.0f, 0.0f }).has_value(), "path_tool_pick: a ray beside the mesh misses");
			check(!NavPathTool::pick(view, { 0.0f, 5.0f, 5.0f }, { 0.0f, 1.0f, 0.0f }).has_value(), "path_tool_pick: a ray pointing away misses");
			check(!NavPathTool::pick(view, { 0.0f, 5.0f, 5.0f }, { 0.0f, 0.0f, 0.0f }).has_value() && !NavPathTool::pick(view, { std::numeric_limits<float>::quiet_NaN(), 5.0f, 5.0f }, { 0.0f, -1.0f, 0.0f }).has_value(), "path_tool_pick: a zero or NaN ray gives nothing");

			// The route between two picked points is the one the query gives.
			const NavQuery query(*wall);
			const NavFilter filter;
			NavPathTool tool;
			tool.recompute(query, filter);
			check(tool.path().Status == NavPathStatus::Failed && !tool.has_start(), "path_tool: without points there is no route");
			tool.set_start({ -8.0f, 0.0f, 0.0f });
			tool.recompute(query, filter);
			check(tool.path().Status == NavPathStatus::Failed, "path_tool: with a start only there is no route");
			tool.set_end({ 8.0f, 0.0f, 0.0f });
			tool.recompute(query, filter);
			const NavPath expected = query.find_path({ -8.0f, 0.0f, 0.0f }, { 8.0f, 0.0f, 0.0f }, filter);
			check(tool.has_start() && tool.has_end() && tool.path().Status == NavPathStatus::Complete && tool.path().Corners == expected.Corners && tool.path().Length == expected.Length, "path_tool: the route is the query's");
			tool.clear();
			check(!tool.has_start() && !tool.has_end() && tool.path().Corners.empty(), "path_tool: clear forgets both points and the route");
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
		queries(check);
		agents(check);
		lua_nav(check);
		edit_model(check);
		view_and_tools(check);

		if (check.Failures == 0)
		{
			WI_NAV_LOG(Info, "Nav smoke test passed");
		}
		return check.Failures == 0;
	}
}
