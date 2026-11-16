#include <editor_common.h>
#include "importers.h"

#include <cstring>

#include <video/video_factory.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

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
		// platform decoder reads the file at run time, the importer only records what it finds); a file that is not an mp4 fails the import (the previous cook, if any, stays).
		class VideoImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "VideoImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".mp4", ".m4v", ".mov" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = ctx.read_source(false);
				if (!bytes)
				{
					return false;
				}
				if (!looks_like_video(*bytes))
				{
					WI_ASSET_LOG(Error, "{0} is not an mp4, m4v or mov file", file);
					return false;
				}

				VideoData data;
				data.Name = ctx.source().stem().string();

				// What the platform decoder finds in it. A file it cannot read is still cooked (another platform, or a codec that is installed later,
				// may read it); the widget reports the reason when it plays.
				std::string probeError;
				data.HasInfo = probe_video(*bytes, data.Info, probeError);
				if (!data.HasInfo)
				{
					WI_ASSET_LOG(Warning, "{0}: the platform decoder cannot read it: {1}", file, probeError);
				}
				data.Bytes = std::move(*bytes);
				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_video_importer() { return CreateScope<VideoImporter>(); }
}
