#include <editor_common.h>
#include "importers.h"

#include <input/input_map_io.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .winput: the JSON source of the action maps (input/input_map_io.h), cooked into an InputMapData. A file that cannot be parsed, or
		// names a control that does not exist, fails the import, which keeps the previously cooked map (or none).
		class InputMapImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "InputMapImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".winput" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				InputMapData data;
				std::string error;
				if (!input_map_from_text(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()), data, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_input_map_importer() { return CreateScope<InputMapImporter>(); }
}
