#include <editor_common.h>
#include "importers.h"

#include "../file_utils.h"
#include "../physics_material_file.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wphysmat: the JSON source of a physics material, cooked into a PhysicsMaterialData. A file that cannot be parsed or holds a
		// value that is not allowed fails the import, which keeps the previously cooked material (or none).
		class PhysicsMaterialImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "PhysicsMaterialImporter"; }
			virtual u32 version() const override { return 2; } // 2: tireGrip
			virtual std::vector<std::string> extensions() const override { return { ".wphysmat" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				PhysicsMaterialData data;
				std::string error;
				if (!physics_material_from_text(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()), data, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_physics_material_importer() { return CreateScope<PhysicsMaterialImporter>(); }
}
