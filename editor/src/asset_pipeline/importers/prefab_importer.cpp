#include <editor_common.h>
#include "importers.h"

#include <scene/scene_records.h>

#include "../prefab_file.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wprefab: the entities of a subtree as scene JSON, cooked into a PrefabData. The assets the components refer to are the
		// prefab's dependencies (the ones that exist: a reference to a deleted asset stays in the prefab and is dealt with when it loads).
		class PrefabImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "PrefabImporter"; }
			virtual u32 version() const override { return 2; }
			virtual std::vector<std::string> extensions() const override { return { ".wprefab" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::string error;
				std::vector<std::string> warnings;
				std::optional<std::vector<EntityRecord>> entities = read_prefab_file(ctx.source(), error, &warnings);
				for (const std::string& warning : warnings)
				{
					ctx.warn(file + ": " + warning);
				}
				if (!entities)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				std::optional<PrefabData> prefab = prefab_from_records(*entities, ctx.source().stem().string(), error);
				if (!prefab)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				for (const PrefabNode& node : prefab->Nodes)
				{
					if (node.Instance && node.Instance->Prefab.Guid == ctx.main_asset().Guid)
					{
						WI_ASSET_LOG(Error, "{0}: a prefab cannot hold an instance of itself", file);
						return false;
					}
				}

				std::vector<AssetRef> dependencies;
				for (const AssetRef& ref : collect_references(*entities))
				{
					if (ctx.asset_exists(ref))
					{
						dependencies.push_back(ref);
					}
				}

				ctx.emit(ctx.main_asset(), *prefab, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_prefab_importer() { return CreateScope<PrefabImporter>(); }
}
