#include <editor_common.h>
#include "video_smoke_test.h"

#include <cmath>
#include <cstring>

#include <video/null_decoder.h>
#include <video/video_clock.h>
#include <video/video_decoder.h>
#include <video/video_types.h>

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
					WI_VIDEO_LOG(Error, "Video smoke test FAILED: {0}", what);
				}
			}
		};

		u32 frame_index(const VideoFrame& frame)
		{
			u32 index = 0;
			std::memcpy(&index, frame.Bgra.data(), sizeof(index));
			return index;
		}

		VideoInfo open_null(IVideoDecoder& decoder, bool& ok)
		{
			VideoInfo info;
			std::string error;
			const std::byte byte{};
			ok = decoder.open(std::span<const std::byte>(&byte, 1), info, error);
			return info;
		}

		void check_null_decoder(Check& check)
		{
			{
				NullDecoderConfig config;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				const VideoInfo info = open_null(*decoder, ok);
				check(ok && info.Width == 64 && info.Height == 36 && info.HasAudio, "null decoder opens with its config");
				check(std::abs(info.DurationSeconds - 2.0) < 1e-9, "null decoder reports its duration");

				VideoFrame frame;
				for (u32 i = 0; i < 4; i++)
				{
					check(decoder->read_video(frame) == DecodeStatus::Ok, "null decoder gives frames");
				}
				check(frame_index(frame) == 3, "null decoder: the fourth frame is frame 3");
				check(frame.TimestampUs == 100000, "null decoder: frame 3 at 30 fps is at 100000 us");
				check(frame.Bgra.size() == 64u * 36u * 4u, "null decoder: Bgra is Width*Height*4");
			}
			{
				NullDecoderConfig config;
				config.Width = 321;
				config.Height = 181;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				open_null(*decoder, ok);
				VideoFrame frame;
				check(ok && decoder->read_video(frame) == DecodeStatus::Ok && frame.Bgra.size() == 321u * 181u * 4u, "null decoder: odd sizes have no row padding");
			}
			{
				NullDecoderConfig config;
				config.FailOpen = true;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = true;
				open_null(*decoder, ok);
				check(!ok, "null decoder: FailOpen fails open");
			}
			{
				NullDecoderConfig config;
				config.FailAfterFrames = 2;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				open_null(*decoder, ok);
				VideoFrame frame;
				check(decoder->read_video(frame) == DecodeStatus::Ok && decoder->read_video(frame) == DecodeStatus::Ok, "null decoder: frames before the failure");
				check(decoder->read_video(frame) == DecodeStatus::Error, "null decoder: FailAfterFrames gives Error");
			}
			{
				NullDecoderConfig config;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				open_null(*decoder, ok);
				check(decoder->seek(1'000'000), "null decoder seeks");
				VideoFrame frame;
				check(decoder->read_video(frame) == DecodeStatus::Ok && frame_index(frame) == 30, "null decoder: after seek(1 s) the next frame is 30");
			}
			{
				NullDecoderConfig config;
				config.DurationSeconds = 0.1;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				open_null(*decoder, ok);
				VideoFrame frame;
				u32 count = 0;
				while (decoder->read_video(frame) == DecodeStatus::Ok && count < 100)
				{
					count++;
				}
				check(count == 3, "null decoder: a 0.1 s clip at 30 fps has 3 frames, then End");
			}
			{
				NullDecoderConfig config;
				Scope<IVideoDecoder> decoder = create_null_decoder(config);
				bool ok = false;
				open_null(*decoder, ok);
				AudioChunk a, b;
				check(decoder->read_audio(a) == DecodeStatus::Ok && decoder->read_audio(b) == DecodeStatus::Ok, "null decoder gives audio");
				check(a.Frames > 0 && a.Samples.size() == a.Frames * 2u, "null decoder: interleaved stereo samples");
				check(b.TimestampUs == a.TimestampUs + static_cast<i64>(a.Frames) * 1'000'000 / 48000, "null decoder: audio timestamps are continuous");
			}
		}

		void check_frame_choice(Check& check)
		{
			const i64 stamps[] = { 0, 33333, 66666 };
			check(choose_frame(stamps, 70000) == 2, "choose_frame: the newest frame not after the clock");
			check(choose_frame(stamps, 40000) == 1, "choose_frame: a frame in the middle");
			check(choose_frame(stamps, -1) == static_cast<size_t>(-1), "choose_frame: a clock before every frame chooses none");
			const i64 old[] = { 0 };
			check(choose_frame(old, 500000) == 0, "choose_frame: a stalled decode holds the last frame, however late");
			check(choose_frame(std::span<const i64>(), 0) == static_cast<size_t>(-1), "choose_frame: no frames chooses none");
		}

		void check_clock(Check& check)
		{
			{
				VideoClock clock;
				clock.start(0);
				clock.pause(1'000'000);
				clock.resume(5'000'000);
				check(clock.now_us(6'000'000) == 2'000'000, "clock: paused time does not count");
			}
			{
				VideoClock clock;
				clock.start(0);
				clock.pause(1'000'000);
				check(clock.now_us(9'000'000) == 1'000'000, "clock: frozen while paused");
			}
			{
				VideoClock clock;
				clock.set_audio_master(true);
				clock.start(0);
				clock.add_audio_frames(48000, 48000);
				check(clock.now_us(123456789) == 1'000'000, "clock: with an audio master only played audio frames count");
			}
			{
				VideoClock clock;
				clock.start(0);
				clock.hold_at(750'000);
				clock.pause(10);
				check(clock.now_us(99999999) == 750'000, "clock: hold_at sets the time");
			}
		}
	}

	bool run_video_smoke_test(AssetPipelineModule&)
	{
		Check check;
		check_null_decoder(check);
		check_frame_choice(check);
		check_clock(check);

		if (check.Failures == 0)
		{
			WI_VIDEO_LOG(Info, "Video smoke test passed");
		}
		return check.Failures == 0;
	}
}
