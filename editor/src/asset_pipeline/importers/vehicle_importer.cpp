#include <editor_common.h>
#include "importers.h"

#include "../file_utils.h"
#include "../vehicle_file.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wvehicle: the JSON source of a vehicle's tuning, cooked into a VehicleSettingsData. A file that cannot be parsed or holds a value
		// that is not allowed fails the import, which keeps the previously cooked vehicle (or none).
		class VehicleImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "VehicleImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wvehicle" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				VehicleSettingsData data;
				std::string error;
				if (!vehicle_from_text(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()), data, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_vehicle_importer() { return CreateScope<VehicleImporter>(); }
}
