#include <editor_common.h>
#include "nav_smoke_test.h"

#include <cstring>

#include <nav/nav_mesh_asset.h>
#include <nav/nav_project_settings.h>
#include <scene/components.h>
#include <scene/reflection/component_registry.h>
#include <scene/scene.h>
#include <scene/scene_clone.h>
#include <scene/scene_components.h>
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

	bool run_nav_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		wnav_format(check);
		components(check);
		settings(check);

		if (check.Failures == 0)
		{
			WI_NAV_LOG(Info, "Nav smoke test passed");
		}
		return check.Failures == 0;
	}
}
