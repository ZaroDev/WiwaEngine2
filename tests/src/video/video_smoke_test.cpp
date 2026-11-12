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

#include <video/null_decoder.h>
#include <video/video_audio.h>
#include <video/video_clock.h>
#include <video/video_decoder.h>
#include <video/video_player.h>
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
			check(true, "player: destroying a player mid decode does not hang");

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
					check(true, "audio out: a real device starts and stops");
				}
				else
				{
					WI_VIDEO_LOG(Warning, "Video smoke test: no sound device here ({0}): the real device check was skipped", error);
				}
			}
		}

		void check_player_audio(Check& check)
		{
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
				check(true, "audio player: destroying players with sound mid decode does not hang");
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
		check_player_lifecycle(check);
		check_audio_out(check);
		check_player_audio(check);

		if (check.Failures == 0)
		{
			WI_VIDEO_LOG(Info, "Video smoke test passed");
		}
		return check.Failures == 0;
	}
}
