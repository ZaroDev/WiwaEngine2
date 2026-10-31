#include <editor_common.h>
#include "asset_smoke_test.h"

#include <assets/asset_manager.h>
#include <assets/build_info.h>
#include <assets/library_source.h>
#include <assets/pak_source.h>
#include <core/engine.h>

#include "build_settings.h"
#include "file_utils.h"
#include "material_file.h"
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
			case AssetType::Script: { auto h = manager.load<ScriptData>(ref); return [h]() { return h.state(); }; }
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

		// 6. material assets (.wmat)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_material_smoke";
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}

			const auto write_text = [](const std::filesystem::path& path, const std::string& text)
			{
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				out << text;
			};

			// the cooked material of a source file, straight from the Library
			const auto cooked_material = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr) -> std::optional<MaterialData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == meta->Guid && file.Type == AssetType::Material && parse_wasset_header(file.Bytes, payload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = file.Dependencies;
						}
						return deserialize<MaterialData>(payload);
					}
				}
				return std::nullopt;
			};

			const auto full_material = [](double roughness)
			{
				nlohmann::json json = {
					{ "version", 1 }, { "name", "Smoke" }, { "baseColor", { 0.1, 0.2, 0.3, 0.4 } }, { "metallic", 0.6 }, { "roughness", roughness },
					{ "emissive", { 1.0, 2.0, 3.0 } }, { "alpha", "mask" }, { "alphaCutoff", 0.25 }, { "doubleSided", true },
					{ "clearcoat", 0.7 }, { "clearcoatRoughness", 0.15 }, { "sheen", 0.45 }, { "sheenRoughness", 0.55 }, { "transmission", 0.65 },
					{ "textures", { { "baseColor", "pixel.png" }, { "normal", "pixel.png" }, { "orm", "pixel.png" }, { "emissive", "pixel.png" } } } };
				return json.dump(2);
			};

			const std::filesystem::path wmat = dir / "smoke.wmat";
			write_text(wmat, full_material(0.35));
			database.scan();

			std::vector<AssetGuid> dependencies;
			std::optional<MaterialData> material = cooked_material(wmat, &dependencies);
			check(material.has_value(), "material_import_all_fields: the .wmat is cooked");
			if (material)
			{
				check(material->Name == "Smoke" && material->BaseColorFactor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f) && material->MetallicFactor == 0.6f &&
					material->RoughnessFactor == 0.35f && material->EmissiveFactor == glm::vec3(1.0f, 2.0f, 3.0f), "material_import_all_fields: name, color, metallic, roughness and emissive");
				check(material->Alpha == AlphaMode::Mask && material->AlphaCutoff == 0.25f && material->DoubleSided, "material_import_all_fields: alpha mode, cutoff and double sided");
				check(material->Clearcoat == 0.7f && material->ClearcoatRoughness == 0.15f && material->SheenIntensity == 0.45f &&
					material->SheenRoughness == 0.55f && material->Transmission == 0.65f, "material_import_all_fields: clearcoat, sheen and transmission");
				check(material->BaseColorTexture.valid() && material->NormalTexture.valid() && material->OrmTexture.valid() && material->EmissiveTexture.valid(), "material_import_all_fields: the four textures resolve");
				check(std::find(dependencies.begin(), dependencies.end(), material->BaseColorTexture.resolved()) != dependencies.end(), "material_import_all_fields: the texture is a dependency");
			}

			write_text(wmat, full_material(0.9));
			database.reimport(wmat);
			material = cooked_material(wmat);
			check(material && material->RoughnessFactor == 0.9f, "material_reimport_on_edit: the cooked roughness follows the edit");

			WI_ASSET_LOG(Info, "Smoke test: the next material import error is expected");
			write_text(wmat, "{");
			database.reimport(wmat);
			material = cooked_material(wmat);
			check(material && material->RoughnessFactor == 0.9f && material->Name == "Smoke", "material_bad_json_keeps_previous: the previous cooked data still loads");

			const std::filesystem::path missing = dir / "missing_texture.wmat";
			write_text(missing, R"({ "version": 1, "roughness": 0.5, "textures": { "baseColor": "no_such_texture.png" } })");
			WI_ASSET_LOG(Info, "Smoke test: the next material warning is expected");
			database.reimport(missing);
			material = cooked_material(missing);
			check(material && !material->BaseColorTexture.valid() && material->RoughnessFactor == 0.5f, "material_missing_texture_warns: imports with an invalid texture ref");

			const std::filesystem::path unknown = dir / "unknown.wmat";
			write_text(unknown, R"({ "version": 1, "roughness": 0.5, "shader": "x.wshader", "params": { "Wobble": 0.5 }, "custom": { "a": [1, 2] }, "zzz": 3 })");
			MaterialFileData first, second;
			std::string fileError;
			check(read_material_file(unknown, first, fileError), "material_roundtrip_keeps_unknown_keys: read");
			check(write_material_file(dir / "copy.wmat", first, fileError), "material_roundtrip_keeps_unknown_keys: write");
			check(read_material_file(dir / "copy.wmat", second, fileError), "material_roundtrip_keeps_unknown_keys: read back");
			check(second.Extra == first.Extra && second.Extra.contains("custom") && second.Extra.contains("zzz") && second.Extra["custom"]["a"].size() == 2,
				"material_roundtrip_keeps_unknown_keys: the unknown keys survive");
			check(second.ShaderPath == "x.wshader" && second.Params.contains("Wobble") && second.Material.RoughnessFactor == 0.5f, "material_roundtrip_keeps_unknown_keys: known keys survive");
			check(!read_material_file(dir / "nope.wmat", first, fileError) && !fileError.empty(), "a missing .wmat is an error");

			// material_create_unique_names: three creations give three distinct files holding the defaults
			{
				std::filesystem::path created[3];
				for (std::filesystem::path& path : created)
				{
					path = create_material_file(dir, "New material");
				}
				std::error_code existsError;
				const bool distinct = created[0] != created[1] && created[1] != created[2] && created[0] != created[2];
				check(distinct && !created[0].empty() && !created[1].empty() && !created[2].empty(), "material_create_unique_names: three distinct paths");
				check(created[0].filename() == "New material.wmat" && created[1].filename() == "New material 2.wmat" && created[2].filename() == "New material 3.wmat",
					"material_create_unique_names: the names count up");
				for (const std::filesystem::path& path : created)
				{
					MaterialFileData read;
					std::string createError;
					const MaterialData defaults{};
					const bool ok = !path.empty() && std::filesystem::exists(path, existsError) && read_material_file(path, read, createError);
					check(ok && read.Material.BaseColorFactor == defaults.BaseColorFactor && read.Material.RoughnessFactor == defaults.RoughnessFactor &&
						read.Material.MetallicFactor == defaults.MetallicFactor && read.Material.AlphaCutoff == defaults.AlphaCutoff &&
						read.Material.Alpha == defaults.Alpha && read.ShaderPath.empty() && read.BaseColorPath.empty(), "material_create_unique_names: the file reads back as the defaults");
				}
			}

			// material_write_clean_floats: 0.35f is written as 0.35, and no temporary file is left behind
			{
				MaterialFileData data;
				data.Material.RoughnessFactor = 0.35f;
				data.Material.BaseColorFactor = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
				const std::filesystem::path clean = dir / "clean.wmat";
				std::string cleanError;
				check(write_material_file(clean, data, cleanError), "material_write_clean_floats: write");
				std::ifstream in(clean, std::ios::binary);
				const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				check(text.find("0.35,") != std::string::npos && text.find("0.3499") == std::string::npos && text.find("0.2,") != std::string::npos &&
					text.find("0.1,") != std::string::npos, "material_write_clean_floats: floats are written with their shortest form");
				bool leftover = false;
				for (const auto& entry : std::filesystem::directory_iterator(dir, error))
				{
					leftover = leftover || entry.path().extension() == ".tmp";
				}
				check(!leftover, "material_write_atomic: no temporary file is left behind");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}


		// 7. model materials become .wmat files
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_model_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}

			const auto write_text = [](const std::filesystem::path& path, const std::string& text)
			{
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				out << text;
			};
			const auto read_text = [](const std::filesystem::path& path)
			{
				std::ifstream in(path, std::ios::binary);
				return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			};

			// a glTF with one triangle per material; the first material has a texture
			const auto model_text = [](const std::vector<std::string>& names)
			{
				nlohmann::json primitives = nlohmann::json::array();
				nlohmann::json materials = nlohmann::json::array();
				for (size_t i = 0; i < names.size(); i++)
				{
					primitives.push_back({ { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "material", i } });
					nlohmann::json material = { { "name", names[i] }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.25, 0.5, 0.75, 1.0 } }, { "roughnessFactor", 0.375 }, { "metallicFactor", 0.125 } } } };
					if (i == 0)
					{
						material["pbrMetallicRoughness"]["baseColorTexture"] = { { "index", 0 } };
					}
					else
					{
						material["doubleSided"] = true;
						material["emissiveFactor"] = { 0.5, 0.25, 0.125 };
					}
					materials.push_back(material);
				}
				nlohmann::json root = {
					{ "asset", { { "version", "2.0" } } }, { "scene", 0 }, { "scenes", { { { "nodes", { 0 } } } } },
					{ "nodes", { { { "name", "Body" }, { "mesh", 0 } } } },
					{ "meshes", { { { "primitives", primitives } } } },
					{ "materials", materials },
					{ "textures", { { { "source", 0 } } } }, { "images", { { { "uri", "pixel.png" } } } },
					{ "accessors", { { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" }, { "min", { 0, 0, 0 } }, { "max", { 1, 1, 0 } } },
						{ { "bufferView", 1 }, { "componentType", 5123 }, { "count", 3 }, { "type", "SCALAR" } } } },
					{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 } }, { { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 6 } } } },
					{ "buffers", { { { "byteLength", 44 }, { "uri", "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=" } } } } };
				return root.dump(2);
			};

			// the cooked asset with this guid
			const auto cooked_payload = [&](AssetGuid guid, AssetType type, std::vector<std::byte>& storage) -> std::span<const std::byte>
			{
				for (CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == guid && file.Type == type)
					{
						storage = std::move(file.Bytes);
						if (parse_wasset_header(storage, payload))
						{
							return payload;
						}
					}
				}
				return {};
			};

			// the material guid of each primitive of the first mesh of a model
			const auto primitive_materials = [&](const std::filesystem::path& modelFile) -> std::vector<AssetGuid>
			{
				std::vector<AssetGuid> result;
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(modelFile));
				if (!meta)
				{
					return result;
				}
				std::vector<std::byte> prefabBytes, meshBytes;
				std::optional<PrefabData> prefab = deserialize<PrefabData>(cooked_payload(meta->Guid, AssetType::Prefab, prefabBytes));
				if (!prefab)
				{
					return result;
				}
				for (const PrefabNode& node : prefab->Nodes)
				{
					if (node.Mesh.valid())
					{
						std::optional<MeshData> mesh = deserialize<MeshData>(cooked_payload(node.Mesh.resolved(), AssetType::Mesh, meshBytes));
						for (const MeshPrimitive& primitive : mesh ? mesh->Primitives : std::vector<MeshPrimitive>{})
						{
							result.push_back(primitive.Material.valid() ? primitive.Material.resolved() : AssetGuid{});
						}
						break;
					}
				}
				return result;
			};

			const auto guid_of = [](const std::filesystem::path& source) -> AssetGuid
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				return meta ? meta->Guid : AssetGuid{};
			};

			// model_extracts_materials
			const std::filesystem::path model = dir / "m.gltf";
			write_text(model, model_text({ "Red", "Blue" }));
			database.scan();
			const std::filesystem::path materialsDir = dir / "m_materials";
			const std::filesystem::path red = materialsDir / "Red.wmat";
			const std::filesystem::path blue = materialsDir / "Blue.wmat";
			{
				std::error_code existsError;
				check(std::filesystem::exists(red, existsError) && std::filesystem::exists(blue, existsError), "model_extracts_materials: one .wmat per material");
				MaterialFileData redData, blueData;
				std::string readError;
				const bool readRed = read_material_file(red, redData, readError);
				const bool readBlue = read_material_file(blue, blueData, readError);
				check(readRed && readBlue, "model_extracts_materials: both files read back");
				if (readRed && readBlue)
				{
					check(redData.Material.Name == "Red" && redData.Material.BaseColorFactor == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f) && redData.Material.RoughnessFactor == 0.375f &&
						redData.Material.MetallicFactor == 0.125f, "model_extracts_materials: the factors of the model");
					check(blueData.Material.DoubleSided && blueData.Material.EmissiveFactor == glm::vec3(0.5f, 0.25f, 0.125f), "model_extracts_materials: double sided and emissive");
					std::error_code pathError;
					check(!redData.BaseColorPath.empty() && std::filesystem::equivalent(materialsDir / redData.BaseColorPath, dir / "pixel.png", pathError) && blueData.BaseColorPath.empty(),
						"model_extracts_materials: the texture is written as a path to the image");
				}

				const std::vector<AssetGuid> used = primitive_materials(model);
				check(used.size() == 2 && used[0].valid() && used[0] == guid_of(red) && used[1].valid() && used[1] == guid_of(blue), "model_extracts_materials: the mesh primitives refer to the files");
				std::vector<std::byte> materialBytes;
				std::optional<MaterialData> cooked = deserialize<MaterialData>(cooked_payload(guid_of(red), AssetType::Material, materialBytes));
				check(cooked && cooked->RoughnessFactor == 0.375f && cooked->BaseColorTexture.valid(), "model_extracts_materials: the file is cooked with its texture");
			}

			// model_reimport_keeps_edits
			{
				MaterialFileData edited;
				std::string editError;
				check(read_material_file(red, edited, editError), "model_reimport_keeps_edits: read");
				edited.Material.RoughnessFactor = 0.9f;
				check(write_material_file(red, edited, editError), "model_reimport_keeps_edits: write");
				const std::string before = read_text(red);

				write_text(model, model_text({ "Red", "Blue" }) + "\n");
				database.reimport(model);

				MaterialFileData after;
				check(read_material_file(red, after, editError) && after.Material.RoughnessFactor == 0.9f && read_text(red) == before, "model_reimport_keeps_edits: the edited file is unchanged");
			}

			// model_material_names_safe
			const std::filesystem::path named = dir / "n.gltf";
			write_text(named, model_text({ "a/b", "", "Mat", "Mat" }));
			database.scan();
			const std::filesystem::path namedDir = dir / "n_materials";
			const std::vector<std::filesystem::path> expectedNames = { namedDir / "a_b.wmat", namedDir / "material.wmat", namedDir / "Mat.wmat", namedDir / "Mat 2.wmat" };
			{
				std::error_code existsError;
				size_t files = 0;
				for (const auto& entry : std::filesystem::directory_iterator(namedDir, existsError))
				{
					files += entry.path().extension() == ".wmat" ? 1 : 0;
				}
				bool all = true;
				for (const std::filesystem::path& path : expectedNames)
				{
					all = all && std::filesystem::exists(path, existsError);
				}
				check(all && files == 4, "model_material_names_safe: four distinct safe files");

				const std::vector<AssetGuid> used = primitive_materials(named);
				bool distinct = used.size() == 4;
				for (size_t i = 0; distinct && i < used.size(); i++)
				{
					distinct = used[i].valid() && used[i] == guid_of(expectedNames[i]);
				}
				check(distinct, "model_material_names_safe: each primitive refers to its own file");
			}

			// model_removed_material_keeps_file: a material the model no longer has leaves its file, and the others keep theirs
			{
				write_text(named, model_text({ "", "Mat", "Mat" }));
				database.reimport(named);
				std::error_code existsError;
				bool all = true;
				for (const std::filesystem::path& path : expectedNames)
				{
					all = all && std::filesystem::exists(path, existsError);
				}
				check(all, "model_removed_material_keeps_file: every file is still there");
				const std::vector<AssetGuid> used = primitive_materials(named);
				check(used.size() == 3 && used[0] == guid_of(expectedNames[1]) && used[1] == guid_of(expectedNames[2]) && used[2] == guid_of(expectedNames[3]),
					"model_removed_material_keeps_file: the remaining materials keep their files");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

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
