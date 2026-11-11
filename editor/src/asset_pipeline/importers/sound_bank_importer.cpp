#include <editor_common.h>
#include "importers.h"

#include <algorithm>
#include <cctype>

#include <audio/audio_bank_info.h>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// The SoundbanksInfo.json Wwise writes next to the banks (any letter case); empty path if there is none.
		std::filesystem::path find_info_file(const std::filesystem::path& folder)
		{
			std::error_code error;
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
			{
				std::string name = entry.path().filename().string();
				std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (name == "soundbanksinfo.json")
				{
					return entry.path();
				}
			}
			return {};
		}

		// .bnk: a Wwise soundbank. The bytes are cooked as they are (the backend loads them from memory); the metadata of what is inside
		// (events, RTPCs, switches...) comes from the SoundbanksInfo.json next to it, which is a source dependency so that a new bank generation
		// cooks the asset again. No JSON, or one that cannot be read or does not list the bank: the bank is still cooked, without metadata.
		class SoundBankImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "SoundBankImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".bnk" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes || bytes->empty())
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				SoundBankData data;
				data.Name = ctx.source().stem().string();
				data.Bytes = std::move(*bytes);

				const std::filesystem::path infoFile = find_info_file(ctx.source().parent_path());
				if (infoFile.empty())
				{
					ctx.warn(file + ": no SoundbanksInfo.json next to the bank, so it has no event list");
				}
				else
				{
					ctx.add_source_dependency(infoFile);
					std::optional<std::vector<std::byte>> text = read_binary_file(infoFile);
					std::string error;
					std::optional<AudioBankInfo> info;
					if (text)
					{
						info = parse_soundbanks_info(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), data.Name, error);
					}
					else
					{
						error = "cannot read the file";
					}

					if (info)
					{
						data.Info = std::move(*info);
						data.HasInfo = true;
					}
					else
					{
						ctx.warn(file + ": " + infoFile.filename().string() + " was not used: " + error);
					}
				}

				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_sound_bank_importer() { return CreateScope<SoundBankImporter>(); }
}
