#include <editor_common.h>
#include "demo_video_writer.h"

#include <asset_pipeline/asset_database.h>
#include <video/mf/mf_clip_writer.h>

namespace wiwa
{
	bool run_write_demo_video(AssetPipelineModule& pipeline)
	{
		const std::filesystem::path file = pipeline.get_database().get_assets_root() / "video" / "Intro.mp4";
		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		MfClipConfig config;
		config.Width = 640;
		config.Height = 360;
		config.Fps = 30;
		config.Seconds = 6.0;

		std::string message;
		if (!write_mf_test_clip(file, config, message))
		{
			WI_LOG(Error, "Video", "Could not write the demo clip {0}: {1}", file.string(), message);
			return false;
		}
		WI_LOG(Info, "Video", "Wrote the demo clip {0} ({1}x{2}, {3} s)", file.string(), config.Width, config.Height, config.Seconds);
		return true;
	}
}
