#include <editor_common.h>
#include "importers.h"

#include <cstring>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Whether the bytes start like a file the MiniAudio backend can decode: RIFF/WAVE, FLAC, Ogg, or MP3 (an ID3 tag or an MPEG frame sync).
		bool looks_like_audio(const std::vector<std::byte>& bytes)
		{
			if (bytes.size() < 12)
			{
				return false;
			}
			const unsigned char* b = reinterpret_cast<const unsigned char*>(bytes.data());
			const bool wav = std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "WAVE", 4) == 0;
			const bool flac = std::memcmp(b, "fLaC", 4) == 0;
			const bool id3 = std::memcmp(b, "ID3", 3) == 0;
			const bool mpeg = b[0] == 0xFF && (b[1] & 0xE0) == 0xE0;
			const bool ogg = std::memcmp(b, "OggS", 4) == 0;
			return wav || flac || id3 || mpeg || ogg;
		}

		// .wav, .mp3, .flac, .ogg: a sound the MiniAudio backend plays under the file name (without extension) as the event name. The bytes are cooked as
		// they are; a file that is not really a sound fails the import (the previous cook, if any, stays).
		class AudioClipImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "AudioClipImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wav", ".mp3", ".flac", ".ogg" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes || bytes->empty())
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}
				if (!looks_like_audio(*bytes))
				{
					WI_ASSET_LOG(Error, "{0} is not a wav, mp3, flac or ogg file", file);
					return false;
				}

				AudioClipData data;
				data.Name = ctx.source().stem().string();
				data.Bytes = std::move(*bytes);
				ctx.emit(ctx.main_asset(), data, {});
				return true;
			}
		};
	}

	Scope<IImporter> create_audio_clip_importer() { return CreateScope<AudioClipImporter>(); }
}
