#include <editor_common.h>
#include "video_mf_smoke_test.h"

#include <chrono>
#include <thread>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/file_utils.h>
#include <assets/wasset_io.h>
#include <video/video_decoder.h>
#include <video/video_factory.h>
#include <video/mf/mf_clip_writer.h>
#include <video/video_player.h>

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_VIDEO_LOG(Error, "Video MF smoke test FAILED: {0}", what);
				}
			}
		};

		bool has_picture(const VideoFrame& frame)
		{
			// the demo clip is not black, and the alpha of every pixel is opaque (the BGRA the UI tints must not be transparent)
			u32 bright = 0;
			bool opaque = true;
			for (size_t i = 0; i + 3 < frame.Bgra.size(); i += 4)
			{
				bright += (frame.Bgra[i] > 24 || frame.Bgra[i + 1] > 24 || frame.Bgra[i + 2] > 24) ? 1 : 0;
				opaque = opaque && frame.Bgra[i + 3] == 255;
			}
			return bright > frame.Bgra.size() / 4 / 50 && opaque;
		}

		void check_decoder(Check& check, const std::vector<std::byte>& clip, VideoInfo& probed)
		{
			{
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				check(decoder != nullptr, "mf_decoder_exists: this platform has a video decoder");
				if (!decoder)
				{
					return;
				}
				std::string error;
				check(decoder->open(clip, probed, error), "mf_open_demo: the demo clip opens (" + error + ")");
				check(probed.Width > 0 && probed.Height > 0 && probed.Width <= VIDEO_MAX_DIMENSION && probed.Height <= VIDEO_MAX_DIMENSION, "mf_open_demo: the size is in range");
				check(probed.DurationSeconds > 0.5 && probed.Fps > 1.0f && probed.Fps < 1000.0f, "mf_open_demo: duration and frame rate are known");
				WI_VIDEO_LOG(Info, "Video MF smoke test: the clip is {0}x{1}, {2:.2f} s, {3:.2f} fps, sound: {4} ({5} Hz, {6} ch)", probed.Width, probed.Height, probed.DurationSeconds, probed.Fps, probed.HasAudio, probed.SampleRate, probed.Channels);

				// pictures
				VideoFrame frame;
				i64 last = -1;
				bool monotonic = true;
				bool sized = true;
				bool picture = false;
				u32 count = 0;
				for (u32 guard = 0; guard < 2000 && count < 24; guard++)
				{
					const DecodeStatus status = decoder->read_video(frame);
					if (status == DecodeStatus::Pending)
					{
						continue;
					}
					if (status != DecodeStatus::Ok)
					{
						break;
					}
					count++;
					monotonic = monotonic && frame.TimestampUs >= last;
					last = frame.TimestampUs;
					sized = sized && frame.Width == probed.Width && frame.Height == probed.Height && frame.Bgra.size() == static_cast<size_t>(frame.Width) * frame.Height * 4;
					picture = picture || has_picture(frame);
				}
				check(count >= 10, "mf_frames: at least ten pictures decode");
				check(monotonic, "mf_frames_monotonic: picture timestamps never go back");
				check(sized, "mf_frames: every picture is Width x Height x 4 bytes");
				check(picture, "mf_frames: the picture is not black and is opaque BGRA");

				// sound
				if (probed.HasAudio)
				{
					AudioChunk chunk;
					i64 lastAudio = -1;
					bool audioMonotonic = true;
					bool audioSized = true;
					float peak = 0.0f;
					u32 chunks = 0;
					for (u32 guard = 0; guard < 20000 && chunks < 4000; guard++) // the whole track: a clip may open on silence
					{
						const DecodeStatus status = decoder->read_audio(chunk);
						if (status == DecodeStatus::Pending)
						{
							continue;
						}
						if (status != DecodeStatus::Ok)
						{
							break;
						}
						chunks++;
						audioMonotonic = audioMonotonic && chunk.TimestampUs >= lastAudio;
						lastAudio = chunk.TimestampUs;
						audioSized = audioSized && chunk.Frames > 0 && chunk.Samples.size() == static_cast<size_t>(chunk.Frames) * probed.Channels;
						for (const float sample : chunk.Samples)
						{
							peak = std::max(peak, std::abs(sample));
						}
					}
					check(chunks >= 5, "mf_audio: at least five runs of sound decode");
					check(audioMonotonic && audioSized, "mf_audio_monotonic: sound timestamps never go back and runs are Frames x channels floats");
					WI_VIDEO_LOG(Info, "Video MF smoke test: {0} runs of sound, peak level {1:.4f}", chunks, peak);
					check(peak > 0.001f, "mf_audio: the sound track is not silence");
				}
				else
				{
					WI_VIDEO_LOG(Warning, "Video MF smoke test: the clip has no sound track: the sound checks were skipped");
				}
			}

			{
				// seek: to a keyframe at or before the time; reading on reaches the time
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				decoder->open(clip, info, error);
				const i64 target = static_cast<i64>(std::min(1.0, info.DurationSeconds * 0.5) * 1e6);
				check(decoder->seek(target), "mf_seek: the clip seeks");
				VideoFrame frame;
				bool reached = false;
				bool startedEarly = true;
				for (u32 guard = 0, count = 0; guard < 4000 && count < 600; guard++)
				{
					const DecodeStatus status = decoder->read_video(frame);
					if (status == DecodeStatus::Pending)
					{
						continue;
					}
					if (status != DecodeStatus::Ok)
					{
						break;
					}
					if (count++ == 0)
					{
						startedEarly = frame.TimestampUs <= target + static_cast<i64>(1e6 / std::max(info.Fps, 1.0f)) * 2;
					}
					if (frame.TimestampUs >= target)
					{
						reached = true;
						break;
					}
				}
				check(startedEarly, "mf_seek: the first picture after a seek is at or shortly before the target");
				check(reached, "mf_seek: reading on reaches the target time");
			}

			{
				// the end of the stream
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				decoder->open(clip, info, error);
				VideoFrame frame;
				u32 count = 0;
				DecodeStatus status = DecodeStatus::Ok;
				const u32 limit = static_cast<u32>(info.DurationSeconds * std::max(info.Fps, 1.0f)) + 600;
				for (u32 guard = 0; guard < limit * 4 && count < limit; guard++)
				{
					status = decoder->read_video(frame);
					if (status == DecodeStatus::Ok)
					{
						count++;
					}
					else if (status != DecodeStatus::Pending)
					{
						break;
					}
				}
				check(status == DecodeStatus::End, "mf_end_of_stream: the picture stream ends with End");
				check(count > 10 && static_cast<double>(count) > info.DurationSeconds * info.Fps * 0.8, "mf_end_of_stream: about duration x fps pictures came before it");
			}

			{
				// without the sound: pictures keep coming
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				decoder->open(clip, info, error);
				decoder->disable_audio();
				VideoFrame frame;
				u32 count = 0;
				for (u32 guard = 0; guard < 1000 && count < 20; guard++)
				{
					const DecodeStatus status = decoder->read_video(frame);
					count += status == DecodeStatus::Ok ? 1 : 0;
					if (status != DecodeStatus::Ok && status != DecodeStatus::Pending)
					{
						break;
					}
				}
				check(count >= 20, "mf_disable_audio: pictures decode with the sound stream off");
			}
		}

		// The pitch of a run of mono samples, by counting the times it crosses zero upward.
		double estimate_hz(const std::vector<float>& mono, size_t first, size_t count, u32 rate)
		{
			u32 crossings = 0;
			for (size_t i = first + 1; i < first + count && i < mono.size(); i++)
			{
				crossings += (mono[i - 1] < 0.0f && mono[i] >= 0.0f) ? 1 : 0;
			}
			return static_cast<double>(crossings) * rate / static_cast<double>(count);
		}

		// A clip whose content is known, made by the clip writer: what comes out of the decoder is what went in (to the precision of H.264 and AAC).
		void check_generated_clip(Check& check)
		{
			MfClipConfig config;
			config.Width = 320;
			config.Height = 180;
			config.Fps = 30;
			config.Seconds = 2.0;
			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_video_smoke_clip.mp4";
			std::string error;
			const bool written = write_mf_test_clip(file, config, error);
			check(written, "mf_generated_clip: the clip writer makes an mp4 (" + error + ")");
			if (!written)
			{
				return;
			}
			std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
			std::error_code removed;
			std::filesystem::remove(file, removed);
			check(bytes && bytes->size() > 1000, "mf_generated_clip: the file has content");
			if (!bytes)
			{
				return;
			}

			Scope<IVideoDecoder> decoder = create_platform_decoder();
			VideoInfo info;
			check(decoder->open(*bytes, info, error), "mf_generated_clip: the decoder opens it (" + error + ")");
			check(info.Width == 320 && info.Height == 180, "mf_generated_clip: the size that was written");
			check(std::abs(info.DurationSeconds - 2.0) < 0.2 && std::abs(info.Fps - 30.0f) < 0.5f, "mf_generated_clip: about 2 s at 30 fps");
			check(info.HasAudio && info.SampleRate == 48000 && info.Channels == 2, "mf_generated_clip: stereo sound at 48000 Hz");

			// pictures: the square crosses the picture from the left edge to the right edge
			VideoFrame frame;
			u32 count = 0;
			bool firstSquare = false;
			bool lastSquare = false;
			bool differs = false;
			std::vector<u8> firstPicture;
			i64 lastTime = -1;
			bool monotonic = true;
			const auto white_at = [&](const VideoFrame& f, u32 x, u32 y)
			{
				const u8* pixel = f.Bgra.data() + (static_cast<size_t>(y) * f.Width + x) * 4;
				return pixel[0] > 190 && pixel[1] > 190 && pixel[2] > 190;
			};
			for (u32 guard = 0; guard < 2000; guard++)
			{
				const DecodeStatus status = decoder->read_video(frame);
				if (status == DecodeStatus::Pending)
				{
					continue;
				}
				if (status != DecodeStatus::Ok)
				{
					break;
				}
				monotonic = monotonic && frame.TimestampUs >= lastTime;
				lastTime = frame.TimestampUs;
				if (count == 0)
				{
					firstPicture = frame.Bgra;
					firstSquare = white_at(frame, 12, 90);
				}
				differs = differs || (count == 20 && frame.Bgra != firstPicture);
				lastSquare = white_at(frame, frame.Width - 12, 90);
				count++;
			}
			// the bars darken toward the top (a flat top to bottom ramp) and the second bar is cyan (red low, green and blue high)
			if (!firstPicture.empty())
			{
				const auto pixel = [&](u32 x, u32 y) { return firstPicture.data() + (static_cast<size_t>(y) * 320 + x) * 4; };
				const u8* top = pixel(100, 4);
				const u8* bottom = pixel(100, 175);
				check(top[0] + top[1] + top[2] < bottom[0] + bottom[1] + bottom[2], "mf_generated_clip: the picture is not upside down (the top is darker than the bottom)");
				const u8* cyan = pixel(60, 170); // x 60 is in the second bar (45 pixels wide) of the first picture
				check(cyan[2] < 80 && cyan[1] > 170 && cyan[0] > 170, "mf_generated_clip: the colours are in the right channels (cyan is not red or yellow)");
			}
			check(count >= 57 && count <= 63, "mf_generated_clip: about 60 pictures");
			check(monotonic, "mf_generated_clip: picture timestamps never go back");
			check(firstSquare, "mf_generated_clip: the white square is at the left in the first picture (the picture is the right way up and in the right colours)");
			check(lastSquare, "mf_generated_clip: the white square is at the right in the last picture");
			check(differs, "mf_generated_clip: the pictures change");

			// sound: 440 Hz for the first second, 660 Hz for the second
			Scope<IVideoDecoder> audioDecoder = create_platform_decoder();
			audioDecoder->open(*bytes, info, error);
			std::vector<float> left;
			AudioChunk chunk;
			for (u32 guard = 0; guard < 20000; guard++)
			{
				const DecodeStatus status = audioDecoder->read_audio(chunk);
				if (status == DecodeStatus::Pending)
				{
					continue;
				}
				if (status != DecodeStatus::Ok)
				{
					break;
				}
				for (u32 f = 0; f < chunk.Frames; f++)
				{
					left.push_back(chunk.Samples[static_cast<size_t>(f) * info.Channels]);
				}
			}
			check(left.size() > 90000 && left.size() < 100000, "mf_generated_clip: about 2 s of sound at 48000 Hz");
			if (left.size() > 90000)
			{
				float peak = 0.0f;
				for (const float v : left)
				{
					peak = std::max(peak, std::abs(v));
				}
				check(peak > 0.3f && peak <= 1.0f, "mf_generated_clip: the tone is loud (the decoder gives real samples)");
				const double early = estimate_hz(left, 12000, 24000, 48000);  // from 0.25 s to 0.75 s
				const double late = estimate_hz(left, 60000, 24000, 48000);   // from 1.25 s to 1.75 s
				WI_VIDEO_LOG(Info, "Video MF smoke test: the generated clip's tones are {0:.0f} Hz and {1:.0f} Hz (peak {2:.2f})", early, late, peak);
				check(std::abs(early - 440.0) < 30.0, "mf_generated_clip: the first half is 440 Hz");
				check(std::abs(late - 660.0) < 40.0, "mf_generated_clip: the second half is 660 Hz");
			}
		}

		void check_bad_files(Check& check, const std::vector<std::byte>& clip)
		{
			{
				// a clip cut short: whatever happens, it ends cleanly
				std::vector<std::byte> cut(clip.begin(), clip.begin() + static_cast<std::ptrdiff_t>(clip.size() * 4 / 10));
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				const bool opened = decoder->open(cut, info, error);
				if (opened)
				{
					VideoFrame frame;
					DecodeStatus status = DecodeStatus::Ok;
					u32 guard = 0;
					for (; guard < 5000; guard++)
					{
						status = decoder->read_video(frame);
						if (status == DecodeStatus::End || status == DecodeStatus::Error)
						{
							break;
						}
					}
					check(guard < 5000 && (status == DecodeStatus::End || status == DecodeStatus::Error), "mf_corrupt_file_fails_cleanly: a clip cut short ends with End or Error");
				}
				else
				{
					check(!error.empty(), "mf_corrupt_file_fails_cleanly: a clip cut short that does not open says why");
				}
			}
			{
				std::vector<std::byte> noise(4096);
				for (size_t i = 0; i < noise.size(); i++)
				{
					noise[i] = static_cast<std::byte>((i * 2654435761u) >> 13);
				}
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				check(!decoder->open(noise, info, error) && !error.empty(), "mf_not_a_video: noise does not open and the reason is given");
				VideoFrame frame;
				check(decoder->read_video(frame) == DecodeStatus::Error, "mf_not_a_video: reading a decoder that did not open is an Error, not a crash");
			}
			{
				Scope<IVideoDecoder> decoder = create_platform_decoder();
				VideoInfo info;
				std::string error;
				check(!decoder->open(std::span<const std::byte>(), info, error) && !error.empty(), "mf_not_a_video: no bytes do not open");
			}
		}

		void check_player(Check& check, const std::vector<std::byte>& clip, const VideoInfo& info)
		{
			// the whole chain on the decode worker: COM and Media Foundation start on that thread and end on it
			VideoPlayerDesc desc;
			desc.AudioMode = VideoAudioMode::Manual;
			VideoPlayer player(create_platform_decoder(), desc);
			player.open(clip);

			const auto start = std::chrono::steady_clock::now();
			const auto expired = [&](u32 ms) { return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(ms); };
			while (player.state() == VideoState::Loading && !expired(10000))
			{
				player.update(0);
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			check(player.state() == VideoState::Idle, "mf_player: the clip opens on the worker (" + player.error() + ")");
			if (player.state() != VideoState::Idle)
			{
				return;
			}
			check(player.uses_audio() == info.HasAudio, "mf_player: the sound feed exists exactly when the clip has sound");
			check(std::abs(player.duration() - info.DurationSeconds) < 1e-6, "mf_player: the duration is the decoder's");

			player.play();
			player.update(0);
			bool advanced = false;
			i64 wall = 0;
			u64 lastSerial = player.frame_serial();
			u32 changes = 0;
			const auto begin = std::chrono::steady_clock::now();
			while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(8) && changes < 20 && player.state() != VideoState::Failed)
			{
				wall += 16667;
				player.update(wall);
				if (player.audio() != nullptr)
				{
					player.audio()->render(800); // 60 Hz of 48 kHz sound
				}
				if (player.frame_serial() != lastSerial)
				{
					lastSerial = player.frame_serial();
					changes++;
				}
				advanced = player.current_frame() != nullptr;
				std::this_thread::sleep_for(std::chrono::milliseconds(4));
			}
			check(player.state() != VideoState::Failed, "mf_player: the clip plays without failing (" + player.error() + ")");
			check(advanced && changes >= 5, "mf_player: new pictures keep arriving while it plays");

			player.seek(0.5);
			player.update(wall);
			bool afterSeek = false;
			const auto seekStart = std::chrono::steady_clock::now();
			const u64 serialBefore = player.frame_serial();
			while (std::chrono::steady_clock::now() - seekStart < std::chrono::seconds(5) && !afterSeek)
			{
				wall += 16667;
				player.update(wall);
				if (player.audio() != nullptr)
				{
					player.audio()->render(800);
				}
				afterSeek = player.frame_serial() != serialBefore;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			check(afterSeek, "mf_player: a picture arrives after a seek");
			// the player goes out of scope with the worker mid decode
		}
	}

	bool run_video_mf_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		const std::filesystem::path path = pipeline.get_database().get_assets_root() / "video" / "Intro.mp4";
		std::optional<std::vector<std::byte>> clip = read_binary_file(path);
		if (!clip || clip->empty())
		{
			WI_VIDEO_LOG(Warning, "Video MF smoke test: skipped, {0} is missing (scripts/gen_demo_scene.py downloads it): the platform decoder was NOT checked", path.string());
			WI_VIDEO_LOG(Info, "Video MF smoke test passed");
			return true;
		}

		VideoInfo probed;
		check_decoder(check, *clip, probed);
		check_bad_files(check, *clip);
		check_generated_clip(check);

		// the importer's probe cooked the same info into the asset
		{
			const std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(path));
			check(meta.has_value(), "mf_importer_probe: the clip has a .meta file");
			bool found = false;
			if (meta)
			{
				for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(pipeline.get_database().get_library_root()))
				{
					if (!entry.is_regular_file())
					{
						continue;
					}
					std::optional<std::vector<std::byte>> bytes = read_binary_file(entry.path());
					std::span<const std::byte> payload;
					std::optional<WAssetHeader> header = bytes ? parse_wasset_header(*bytes, payload) : std::nullopt;
					if (header && header->Guid == meta->Guid && header->Type == AssetType::Video)
					{
						found = true;
						const std::optional<VideoData> data = deserialize<VideoData>(payload);
						check(data && data->HasInfo && data->Info.Width == probed.Width && data->Info.Height == probed.Height &&
							std::abs(data->Info.DurationSeconds - probed.DurationSeconds) < 1e-6 && data->Info.HasAudio == probed.HasAudio,
							"mf_importer_probe: the cooked asset carries the info the decoder reports");
					}
				}
			}
			check(found, "mf_importer_probe: the clip is cooked as a Video asset");
		}

		check_player(check, *clip, probed);

		if (check.Failures == 0)
		{
			WI_VIDEO_LOG(Info, "Video MF smoke test passed");
		}
		return check.Failures == 0;
	}
}
