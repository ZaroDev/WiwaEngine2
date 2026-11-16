#include <editor_common.h>
#include "importers.h"

#include <nav/nav_mesh_asset.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wnav: a navmesh baked by the editor (nav_bake.h). Cooked as the NavMeshData it holds; a file that does not read keeps the previous cook.
		class NavMeshImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "NavMeshImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wnav" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				NavMeshData data;
				std::string error;
				if (!nav_mesh_from_bytes(*bytes, data, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_nav_mesh_importer() { return CreateScope<NavMeshImporter>(); }
}
