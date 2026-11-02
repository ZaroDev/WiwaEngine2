#include <editor_common.h>
#include "importers.h"

#include <ui/ui_panel_io.h>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// .wuipanel: the JSON source of a UI panel, cooked into a UiPanelData. The assets its widgets use (fonts, textures) are the
		// dependencies of the cooked panel, the ones that exist: a reference to a deleted asset stays in the panel and the widget
		// draws without it.
		class UiPanelImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "UiPanelImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wuipanel" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				std::string error;
				const std::optional<UiPanelData> panel = ui_panel_from_text(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
				if (!panel)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				std::vector<AssetRef> dependencies;
				for (const UiWidget& widget : panel->Widgets)
				{
					for (const UiField& field : ui_fields())
					{
						if (field.Type != FieldType::Asset)
						{
							continue;
						}

						const AssetRef ref = std::get<AssetRef>(field.Get(widget));
						if (ref.valid() && ctx.asset_exists(ref) && std::find(dependencies.begin(), dependencies.end(), ref) == dependencies.end())
						{
							dependencies.push_back(ref);
						}
					}
				}

				ctx.emit(ctx.main_asset(), *panel, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_ui_panel_importer() { return CreateScope<UiPanelImporter>(); }
}
