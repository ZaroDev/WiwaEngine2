#include <editor_common.h>
#include "asset_smoke_test.h"

#include <assets/asset_manager.h>
#include <assets/build_info.h>
#include <assets/library_source.h>
#include <assets/pak_source.h>
#include <core/engine.h>

#include "build_settings.h"
#include "file_utils.h"
#include "packager.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

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
					WI_ASSET_LOG(Error, "Smoke test FAILED: {0}", what);
				}
			}
		};

		struct CookedFile
		{
			AssetGuid Guid{};
			AssetType Type = AssetType::None;
			std::vector<AssetGuid> Dependencies{};
			std::vector<std::byte> Bytes{};
		};

		std::vector<CookedFile> read_library(const std::filesystem::path& library)
		{
			std::vector<CookedFile> files;
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(library, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (!it->is_regular_file() || it->path().extension() != ".wasset")
				{
					continue;
				}

				CookedFile file;
				if (std::optional<std::vector<std::byte>> bytes = read_binary_file(it->path()))
				{
					std::span<const std::byte> payload;
					if (std::optional<WAssetHeader> header = parse_wasset_header(*bytes, payload))
					{
						file.Guid = header->Guid;
						file.Type = header->Type;
						file.Dependencies = header->Dependencies;
						file.Bytes = std::move(*bytes);
						files.push_back(std::move(file));
					}
				}
			}
			return files;
		}

		// Starts loading an asset of any type; the returned function tells its state.
		std::function<LoadState()> request(AssetManager& manager, AssetType type, AssetGuid guid)
		{
			const AssetRef ref{ guid, 0 };
			switch (type)
			{
			case AssetType::Texture: { auto h = manager.load<TextureData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Mesh: { auto h = manager.load<MeshData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Material: { auto h = manager.load<MaterialData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Skeleton: { auto h = manager.load<SkeletonData>(ref); return [h]() { return h.state(); }; }
			case AssetType::AnimationClip: { auto h = manager.load<AnimationClipData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Prefab: { auto h = manager.load<PrefabData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Environment: { auto h = manager.load<EnvironmentData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Lut: { auto h = manager.load<LutData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Shader: { auto h = manager.load<ShaderData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Scene: { auto h = manager.load<SceneData>(ref); return [h]() { return h.state(); }; }
			default: return []() { return LoadState::Failed; };
			}
		}

		struct MetaSnapshot
		{
			AssetGuid Guid{};
			std::map<std::string, u64> SubAssets{};
		};

		std::map<std::filesystem::path, MetaSnapshot> snapshot_metas(const std::filesystem::path& assets)
		{
			std::map<std::filesystem::path, MetaSnapshot> result;
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(assets, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (it->is_regular_file() && it->path().extension() == ".meta")
				{
					if (std::optional<MetaFile> meta = MetaFile::load(it->path()))
					{
						result[it->path()] = MetaSnapshot{ meta->Guid, meta->SubAssets };
					}
				}
			}
			return result;
		}
	}

	namespace
	{
		// the .meta files of every source root (Assets/ and engine/)
		std::map<std::filesystem::path, MetaSnapshot> snapshot_all_metas(const AssetDatabase& database)
		{
			std::map<std::filesystem::path, MetaSnapshot> result;
			for (const AssetDatabase::SourceRoot& root : database.get_roots())
			{
				result.merge(snapshot_metas(root.Source));
			}
			return result;
		}
	}

	bool run_asset_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;
		AssetDatabase& database = pipeline.get_database();
		AssetManager& manager = *Engine::get().get_module<AssetManager>();

		const std::filesystem::path temp = std::filesystem::temp_directory_path() / "wiwa_asset_smoke";
		std::error_code error;
		std::filesystem::remove_all(temp, error);

		// A shader payload survives a round trip, and no truncated copy of it is accepted.
		{
			ShaderData shader;
			shader.Name = "smoke.hlsl";
			shader.Binaries.push_back(ShaderBinary{ ShaderStage::Pixel, ShaderTarget::Spirv, "PSMain", { 1, 2, 3, 4 } });
			shader.Binaries.push_back(ShaderBinary{ ShaderStage::Vertex, ShaderTarget::Dxbc, "VSMain", { 9 } });

			const std::vector<std::byte> bytes = serialize(shader);
			std::optional<ShaderData> back = deserialize<ShaderData>(bytes);
			const ShaderBinary* pixel = back ? back->find(ShaderStage::Pixel, ShaderTarget::Spirv) : nullptr;
			check(pixel != nullptr && pixel->Code == shader.Binaries[0].Code && back->find(ShaderStage::Vertex, ShaderTarget::Dxbc) != nullptr, "a shader payload round-trips");
			check(back && back->find(ShaderStage::Compute, ShaderTarget::Dxbc) == nullptr, "find returns null for a binary that is not there");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<ShaderData>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated shader payload is rejected");
		}

		// The sample environment is cooked into a cube with prefiltered mips and SH.
		if (const std::optional<AssetRef> sample = manager.find_asset("Assets/environments/kloofendal_48d_partly_cloudy_puresky_1k.hdr"))
		{
			Handle<EnvironmentData> handle = manager.load<EnvironmentData>(*sample);
			manager.wait_idle();
			const EnvironmentData* environment = handle.get();
			check(environment != nullptr, "the sample .hdr loads as a cooked environment");
			if (environment != nullptr)
			{
				check(environment->FaceSize == 512 && environment->Mips.size() == 7, "its cube has the face size and the mips of the prefilter chain");
				bool finite = true;
				for (const glm::vec4& coefficient : environment->SH)
				{
					finite = finite && std::isfinite(coefficient.x) && std::isfinite(coefficient.y) && std::isfinite(coefficient.z);
				}
				check(finite && environment->SH[0].x > 0.0f && environment->SH[0].y > 0.0f && environment->SH[0].z > 0.0f, "its SH are finite and the first coefficient is positive");
			}
		}

		// The catalog survives a round trip, and no truncated copy of it is accepted.
		{
			AssetCatalog catalog;
			const AssetGuid guid = AssetGuid::generate();
			catalog.Entries.push_back({ "engine/shaders/x.hlsl", guid });
			catalog.Entries.push_back({ "Assets/models/y.glb", AssetGuid::generate() });

			const std::vector<std::byte> bytes = serialize(catalog);
			std::optional<AssetCatalog> back = deserialize<AssetCatalog>(bytes);
			check(back && back->find("engine\\shaders\\x.hlsl") && back->find("engine/shaders/x.hlsl")->Guid == guid, "the catalog round-trips and finds a path");
			check(back && !back->find("engine/shaders/missing.hlsl"), "the catalog does not find an unknown path");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<AssetCatalog>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated catalog is rejected");
		}

		// The build info (which scene a pack starts with) survives a round trip, and no truncated copy of it is accepted.
		{
			const AssetGuid scene = AssetGuid::generate();
			BuildInfo info;
			info.DefaultScene = scene;

			const std::vector<std::byte> bytes = serialize(info);
			std::optional<BuildInfo> back = deserialize<BuildInfo>(bytes);
			check(back && back->DefaultScene == scene, "build info round-trips");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<BuildInfo>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated build info is rejected");
		}

		// The build settings survive a round trip; a missing file, a damaged one and a bad guid are told apart.
		{
			std::filesystem::create_directories(temp, error);
			const std::filesystem::path file = temp / "build_settings.json";

			BuildSettings settings;
			settings.Scenes = { AssetGuid::generate(), AssetGuid::generate() };
			settings.DefaultScene = settings.Scenes[1];
			settings.AlwaysInclude = { AssetGuid::generate() };
			check(settings.save(file), "the build settings save");

			std::string loadError;
			std::optional<BuildSettings> back = BuildSettings::load(file, loadError);
			check(back && back->DefaultScene == settings.DefaultScene && back->Scenes == settings.Scenes && back->AlwaysInclude == settings.AlwaysInclude, "the build settings round-trip");

			std::optional<BuildSettings> missing = BuildSettings::load(temp / "no_such_settings.json", loadError);
			check(!missing && loadError.empty(), "a missing settings file is nullopt without an error");

			{
				std::ofstream(file) << "{not json";
			}
			check(!BuildSettings::load(file, loadError) && !loadError.empty(), "a damaged settings file is an error");

			{
				std::ofstream(file) << R"({ "scenes": ["zz"] })";
			}
			loadError.clear();
			check(!BuildSettings::load(file, loadError) && loadError.find("zz") != std::string::npos, "a bad guid is an error that names it");

			BuildSettings empty;
			check(empty.validate(loadError), "empty settings are valid");
			BuildSettings stray;
			stray.DefaultScene = AssetGuid::generate();
			check(!stray.validate(loadError) && !loadError.empty(), "a default scene outside the scene list is invalid");
			stray.Scenes = { stray.DefaultScene };
			check(stray.validate(loadError), "a default scene in the scene list is valid");
		}

		// 0. an import reports its progress and ends with every item done
		{
			Ref<JobProgress> scan = pipeline.scan_async();
			scan->wait();
			check(scan->succeeded(), "the import job succeeds");
			check(scan->done() == scan->total() && scan->fraction() == 1.0f, "the import job reports all its items done");
		}

		// 1. import + pack
		check(Packager::pack(database, temp), "packing the Library");
		const std::vector<CookedFile> cooked = read_library(database.get_library_root());
		check(!cooked.empty(), "the Library has cooked assets (put some files in Assets/)");
		WI_ASSET_LOG(Info, "Smoke test: {0} cooked assets", cooked.size());

		// 2. the pack holds exactly the Library's bytes
		PakSource pak(temp / "assets.wpak");
		check(pak.valid(), "the pack opens");
		check(pak.get_index().size() == cooked.size() + 1, "the pack has one entry per cooked asset, and the catalog"); // + the catalog
		check(pak.contains(CATALOG_GUID), "the pack holds the asset catalog");
		for (const CookedFile& file : cooked)
		{
			std::optional<std::vector<std::byte>> fromPak = pak.read(file.Guid);
			check(fromPak && *fromPak == file.Bytes, "pack bytes equal Library bytes for " + file.Guid.to_string());
			for (AssetGuid dependency : file.Dependencies)
			{
				check(pak.contains(dependency), "dependency " + dependency.to_string() + " of " + file.Guid.to_string() + " is in the pack");
			}
		}

		// 3. every asset loads through the AssetManager reading the pack
		manager.set_source(CreateScope<PakSource>(temp / "assets.wpak"));
		{
			// the engine's own assets are found by path through the packed catalog, no guid involved
			std::optional<AssetRef> imgui = manager.find_asset("engine/shaders/imgui.hlsl");
			check(imgui.has_value(), "engine/shaders/imgui.hlsl is found by path in the packed catalog");
			check(!manager.find_asset("engine/shaders/does_not_exist.hlsl").has_value(), "a path that is not in the catalog is not found");
			check(manager.find_asset(".\\engine\\shaders\\imgui.hlsl").has_value(), "paths are accepted with backslashes and a leading ./");

			std::vector<std::function<LoadState()>> handles;
			for (const CookedFile& file : cooked)
			{
				handles.push_back(request(manager, file.Type, file.Guid));
			}
			manager.wait_idle();
			for (size_t i = 0; i < handles.size(); i++)
			{
				check(handles[i]() == LoadState::Ready, "asset " + cooked[i].Guid.to_string() + " loads from the pack");
			}

			// the same assets asked for through preload() report their progress to the end
			std::vector<AssetRef> refs;
			for (const CookedFile& file : cooked)
			{
				refs.push_back(AssetRef{ file.Guid, 0 });
			}
			Ref<JobProgress> preload = manager.preload(refs);
			preload->wait();
			check(preload->succeeded(), "preloading every asset succeeds");
			check(preload->total() == cooked.size() && preload->done() == preload->total(), "preload counts every asset once");

			// the same guid asked for as the wrong type fails, and a missing guid fails
			WI_ASSET_LOG(Info, "Smoke test: the next two load errors are expected");
			const AssetType other = (cooked[0].Type == AssetType::Mesh) ? AssetType::Texture : AssetType::Mesh;
			check(request(manager, other, cooked[0].Guid)() == LoadState::Failed, "a wrong-type load fails");
			auto missing = request(manager, AssetType::Texture, AssetGuid::generate());
			manager.wait_idle();
			check(missing() == LoadState::Failed, "a missing asset fails");
		}
		manager.unload_unused();

		// 3b. which assets a pack holds follows the build settings
		{
			// the selection itself, on made-up assets: A, D and E are scenes; A and D both use C
			const AssetGuid a = AssetGuid::generate(), b = AssetGuid::generate(), c = AssetGuid::generate(), d = AssetGuid::generate();
			const AssetGuid e = AssetGuid::generate(), f = AssetGuid::generate(), g = AssetGuid::generate(), engineAsset = AssetGuid::generate();
			const AssetGuid x = AssetGuid::generate(), y = AssetGuid::generate();
			const std::vector<PackCandidate> candidates = {
				{ a, { b } }, { b, { c } }, { c, {} }, { d, { c } }, { e, { f } }, { f, {} }, { g, {} },
				{ engineAsset, {}, true }, { x, { y } }, { y, { x } } };
			std::vector<AssetGuid> missing;

			BuildSettings settings;
			settings.Scenes = { a };
			std::unordered_set<AssetGuid> set = select_pack_set(candidates, settings, missing);
			check(set.contains(a) && set.contains(b) && set.contains(c), "a chosen scene brings what it uses, transitively");
			check(!set.contains(d) && !set.contains(e) && !set.contains(f), "a scene that is not chosen, and what only it uses, stay out");
			check(set.contains(engineAsset), "engine assets are always packed");
			check(!set.contains(g) && missing.empty(), "an asset nobody asks for stays out");

			settings.Scenes = { d };
			check(select_pack_set(candidates, settings, missing).contains(c), "an asset shared with a chosen scene stays in the pack");

			settings.Scenes.clear();
			settings.AlwaysInclude = { g };
			set = select_pack_set(candidates, settings, missing);
			check(set.contains(g) && !set.contains(a), "an always-included asset is packed without a scene using it");

			settings.AlwaysInclude = { x };
			set = select_pack_set(candidates, settings, missing);
			check(set.contains(x) && set.contains(y), "a dependency cycle ends and holds both assets");

			settings.AlwaysInclude.clear();
			set = select_pack_set(candidates, settings, missing);
			check(set.size() == 1 && set.contains(engineAsset), "empty settings pack the engine assets only");

			const AssetGuid unknown = AssetGuid::generate();
			settings.Scenes = { unknown };
			select_pack_set(candidates, settings, missing);
			check(missing.size() == 1 && missing[0] == unknown, "a guid that is not in the Library is reported as missing");

			// an asset that a packed asset needs but that has no cooked file is reported too, once
			const AssetGuid lost = AssetGuid::generate(), user = AssetGuid::generate(), otherUser = AssetGuid::generate();
			std::vector<PackCandidate> dangling = candidates;
			dangling.push_back({ user, { lost } });
			dangling.push_back({ otherUser, { lost } });
			settings.Scenes = { user, otherUser };
			select_pack_set(dangling, settings, missing);
			check(missing.size() == 1 && missing[0] == lost, "a dependency without a cooked file is reported as missing, once");
			settings.Scenes = { a };
			select_pack_set(dangling, settings, missing);
			check(missing.empty(), "a dangling dependency of an asset that is not packed does not matter");

			// a real pack: the project's scenes, none or all of them
			std::unordered_map<AssetGuid, const CookedFile*> byGuid;
			std::vector<AssetGuid> scenes;
			for (const CookedFile& file : cooked)
			{
				byGuid[file.Guid] = &file;
				if (file.Type == AssetType::Scene)
				{
					scenes.push_back(file.Guid);
				}
			}
			check(!scenes.empty(), "the Library has a scene to test the pack selection with");

			if (!scenes.empty())
			{
				std::unordered_set<AssetGuid> needed; // what the scenes use, transitively
				std::vector<AssetGuid> pending = scenes;
				while (!pending.empty())
				{
					const AssetGuid current = pending.back();
					pending.pop_back();
					if (needed.insert(current).second && byGuid.contains(current))
					{
						pending.insert(pending.end(), byGuid[current]->Dependencies.begin(), byGuid[current]->Dependencies.end());
					}
				}

				BuildSettings none; // no scene: only the engine's assets
				const std::filesystem::path noneDirectory = temp / "none";
				check(Packager::pack(database, noneDirectory, nullptr, &none), "packing with no scene chosen");
				PakSource nonePak(noneDirectory / "assets.wpak");
				check(nonePak.valid() && nonePak.get_index().size() < pak.get_index().size(), "a pack with no scene is smaller than the full pack");
				check(!nonePak.contains(scenes[0]), "an unchosen scene is not in the pack");
				check(!nonePak.contains(BUILD_INFO_GUID), "no default scene, no build info");
				std::optional<std::vector<std::byte>> noneCatalog = nonePak.read_catalog();
				std::optional<AssetCatalog> noneEntries = noneCatalog ? deserialize<AssetCatalog>(*noneCatalog) : std::nullopt;
				check(noneEntries && noneEntries->find("engine/shaders/imgui.hlsl") && !noneEntries->find("Assets/scenes/demo.wscene"), "its catalog lists the engine's assets and not the unchosen scene");

				BuildSettings all;
				all.Scenes = scenes;
				all.DefaultScene = scenes[0];
				const std::filesystem::path allDirectory = temp / "all";
				check(Packager::pack(database, allDirectory, nullptr, &all), "packing with every scene chosen");
				PakSource allPak(allDirectory / "assets.wpak");
				bool everythingNeeded = allPak.valid();
				for (AssetGuid guid : needed)
				{
					everythingNeeded = everythingNeeded && allPak.contains(guid);
				}
				check(everythingNeeded, "a chosen scene and everything it uses are in the pack");
				std::optional<BuildInfo> info = read_build_info(allPak);
				check(info && info->DefaultScene == scenes[0], "the default scene is in the pack's build info");

				// settings that name an asset the Library does not have fail the pack and leave no file
				BuildSettings broken;
				broken.Scenes = { AssetGuid::generate() };
				WI_ASSET_LOG(Info, "Smoke test: the next pack error is expected");
				const std::filesystem::path brokenDirectory = temp / "broken";
				check(!Packager::pack(database, brokenDirectory, nullptr, &broken), "settings that name an unknown asset fail the pack");
				check(!std::filesystem::exists(brokenDirectory / "assets.wpak"), "a failed pack leaves no file");

				// a cooked file that cannot be read fails the pack when the build needs it, and only then
				const AssetGuid damagedGuid = AssetGuid::generate();
				const std::filesystem::path damaged = database.get_library_root() / "zz" / (damagedGuid.to_string() + ".wasset");
				std::filesystem::create_directories(damaged.parent_path(), error);
				{
					std::ofstream(damaged, std::ios::binary) << "not a cooked asset";
				}

				BuildSettings needsDamaged;
				needsDamaged.AlwaysInclude = { damagedGuid };
				WI_ASSET_LOG(Info, "Smoke test: the next pack error is expected");
				check(!Packager::pack(database, temp / "damaged", nullptr, &needsDamaged), "a damaged cooked file the build needs fails the pack");
				check(!std::filesystem::exists(temp / "damaged" / "assets.wpak"), "and leaves no pack");
				check(Packager::pack(database, temp / "ignored", nullptr, &none), "a damaged cooked file the build does not need is left out");

				std::filesystem::remove_all(damaged.parent_path(), error);
			}
		}

		// 4. damaged packs are rejected without a crash
		{
			std::optional<std::vector<std::byte>> bytes = read_binary_file(temp / "assets.wpak");
			check(bytes.has_value(), "read the pack back");
			if (bytes)
			{
				WI_ASSET_LOG(Info, "Smoke test: the next pack errors are expected");

				const std::filesystem::path truncated = temp / "truncated.wpak";
				{
					std::ofstream out(truncated, std::ios::binary);
					out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size() / 2));
				}
				check(!PakSource(truncated).valid(), "a truncated pack is rejected");

				const PakEntry* first = nullptr;
				for (const auto& [guid, entry] : pak.get_index())
				{
					if (first == nullptr || entry.Offset < first->Offset)
					{
						first = &entry;
					}
				}

				if (first != nullptr)
				{
					std::vector<std::byte> damaged = *bytes;
					damaged[first->Offset + first->CompressedSize / 2] ^= std::byte{ 0x55 };
					const std::filesystem::path corrupt = temp / "corrupt.wpak";
					{
						std::ofstream out(corrupt, std::ios::binary);
						out.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
					}

					PakSource corruptPak(corrupt);
					check(corruptPak.valid() && !corruptPak.read(first->Guid).has_value(), "a flipped byte in a blob is caught");

					manager.set_source(CreateScope<PakSource>(corrupt));
					auto handle = request(manager, static_cast<AssetType>(first->Type), first->Guid);
					manager.wait_idle();
					check(handle() == LoadState::Failed, "loading a damaged asset gives a Failed handle");
				}
			}
		}

		// 5. a forced re-cook keeps every guid and sub-asset id
		manager.set_source(CreateScope<LibrarySource>(database.get_library_root()));
		const auto metasBefore = snapshot_all_metas(database);
		for (const auto& [metaPath, meta] : metasBefore)
		{
			std::filesystem::path source = metaPath;
			source.replace_extension();
			database.reimport(source, true);
		}
		const auto metasAfter = snapshot_all_metas(database);
		check(metasBefore.size() == metasAfter.size(), "no .meta file appeared or disappeared");
		for (const auto& [metaPath, before] : metasBefore)
		{
			auto after = metasAfter.find(metaPath);
			check(after != metasAfter.end() && after->second.Guid == before.Guid && after->second.SubAssets == before.SubAssets,
				"guid and sub-asset ids of " + metaPath.generic_string() + " survive a re-import");
		}

		const std::vector<CookedFile> cookedAfter = read_library(database.get_library_root());
		check(cookedAfter.size() == cooked.size(), "the Library holds the same assets after a re-import");

		std::filesystem::remove_all(temp, error);

		if (check.Failures == 0)
		{
			WI_ASSET_LOG(Info, "Asset smoke test passed");
			return true;
		}

		WI_ASSET_LOG(Error, "Asset smoke test: {0} check(s) failed", check.Failures);
		return false;
	}
}
