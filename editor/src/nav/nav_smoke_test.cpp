#include <editor_common.h>
#include "nav_smoke_test.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

#include <nav/nav_data.h>
#include <nav/nav_mesh_asset.h>
#include <nav/nav_module.h>
#include <nav/nav_query.h>
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

		if (check.Failures == 0)
		{
			WI_NAV_LOG(Info, "Nav smoke test passed");
		}
		return check.Failures == 0;
	}
}
