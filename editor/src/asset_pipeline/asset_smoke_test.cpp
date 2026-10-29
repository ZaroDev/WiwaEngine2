#include <editor_common.h>
#include "asset_smoke_test.h"

#include <assets/asset_manager.h>
#include <assets/library_source.h>
#include <assets/pak_source.h>
#include <core/engine.h>

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
		check(pak.get_index().size() == cooked.size(), "the pack has one entry per cooked asset");
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

			// the same guid asked for as the wrong type fails, and a missing guid fails
			WI_ASSET_LOG(Info, "Smoke test: the next two load errors are expected");
			const AssetType other = (cooked[0].Type == AssetType::Mesh) ? AssetType::Texture : AssetType::Mesh;
			check(request(manager, other, cooked[0].Guid)() == LoadState::Failed, "a wrong-type load fails");
			auto missing = request(manager, AssetType::Texture, AssetGuid::generate());
			manager.wait_idle();
			check(missing() == LoadState::Failed, "a missing asset fails");
		}
		manager.unload_unused();

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
