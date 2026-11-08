#include <editor_common.h>
#include "importers.h"

#include <scene/scene_records.h>

#include "../../scene/scene_json.h"
#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// .wscene: the JSON source of a scene, cooked into a SceneData. The cooked scene lists the assets it refers to as
		// dependencies (the ones that exist: a reference to a deleted asset stays in the scene and is dealt with when it loads).
		class SceneImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "SceneImporter"; }
			virtual u32 version() const override { return 6; } // 6: dependencies follow the redirects of extracted model assets; 5: the physics settings; 4: scripts: the scene-level list and script components as dependencies
			virtual std::vector<std::string> extensions() const override { return { ".wscene" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				const char* begin = reinterpret_cast<const char*>(bytes->data());
				SceneJson json = SceneJson::parse(begin, begin + bytes->size(), nullptr, false);
				if (json.is_discarded())
				{
					WI_ASSET_LOG(Error, "{0}: not valid JSON", file);
					return false;
				}

				SceneJsonResult result = scene_from_json(json);
				for (const std::string& warning : result.Warnings)
				{
					ctx.warn(file + ": " + warning);
				}
				if (!result.Scene)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, result.Error);
					return false;
				}

				if (result.Scene->Name.empty())
				{
					result.Scene->Name = ctx.source().stem().string();
				}

				std::vector<AssetRef> dependencies;
				const std::unordered_map<AssetGuid, AssetGuid> redirects = ctx.redirect_map();
				for (const AssetRef& ref : collect_references(*result.Scene))
				{
					if (ctx.asset_exists(ref))
					{
						// the scene keeps its reference (it loads through the redirect); what the pack needs is the file it leads to
						const auto moved = redirects.find(ref.resolved());
						dependencies.push_back(moved == redirects.end() ? ref : AssetRef{ moved->second, 0 });
					}
				}

				ctx.emit(ctx.main_asset(), *result.Scene, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_scene_importer() { return CreateScope<SceneImporter>(); }
}
