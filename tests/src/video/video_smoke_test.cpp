#include <editor_common.h>
#include "video_smoke_test.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

#include <assets/asset_manager.h>
#include <renderer/view/view_types.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_module.h>
#include <video/null_decoder.h>
#include <video/video_audio.h>
#include <video/video_clock.h>
#include <video/video_decoder.h>
#include <video/video_module.h>
#include <video/video_player.h>
#include <video/video_types.h>

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

#include <test_kit.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Video";
			static constexpr const char* Prefix = "Video smoke test";
		};
		using Check = BasicCheck<CheckLog>;

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

	namespace
	{
		constexpr i64 STEP_US = 33333; // one frame at 30 fps

		// The worker is a real thread, the clock is driven by hand: update at this wall time until the condition holds (or a second passes).
		bool pump(VideoPlayer& player, i64 wallUs, const std::function<bool()>& done, u32 timeoutMs = 2000)
		{
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				player.update(wallUs);
				if (done())
				{
					return true;
				}
				if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
				{
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		struct Rig
		{
			Scope<VideoPlayer> Player;

			explicit Rig(const NullDecoderConfig& config = {}, bool loop = false)
			{
				VideoPlayerDesc desc;
				desc.Loop = loop;
				Player = CreateScope<VideoPlayer>(create_null_decoder(config), desc);
				Player->open(std::vector<std::byte>(16));
			}

			// Waits for the open to finish; returns whether the state left Loading.
			bool opened(i64 wallUs = 0) { return pump(*Player, wallUs, [this]() { return Player->state() != VideoState::Loading; }); }
			i64 index() const { const auto frame = Player->current_frame(); return frame ? static_cast<i64>(frame_index(*frame)) : -1; }
		};

		// The newest frame of a 30 fps Null stream that is due at this time (frame k is at round(k * 1e6 / 30)).
		i64 due_frame(i64 wallUs)
		{
			i64 k = wallUs * 30 / 1'000'000;
			while (std::llround(static_cast<double>(k + 1) * 1e6 / 30.0) <= wallUs)
			{
				k++;
			}
			return k;
		}

		// Plays from wall 0 in frame steps up to endUs; at each step waits until the picture shown is the expected one. Returns the last index shown.
		i64 play_in_steps(Rig& rig, i64 endUs, Check& check, const char* what)
		{
			for (i64 wall = 0; wall <= endUs; wall += STEP_US)
			{
				const i64 expected = due_frame(wall);
				const bool reached = pump(*rig.Player, wall, [&]() { return rig.index() >= expected; });
				check(reached, std::string(what) + ": the expected frame arrives");
				check(rig.index() <= expected, std::string(what) + ": the picture is never ahead of the clock");
			}
			return rig.index();
		}

		void check_player_basic(Check& check)
		{
			{
				NullDecoderConfig config;
				config.DurationSeconds = 1.0;
				Rig rig(config);
				check(rig.Player->state() == VideoState::Loading, "player starts Loading");
				check(rig.opened() && rig.Player->state() == VideoState::Idle, "player: open leaves Loading for Idle");
				check(std::abs(rig.Player->duration() - 1.0) < 1e-9, "player reports the duration");
				check(pump(*rig.Player, 0, [&]() { return rig.index() == 0; }), "player shows the first frame while idle");

				rig.Player->play();
				rig.Player->update(0);
				check(rig.Player->state() == VideoState::Playing, "player: play -> Playing");

				play_in_steps(rig, 400000, check, "player play");
				rig.Player->pause();
				rig.Player->update(400000);
				check(rig.Player->state() == VideoState::Paused, "player: pause -> Paused");
				const double frozen = rig.Player->time();
				rig.Player->update(900000);
				check(std::abs(rig.Player->time() - frozen) < 1e-6, "player: time stands still while paused");
				rig.Player->play();
				rig.Player->update(900000);
				check(rig.Player->state() == VideoState::Playing, "player: play after pause -> Playing");
				check(std::abs(rig.Player->time() - frozen) < 0.01, "player: resume continues from the paused time");

				// let it run to the end: the clock moves in frame steps from 0.4 s
				i64 wall = 900000;
				const bool ended = pump(*rig.Player, wall, [&]() { wall += STEP_US; rig.Player->update(wall); return rig.Player->state() == VideoState::Ended; }, 5000);
				check(ended, "player: a non looping video ends");
				const std::optional<VideoFinished> finished = rig.Player->take_finished();
				check(finished && !finished->Skipped && !finished->Failed, "player: Finished{} after the natural end");
				check(!rig.Player->take_finished(), "player: Finished is delivered once");
			}
			{
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				play_in_steps(rig, 450000, check, "player right frame");
				check(pump(*rig.Player, 500000, [&]() { return rig.index() == 15; }), "player: at 0.5 s the picture is frame 15");
			}
		}

		void check_player_sync(Check& check)
		{
			{
				// a jump of the clock: the frames in between are dropped, never shown
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				rig.Player->update(0);
				pump(*rig.Player, 0, [&]() { return rig.index() == 0; });
				std::vector<i64> shown;
				i64 last = -1;
				const bool reached = pump(*rig.Player, 1'000'000, [&]() {
					if (rig.index() != last) { last = rig.index(); shown.push_back(last); }
					return rig.index() == 30; });
				check(reached, "player: after a clock jump the picture catches up");
				check(shown.size() < 20, "player: frames older than the clock are dropped, not shown");
				bool ascending = true;
				for (size_t i = 1; i < shown.size(); i++)
				{
					ascending = ascending && shown[i] > shown[i - 1];
				}
				check(ascending, "player: pictures only move forward");
			}
			{
				// a decoder that is behind: the last picture stays up
				NullDecoderConfig config;
				config.PendingEvery = 2;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				rig.Player->update(0);
				pump(*rig.Player, 0, [&]() { return rig.index() == 0; });
				i64 previous = rig.index();
				bool held = true;
				for (i64 wall = 200000; wall <= 2'000'000; wall += 200000)
				{
					rig.Player->update(wall);
					held = held && rig.Player->current_frame() != nullptr && rig.index() >= previous;
					previous = rig.index();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				check(held, "player: a decoder that is behind never leaves the screen empty or goes back");
			}
		}

		void check_player_seek_and_loop(Check& check)
		{
			{
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				play_in_steps(rig, 200000, check, "player before seek");
				rig.Player->seek(1.0);
				const u64 before = rig.Player->frame_serial();
				const bool arrived = pump(*rig.Player, 200000, [&]() { return rig.Player->frame_serial() != before; });
				check(arrived, "player: a picture arrives after a seek");
				check(rig.index() >= 30, "player: after a seek no picture from before the target is shown");
				check(std::abs(rig.Player->time() - 1.0) < 0.05, "player: the clock holds at the seek target");

				// stale pictures never come back
				bool stale = false;
				for (int i = 0; i < 50; i++)
				{
					rig.Player->update(200000 + (i + 1) * STEP_US);
					stale = stale || rig.index() < 30;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				check(!stale, "player: pictures from before the seek never reappear");
			}
			{
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				for (const double bad : { std::nan(""), -5.0, 1e9, -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() })
				{
					rig.Player->seek(bad);
					pump(*rig.Player, 0, []() { return true; });
					const double t = rig.Player->time();
					check(std::isfinite(t) && t >= 0.0 && t <= rig.Player->duration() + 1e-6, "player: a seek to NaN, a negative, infinity or past the end is clamped");
				}
			}
			{
				NullDecoderConfig config;
				config.DurationSeconds = 0.5;
				Rig rig(config, true);
				rig.opened();
				rig.Player->play();
				rig.Player->update(0);
				double previous = -1.0;
				bool monotonic = true;
				bool wrapped = false;
				i64 lastIndex = -1;
				for (i64 wall = 0; wall <= 1'600'000; wall += STEP_US)
				{
					pump(*rig.Player, wall, [&]() { return rig.index() >= std::min<i64>(14, wall * 30 / 1'000'000 % 15); }, 200);
					const double t = rig.Player->time();
					monotonic = monotonic && t >= previous;
					previous = t;
					wrapped = wrapped || (rig.index() < lastIndex);
					lastIndex = rig.index();
				}
				check(monotonic, "player: the clock never goes back across a loop");
				check(wrapped, "player: a looping video starts over");
				check(!rig.Player->take_finished(), "player: a looping video does not finish");
				check(rig.Player->state() == VideoState::Playing, "player: a looping video keeps playing");
			}
		}

		// the findings of the final review
		void check_player_review(Check& check)
		{
			// a seek to the end (or past it) of a playing video ends it: no waiting forever on a picture that does not exist
			{
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				play_in_steps(rig, 200000, check, "player before the seek to the end");
				rig.Player->seek(1e9);
				i64 wall = 200000;
				const bool ended = pump(*rig.Player, wall, [&]() { wall += STEP_US; rig.Player->update(wall); return rig.Player->state() == VideoState::Ended; }, 5000);
				check(ended, "player_seek_to_end: a seek past the end of a playing video ends it");
				const std::optional<VideoFinished> finished = rig.Player->take_finished();
				check(finished && !finished->Skipped && !finished->Failed, "player_seek_to_end: with Finished{}");
			}
			{
				NullDecoderConfig config;
				config.DurationSeconds = 0.5;
				Rig rig(config, true);
				rig.opened();
				rig.Player->play();
				rig.Player->update(0);
				rig.Player->seek(1e9);
				i64 wall = 0;
				const bool restarted = pump(*rig.Player, wall, [&]() { wall += STEP_US; rig.Player->update(wall); return rig.Player->current_frame() && rig.Player->time() < 0.3 && rig.Player->state() == VideoState::Playing; }, 5000);
				check(restarted && !rig.Player->take_finished(), "player_seek_to_end: a looping video seeked past its end starts over and does not finish");
			}

			// requests made while the file is still opening are kept, not dropped
			{
				NullDecoderConfig config;
				config.OpenDelayMs = 150;
				Rig rig(config);
				rig.Player->play();
				rig.Player->seek(1.0);
				rig.Player->update(0);
				check(rig.Player->state() == VideoState::Loading, "player_requests_while_loading: the file is still opening");
				check(pump(*rig.Player, 0, [&]() { return rig.Player->state() == VideoState::Playing; }), "player_requests_while_loading: the play that came first happens");
				check(pump(*rig.Player, 0, [&]() { return rig.index() >= 30; }), "player_requests_while_loading: and so does the seek (the first picture is at or after 1 s)");
			}
			{
				NullDecoderConfig config;
				config.OpenDelayMs = 150;
				Rig rig(config);
				rig.Player->play();
				rig.Player->pause();
				rig.Player->update(0);
				check(pump(*rig.Player, 0, [&]() { return rig.Player->state() != VideoState::Loading; }) && rig.Player->state() == VideoState::Paused, "player_requests_while_loading: a pause after the play leaves it paused");
			}
			{
				NullDecoderConfig config;
				config.OpenDelayMs = 150;
				Rig rig(config);
				rig.Player->play();
				rig.Player->update(0);
				rig.Player->stop();
				rig.Player->update(0);
				const std::optional<VideoFinished> finished = rig.Player->take_finished();
				check(finished && finished->Skipped, "player_requests_while_loading: stopping a video that was asked to play reports Finished{Skipped}");
				check(pump(*rig.Player, 0, [&]() { return rig.Player->state() == VideoState::Idle; }), "player_requests_while_loading: and it is idle once open");
			}
		}

		void check_player_failures(Check& check)
		{
			{
				NullDecoderConfig config;
				config.FailOpen = true;
				Rig rig(config);
				check(rig.opened() && rig.Player->state() == VideoState::Failed, "player: a file that does not open is Failed");
				check(!rig.Player->error().empty(), "player: Failed has a reason");
				const std::optional<VideoFinished> finished = rig.Player->take_finished();
				check(finished && finished->Failed, "player: Finished{Failed} for a file that does not open");
			}
			const std::vector<std::pair<u32, u32>> badSizes = { { 0, 36 }, { 64, 0 }, { 9000, 36 }, { 64, 9000 } };
			for (const auto& [w, h] : badSizes)
			{
				NullDecoderConfig config;
				config.Width = w;
				config.Height = h;
				Rig rig(config);
				check(rig.opened() && rig.Player->state() == VideoState::Failed && !rig.Player->error().empty(), "player: a picture of 0 or over 8192 pixels is refused with a reason");
			}
			{
				NullDecoderConfig config;
				config.Fps = 100000.0f;
				Rig rig(config);
				check(rig.opened() && rig.Player->state() == VideoState::Failed, "player: an absurd frame rate is refused");
			}
			{
				NullDecoderConfig config;
				config.FailAfterFrames = 5;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				i64 wall = 0;
				i64 maxIndex = -1;
				const bool failed = pump(*rig.Player, wall, [&]() { wall += STEP_US; maxIndex = std::max(maxIndex, rig.index()); rig.Player->update(wall); return rig.Player->state() == VideoState::Failed; }, 5000);
				check(failed, "player: a stream that breaks ends Failed");
				check(maxIndex == 4, "player: the pictures decoded before the break are shown first");
				const std::optional<VideoFinished> finished = rig.Player->take_finished();
				check(finished && finished->Failed, "player: Finished{Failed} for a stream that breaks");
			}
			{
				NullDecoderConfig config;
				config.Width = 321;
				config.Height = 181;
				Rig rig(config);
				rig.opened();
				check(pump(*rig.Player, 0, [&]() { return rig.Player->current_frame() != nullptr; }), "player: odd sized pictures arrive");
				const auto frame = rig.Player->current_frame();
				check(frame && frame->Width == 321 && frame->Height == 181 && frame->Bgra.size() == 321u * 181u * 4u, "player: odd sizes keep Width*Height*4");
			}
			{
				NullDecoderConfig config;
				config.HasAudio = false;
				config.DurationSeconds = 0.5;
				Rig rig(config);
				rig.opened();
				rig.Player->play();
				play_in_steps(rig, 300000, check, "player without audio");
				check(rig.Player->state() == VideoState::Playing, "player: a file without sound plays");
			}
		}

		void check_player_lifecycle(Check& check)
		{
			// Whatever hangs here would hang the smoke: a guard thread ends the process after ten seconds instead.
			std::mutex guardMutex;
			std::condition_variable guardCv;
			bool finishedTest = false;
			std::thread guard([&]() {
				std::unique_lock<std::mutex> lock(guardMutex);
				if (!guardCv.wait_for(lock, std::chrono::seconds(10), [&]() { return finishedTest; }))
				{
					WI_VIDEO_LOG(Error, "Video smoke test FAILED: the player lifecycle checks hung (a worker was not joined)");
					std::abort();
				}
			});

			for (int i = 0; i < 100; i++)
			{
				NullDecoderConfig config;
				config.PendingEvery = static_cast<u32>(i % 3);
				Rig rig(config, i % 2 == 0);
				rig.Player->play();
				rig.Player->update(0);
				if (i % 4 == 0)
				{
					rig.Player->seek(0.3);
				}
				rig.Player->update(STEP_US);
				// the rig goes out of scope with the worker mid decode
			}

			{
				NullDecoderConfig config;
				Rig rig(config);
				rig.opened();
				u32 skipped = 0;
				for (int i = 0; i < 50; i++)
				{
					rig.Player->play();
					rig.Player->update(i * 1000);
					rig.Player->stop();
					rig.Player->update(i * 1000 + 500);
					check(rig.Player->state() == VideoState::Idle, "player: stop -> Idle");
					const std::optional<VideoFinished> finished = rig.Player->take_finished();
					if (finished && finished->Skipped)
					{
						skipped++;
					}
				}
				check(skipped == 50, "player: stopping a playing video reports Finished{Skipped} every time");
			}
			{
				std::lock_guard<std::mutex> lock(guardMutex);
				finishedTest = true;
			}
			guardCv.notify_all();
			guard.join();
		}
	}

	namespace
	{
		std::vector<float> ramp(u32 frames, u32 channels)
		{
			std::vector<float> data(static_cast<size_t>(frames) * channels, 0.5f);
			return data;
		}

		struct AudioRig
		{
			Scope<VideoPlayer> Player;

			explicit AudioRig(const NullDecoderConfig& config)
			{
				VideoPlayerDesc desc;
				desc.AudioMode = VideoAudioMode::Manual;
				Player = CreateScope<VideoPlayer>(create_null_decoder(config), desc);
				Player->open(std::vector<std::byte>(16));
			}
		};

		void check_audio_out(Check& check)
		{
			{
				VideoAudioOut out;
				std::string error;
				check(out.init(48000, 2, true, error), "audio out: init without a device");
				const std::vector<float> data = ramp(1000, 2);
				check(out.write(data.data(), 1000) == 1000, "audio out: a write that fits is accepted whole");
				out.start();
				out.render(600);
				check(out.played_frames() == 600, "audio out: render consumes what was written");
				out.render(600);
				check(out.played_frames() == 1000, "audio out: render plays only what is there");
				out.shutdown();
			}
			{
				VideoAudioOut out;
				std::string error;
				out.init(48000, 2, true, error);
				const std::vector<float> data = ramp(100000, 2);
				const u32 accepted = out.write(data.data(), 100000);
				check(accepted > 0 && accepted < 100000, "audio out: a full ring accepts a part of the write");
				check(out.write(data.data(), 10) == 0, "audio out: a full ring accepts nothing");
				out.shutdown();
			}
			{
				VideoAudioOut out;
				std::string error;
				out.init(48000, 2, true, error);
				const std::vector<float> data = ramp(1000, 2);
				out.write(data.data(), 1000);
				out.flush();
				out.start();
				out.render(1000);
				check(out.played_frames() == 0, "audio out: flushed sound is never played");
				out.write(data.data(), 500);
				out.render(1000);
				check(out.played_frames() == 500, "audio out: sound written after a flush is played");
				out.shutdown();
			}
			{
				VideoAudioOut out;
				VideoClock clock;
				clock.set_audio_master(true);
				std::string error;
				out.init(48000, 2, true, error);
				out.set_clock(&clock);
				clock.start(0);
				out.start();
				out.render(1000); // nothing was written: an underrun
				check(out.played_frames() == 0 && clock.now_us(0) == 0, "audio out: an underrun does not advance the clock");
				out.shutdown();
			}
			{
				VideoAudioOut out;
				std::string error;
				if (out.init(48000, 2, false, error))
				{
					const std::vector<float> data = ramp(480, 2);
					out.write(data.data(), 480);
					out.start();
					out.stop();
					out.set_volume(0.0f);
					out.shutdown();
				}
				else
				{
					WI_VIDEO_LOG(Warning, "Video smoke test: no sound device here ({0}): the real device check was skipped", error);
				}
			}
		}

		void check_player_audio(Check& check)
		{
			// the sound after a seek starts at the target, however early the decoder restarts it
			{
				NullDecoderConfig config;
				config.AudioSeekBackUs = 500000;
				AudioRig rig(config);
				pump(*rig.Player, 0, [&]() { return rig.Player->state() != VideoState::Loading; });
				rig.Player->play();
				rig.Player->update(0);
				rig.Player->seek(1.0);
				const u64 before = rig.Player->frame_serial();
				pump(*rig.Player, 0, [&]() { return rig.Player->frame_serial() != before; });
				pump(*rig.Player, 0, [&]() { return rig.Player->audio_frames_dropped() >= 20000; }, 3000);
				check(rig.Player->audio_frames_dropped() >= 20000 && rig.Player->audio_frames_dropped() <= 26000, "player_audio_trim: the half second of sound before the seek target is dropped, not played");
			}
			{
				// players with sound destroyed mid decode, with and without a real device thread: no hang, no callback on a freed ring
				for (int i = 0; i < 40; i++)
				{
					NullDecoderConfig config;
					VideoPlayerDesc desc;
					desc.AudioMode = i % 2 == 0 ? VideoAudioMode::Manual : VideoAudioMode::Device;
					desc.Loop = i % 3 == 0;
					VideoPlayer player(create_null_decoder(config), desc);
					player.open(std::vector<std::byte>(16));
					pump(player, 0, [&]() { return player.state() != VideoState::Loading; }, 500);
					player.play();
					player.update(0);
					if (player.audio() != nullptr && i % 2 == 0)
					{
						player.audio()->render(256);
					}
					if (i % 5 == 0)
					{
						player.seek(0.5);
						player.update(STEP_US);
					}
				}
			}
			{
				NullDecoderConfig config;
				AudioRig rig(config);
				check(pump(*rig.Player, 0, [&]() { return rig.Player->state() != VideoState::Loading; }), "audio player: opens");
				check(rig.Player->uses_audio(), "audio player: a file with sound uses the audio feed");
				rig.Player->play();
				rig.Player->update(0);
				VideoAudioOut* audio = rig.Player->audio();
				bool reached = false;
				const auto start = std::chrono::steady_clock::now();
				while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
				{
					rig.Player->update(0);
					audio->render(512);
					const auto frame = rig.Player->current_frame();
					if (audio->played_frames() >= 24000 && frame && frame_index(*frame) == 15)
					{
						reached = true;
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				check(reached, "audio player: after 0.5 s of sound has played the picture is frame 15");
				rig.Player->update(900'000'000'000); // the wall clock is far ahead and must not matter
				const auto frame = rig.Player->current_frame();
				check(frame && frame_index(*frame) < 20, "audio player: the wall clock does not move the picture when sound is the master");
			}
			{
				NullDecoderConfig config;
				config.HasAudio = false;
				AudioRig rig(config);
				pump(*rig.Player, 0, [&]() { return rig.Player->state() != VideoState::Loading; });
				check(!rig.Player->uses_audio(), "audio player: a file without a sound track opens no audio feed");
				rig.Player->play();
				rig.Player->update(0);
				check(pump(*rig.Player, 500000, [&]() { return rig.Player->current_frame() && frame_index(*rig.Player->current_frame()) >= 10; }), "audio player: a file without sound runs on the wall clock");
			}
			{
				// sound shorter than the picture: the clock must not stop with the sound
				NullDecoderConfig config;
				config.DurationSeconds = 2.0;
				config.AudioDurationSeconds = 0.25;
				AudioRig rig(config);
				pump(*rig.Player, 0, [&]() { return rig.Player->state() != VideoState::Loading; });
				rig.Player->play();
				rig.Player->update(0);
				VideoAudioOut* audio = rig.Player->audio();
				const auto start = std::chrono::steady_clock::now();
				while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5) && audio->played_frames() < 12000)
				{
					rig.Player->update(0);
					audio->render(512);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				check(audio->played_frames() >= 12000, "audio player: the short sound played out");
				i64 wall = 0;
				const bool ended = pump(*rig.Player, wall, [&]() { wall += STEP_US * 4; rig.Player->update(wall); return rig.Player->state() == VideoState::Ended; }, 8000);
				check(ended, "audio player: when the sound ends first the picture goes on by the wall clock and finishes");
			}
		}
	}

	namespace
	{
		// A scene with canvases that show panels of Video widgets, the UI module that runs them and a video module with the Null decoder: the same
		// calls the engine makes each frame (UI interaction, then the video module, then the draw list), by hand.
		struct ModuleRig
		{
			Ref<Scene> TheScene = std::make_shared<Scene>();
			UiModule Ui{};
			VideoModule Video{};
			std::unordered_map<AssetGuid, Ref<AssetSlot>> Slots{};
			VideoData Data{};
			u32 LoadingPolls = 0; // the video asset says Loading this many times first
			bool LoadFails = false;
			NullDecoderConfig Decoder{};
			std::vector<std::pair<u32, std::shared_ptr<const VideoFrame>>> Uploads{};
			std::vector<u32> Releases{};
			std::vector<AssetGuid> Loaded{}; // the video assets the module asked for
			i64 Wall = 0;
			glm::vec2 Canvas{ 1920.0f, 1080.0f };

			ModuleRig()
			{
				Ui.set_panel_loader([this](const AssetRef& ref) -> Handle<UiPanelData>
				{
					const auto it = Slots.find(ref.Guid);
					return it == Slots.end() ? Handle<UiPanelData>{} : Handle<UiPanelData>(ref, it->second);
				});
				Data.Name = "clip";
				Data.Bytes.assign(16, std::byte{ 1 });
				Video.set_ui_module(&Ui);
				Video.set_audio_mode(VideoAudioMode::Off);
				Video.set_decoder_factory([this]() { return create_null_decoder(Decoder); });
				Video.set_video_loader([this](const AssetRef& asset) -> VideoAssetBytes
				{
					Loaded.push_back(asset.Guid);
					if (LoadFails)
					{
						return { LoadState::Failed, nullptr };
					}
					if (LoadingPolls > 0)
					{
						LoadingPolls--;
						return { LoadState::Loading, nullptr };
					}
					return { LoadState::Ready, &Data };
				});
				Video.set_frame_sink([this](u32 stream, std::shared_ptr<const VideoFrame> frame) { Uploads.emplace_back(stream, std::move(frame)); });
				Video.set_release_sink([this](u32 stream) { Releases.push_back(stream); });
				Video.init(); // the UI module asks it for the pictures of Video widgets
				Ui.set_video_event_source({}); // the checks read take_finished() themselves: the UI must not drain it
			}

			AssetRef add_panel(std::vector<UiWidget> widgets)
			{
				UiPanelData data;
				data.Widgets = std::move(widgets);
				for (const UiWidget& widget : data.Widgets)
				{
					data.NextId = std::max(data.NextId, widget.Id + 1);
				}
				const AssetRef ref{ AssetGuid::generate(), 0 };
				auto slot = std::make_shared<AssetSlot>();
				slot->Guid = ref.Guid;
				slot->Data = std::make_shared<UiPanelData>(data);
				slot->State = LoadState::Ready;
				Slots[ref.Guid] = slot;
				return ref;
			}

			EntityGuid add_canvas(const AssetRef& panel, bool visible = true)
			{
				const entt::entity entity = TheScene->create("Canvas");
				TheScene->registry().emplace<UICanvas>(entity, UICanvas{ panel, 0, visible });
				return TheScene->registry().get<Id>(entity).Guid;
			}

			static UiWidget video_widget(UiId id, const char* name, bool autoPlay, const AssetRef& video)
			{
				UiWidget widget;
				widget.Id = id;
				widget.Kind = WidgetKind::Video;
				widget.Name = name;
				widget.AnchorMin = { 0.0f, 0.0f };
				widget.AnchorMax = { 1.0f, 1.0f };
				widget.OffsetMin = { 0.0f, 0.0f };
				widget.OffsetMax = { 0.0f, 0.0f };
				widget.AutoPlay = autoPlay;
				widget.Video = video;
				return widget;
			}

			UiDrawList frame(bool playing = true, const Ref<Scene>* active = nullptr)
			{
				Wall += STEP_US;
				const Ref<Scene>& scene = active != nullptr ? *active : TheScene;
				scene->update_world_transforms();
				Ui.update_interaction(*scene, Canvas);
				Video.tick(scene, playing, Wall);
				UiDrawList list;
				Ui.build_draw_list(*scene, Canvas, list);
				return list;
			}

			// Frames until the condition holds (the decode worker is a real thread), or a few seconds pass.
			bool frames_until(const std::function<bool()>& done, bool playing = true)
			{
				const auto start = std::chrono::steady_clock::now();
				while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3))
				{
					frame(playing);
					if (done())
					{
						return true;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				return false;
			}

			UiWidget* widget(const EntityGuid& canvas, UiId id)
			{
				UiPanelInstance* instance = Ui.instance_for(*TheScene, canvas);
				return instance != nullptr ? instance->data().find(id) : nullptr;
			}
		};

		void check_module(Check& check)
		{
			const AssetRef someVideo{ AssetGuid::generate(), 0 };

			// a widget without a video asset fails with a reason, and says so
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, AssetRef{}) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				const VideoKey key{ canvas, 1 };
				check(rig.Video.play(key), "module_missing_asset: play on a widget is accepted");
				rig.frame();
				check(rig.Video.state(key) == VideoState::Failed && !rig.Video.error(key).empty(), "module_missing_asset: a widget with no video fails with a reason");
				const auto finished = rig.Video.take_finished();
				check(finished.size() == 1 && finished[0].second.Failed && finished[0].first == key, "module_missing_asset: and reports Finished{Failed}");
			}

			// a play on a widget that does not exist
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				check(!rig.Video.play(VideoKey{ canvas, 99 }) && !rig.Video.last_error().empty(), "module_no_widget: a widget that is not there is refused with a reason");
				check(!rig.Video.play(VideoKey{ EntityGuid::generate(), 1 }), "module_no_widget: so is a canvas that is not there");
				check(rig.Video.state(VideoKey{ canvas, 99 }) == VideoState::Idle && rig.Video.player_count() == 0, "module_no_widget: and nothing plays");
			}

			// the asset loads: Loading, then it plays
			{
				ModuleRig rig;
				rig.LoadingPolls = 5;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				const VideoKey key{ canvas, 1 };
				rig.Video.play(key);
				rig.frame();
				check(rig.Video.state(key) == VideoState::Loading, "module_asset_loading: while the asset loads the state is Loading");
				check(rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; }), "module_asset_loading: once it is loaded the video plays (a play that came first is remembered)");
			}
			{
				ModuleRig rig;
				rig.LoadFails = true;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				const VideoKey key{ canvas, 1 };
				rig.Video.play(key);
				rig.frame();
				check(rig.Video.state(key) == VideoState::Failed && !rig.Video.error(key).empty(), "module_asset_failed: an asset that does not load fails with a reason");
			}

			// no platform decoder
			{
				ModuleRig rig;
				rig.Video.set_decoder_factory([]() -> Scope<IVideoDecoder> { return nullptr; });
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				const VideoKey key{ canvas, 1 };
				rig.Video.play(key);
				rig.frame();
				check(rig.Video.state(key) == VideoState::Failed && !rig.Video.error(key).empty(), "module_no_decoder: a platform without a decoder fails with a reason");
			}

			// a widget whose video or loop is changed from a script plays what it says now
			{
				ModuleRig rig;
				const AssetRef first{ AssetGuid::generate(), 0 };
				const AssetRef second{ AssetGuid::generate(), 0 };
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, first) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frame();
				rig.Video.play(key);
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				check(!rig.Loaded.empty() && rig.Loaded.back() == first.Guid, "module_widget_changed: the first video is the one asked for");
				rig.widget(canvas, 1)->Video = second;
				rig.Video.play(key);
				rig.frames_until([&]() { return !rig.Loaded.empty() && rig.Loaded.back() == second.Guid; });
				check(!rig.Loaded.empty() && rig.Loaded.back() == second.Guid, "module_widget_changed: after the widget names another video, play asks for that one");
			}

			// a video that is over (or stopped) and whose widget is hidden lets go of its decoder, sound and picture
			{
				ModuleRig rig;
				rig.Decoder.DurationSeconds = 0.2;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Ended; });
				check(rig.Video.player_count() == 1, "module_release_on_end: while the widget still shows the end the player is kept");
				const size_t released = rig.Releases.size();
				rig.widget(canvas, 1)->Visible = false;
				rig.frame();
				rig.frame();
				check(rig.Video.player_count() == 0 && rig.Releases.size() == released + 1, "module_release_on_end: hidden, the player and its picture are let go");
				check(rig.Video.state(key) == VideoState::Ended, "module_release_on_end: and the widget still reports that it ended");
				rig.widget(canvas, 1)->Visible = true;
				rig.Video.play(key);
				check(rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; }), "module_release_on_end: and it can play again");
			}
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				rig.Video.stop(key);
				rig.frame();
				rig.widget(canvas, 1)->Visible = false;
				rig.frame();
				rig.frame();
				check(rig.Video.player_count() == 0 && rig.Video.state(key) == VideoState::Idle, "module_release_on_end: a stopped video whose widget is hidden is let go too");
			}

			// a seek and a pause that come while the asset is still loading are not lost
			{
				ModuleRig rig;
				rig.LoadingPolls = 4;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frame();
				rig.Video.play(key);
				rig.Video.seek(key, 1.0);
				double timeWhenPlaying = -1.0;
				check(rig.frames_until([&]() {
					if (timeWhenPlaying < 0.0 && rig.Video.state(key) == VideoState::Playing) { timeWhenPlaying = rig.Video.time(key); }
					return timeWhenPlaying >= 0.0; }), "module_requests_while_loading: the video plays");
				check(timeWhenPlaying >= 0.9, "module_requests_while_loading: a seek made while the asset loads is applied (it starts at the target, not at 0)");
			}
			{
				ModuleRig rig;
				rig.LoadingPolls = 4;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frame();
				rig.Video.play(key);
				rig.Video.pause(key);
				check(rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Paused; }), "module_requests_while_loading: a pause made while the asset loads is applied (it does not play on)");
			}

			// a script starts a video in the first frame: the UI has run, the video module has not ticked yet
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Intro", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.TheScene->update_world_transforms();
				rig.Ui.update_interaction(*rig.TheScene, rig.Canvas);
				check(rig.Video.play(key, rig.TheScene), "module_play_before_first_tick: a play before the video module has seen the scene is accepted");
				check(rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; }), "module_play_before_first_tick: and it plays (the first tick must not drop it)");
			}

			// one active player
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "A", false, someVideo), ModuleRig::video_widget(2, "B", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.frame();
				const VideoKey a{ canvas, 1 };
				const VideoKey b{ canvas, 2 };
				rig.Video.play(a);
				check(rig.frames_until([&]() { return rig.Video.state(a) == VideoState::Playing; }), "module_one_active_player: the first video plays");
				const size_t releasedBefore = rig.Releases.size();
				rig.Video.play(b);
				rig.frame();
				check(rig.Video.player_count() == 1, "module_one_active_player: a second play replaces the first: one player");
				check(rig.Video.state(a) == VideoState::Idle && rig.Releases.size() == releasedBefore + 1, "module_one_active_player: the first one is gone and its picture released");
				check(rig.frames_until([&]() { return rig.Video.state(b) == VideoState::Playing; }), "module_one_active_player: the second one plays");
			}

			// auto play
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo), ModuleRig::video_widget(2, "Manual", false, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				check(rig.frames_until([&]() { return rig.Video.state(VideoKey{ canvas, 1 }) == VideoState::Playing; }), "module_autoplay: a visible AutoPlay widget starts by itself");
				check(rig.Video.state(VideoKey{ canvas, 2 }) == VideoState::Idle, "module_autoplay: a widget without AutoPlay does not");
			}
			{
				// not while the game clock is stopped (the editor's edit mode)
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				for (int i = 0; i < 10; i++)
				{
					rig.frame(false);
				}
				check(rig.Video.player_count() == 0 && rig.Video.state(VideoKey{ canvas, 1 }) == VideoState::Idle, "module_autoplay: nothing starts while the game clock is not playing");
			}

			// hidden widgets and the game clock
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				rig.widget(canvas, 1)->Visible = false;
				rig.frame();
				check(rig.Video.state(key) == VideoState::Paused, "module_hidden_pauses: a hidden widget pauses its video");
				rig.widget(canvas, 1)->Visible = true;
				rig.frame();
				check(rig.Video.state(key) == VideoState::Playing, "module_hidden_pauses: shown again it goes on");

				rig.frame(false);
				check(rig.Video.state(key) == VideoState::Paused, "module_game_clock_pauses: a stopped game clock pauses the video");
				rig.frame(true);
				check(rig.Video.state(key) == VideoState::Playing, "module_game_clock_pauses: and it goes on with the clock");

				// a pause the script asked for is not undone by the clock
				rig.Video.pause(key);
				rig.frame(false);
				rig.frame(true);
				check(rig.Video.state(key) == VideoState::Paused, "module_game_clock_pauses: a video the script paused stays paused");

				// the canvas hidden
				rig.Video.play(key);
				rig.frame();
				rig.TheScene->registry().get<UICanvas>(rig.TheScene->find(canvas)).Visible = false;
				rig.frame();
				check(rig.Video.state(key) == VideoState::Paused, "module_hidden_pauses: a hidden canvas pauses its video");
			}

			// the pictures go to the renderer, and the widget draws them
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				check(rig.frames_until([&]() { return !rig.Uploads.empty(); }), "module_frames_uploaded: a playing video sends its pictures");
				rig.frames_until([&]() { return rig.Uploads.size() >= 4; });
				bool sameStream = !rig.Uploads.empty();
				bool distinct = true;
				for (size_t i = 0; i < rig.Uploads.size(); i++)
				{
					sameStream = sameStream && rig.Uploads[i].first == rig.Uploads[0].first;
					distinct = distinct && (i == 0 || rig.Uploads[i].second != rig.Uploads[i - 1].second);
				}
				check(sameStream, "module_frames_uploaded: all of them to the stream of the player");
				check(distinct, "module_frames_uploaded: a picture is sent once, not every frame");

				if (!rig.Uploads.empty())
				{
					const std::optional<UiVideoView> view = rig.Video.view(key);
					check(view && view->Texture == view_video_texture_id(rig.Uploads[0].first) && view->Size == glm::vec2(64.0f, 36.0f), "module_view: the widget gets the texture of the stream and the size of the picture");
					const UiDrawList list = rig.frame();
					bool drawn = false;
					for (const UiBatch& batch : list.Batches)
					{
						drawn = drawn || (batch.Kind == UiQuadKind::Image && batch.ViewTexture == view_video_texture_id(rig.Uploads[0].first));
					}
					check(drawn, "module_view: the draw list draws that texture");
				}
				check(!rig.Video.view(VideoKey{ canvas, 77 }).has_value(), "module_view: another widget has no picture");
			}

			// the scene goes away
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing && !rig.Uploads.empty(); });
				const size_t released = rig.Releases.size();
				Ref<Scene> other = std::make_shared<Scene>();
				rig.frame(true, &other);
				check(rig.Video.player_count() == 0 && rig.Video.state(key) == VideoState::Idle, "module_scene_change_stops: another scene stops the video");
				check(rig.Releases.size() == released + 1, "module_scene_change_stops: and releases its picture, once");
				rig.frame(true, &other);
				check(rig.Releases.size() == released + 1, "module_scene_change_stops: not again");
			}
			{
				// the canvas is destroyed
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				rig.TheScene->destroy(rig.TheScene->find(canvas));
				rig.frame();
				check(rig.Video.player_count() == 0, "module_scene_change_stops: a destroyed canvas stops its video");
			}

			// a canvas with no area
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				rig.Canvas = { 0.0f, 0.0f };
				for (int i = 0; i < 20; i++)
				{
					rig.frame();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				check(rig.Video.state(VideoKey{ canvas, 1 }) != VideoState::Failed, "module_zero_canvas: a canvas with no area does not break playback");
			}

			// the end of a clip
			{
				ModuleRig rig;
				rig.Decoder.DurationSeconds = 0.2;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				check(rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Ended; }), "module_finished: a short clip ends");
				const auto finished = rig.Video.take_finished();
				check(finished.size() == 1 && finished[0].first == key && !finished[0].second.Failed && !finished[0].second.Skipped, "module_finished: and reports Finished{}");
				check(rig.Video.take_finished().empty(), "module_finished: once");
			}

			// stop, and shutdown
			{
				ModuleRig rig;
				const AssetRef panel = rig.add_panel({ ModuleRig::video_widget(1, "Auto", true, someVideo) });
				const EntityGuid canvas = rig.add_canvas(panel);
				const VideoKey key{ canvas, 1 };
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				rig.Video.stop(key);
				rig.frame();
				const auto finished = rig.Video.take_finished();
				check(rig.Video.state(key) == VideoState::Idle && finished.size() == 1 && finished[0].second.Skipped, "module_stop: stop ends it Idle with Finished{Skipped}");
				rig.Video.play(key);
				const size_t released = rig.Releases.size();
				rig.frames_until([&]() { return rig.Video.state(key) == VideoState::Playing; });
				rig.Video.shutdown();
				check(rig.Video.player_count() == 0 && rig.Releases.size() == released + 1, "module_shutdown: shutting down stops the player and releases its picture");
			}
		}
	}

	bool run_video_smoke_test(AssetPipelineModule&)
	{
		Check check;
		check_null_decoder(check);
		check_frame_choice(check);
		check_clock(check);
		check_player_basic(check);
		check_player_sync(check);
		check_player_seek_and_loop(check);
		check_player_failures(check);
		check_player_review(check);
		check_player_lifecycle(check);
		check_audio_out(check);
		check_player_audio(check);
		check_module(check);

		if (check.Failures == 0)
		{
			WI_VIDEO_LOG(Info, "Video smoke test passed");
		}
		return check.Failures == 0;
	}
}
