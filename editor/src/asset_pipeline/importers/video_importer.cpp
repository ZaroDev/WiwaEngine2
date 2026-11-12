#include <editor_common.h>
#include "importers.h"

#include <cstring>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Whether the bytes start like an ISO base media file (mp4, m4v, mov): a box whose type, four bytes in, is ftyp (or moov / mdat / free /
		// wide / skip, which some QuickTime files start with).
		bool looks_like_video(const std::vector<std::byte>& bytes)
		{
			if (bytes.size() < 12)
			{
				return false;
			}
			const unsigned char* b = reinterpret_cast<const unsigned char*>(bytes.data());
			for (const char* type : { "ftyp", "moov", "mdat", "free", "wide", "skip" })
			{
				if (std::memcmp(b + 4, type, 4) == 0)
				{
					return true;
				}
			}
			return false;
		}

		// .mp4, .m4v, .mov: a video the video system plays in a UI Video widget. The bytes are cooked as they are (nothing is transcoded: the
		// platform decoder reads the file at run time); a file that is not an mp4 fails the import (the previous cook, if any, stays).
		class VideoImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "VideoImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".mp4", ".m4v", ".mov" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes || bytes->empty())
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}
				if (!looks_like_video(*bytes))
				{
					WI_ASSET_LOG(Error, "{0} is not an mp4, m4v or mov file", file);
					return false;
				}

				VideoData data;
				data.Name = ctx.source().stem().string();
				data.Bytes = std::move(*bytes);
				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_video_importer() { return CreateScope<VideoImporter>(); }
}
