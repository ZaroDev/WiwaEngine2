#include <pch.h>
#include "video_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr float MAX_FPS = 1000.0f;
		constexpr float UNKNOWN_FPS = 30.0f;
		constexpr u32 AUDIO_CHUNK_FRAMES = 1024; // the decoder's usual run of sound: the ring is refilled when this much fits
	}

	VideoPlayer::VideoPlayer(Scope<IVideoDecoder> decoder, const VideoPlayerDesc& desc) : m_Decoder(std::move(decoder)), m_Desc(desc) {}

	VideoPlayer::~VideoPlayer()
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stop = true;
		}
		m_Cv.notify_all();
		if (m_Worker.joinable())
		{
			m_Worker.join();
		}
		// the worker no longer writes; the device thread is stopped before the ring and the clock it reads go away
		if (m_Audio)
		{
			m_Audio->shutdown();
		}
	}

	void VideoPlayer::open(std::vector<std::byte> bytes)
	{
		if (m_Started)
		{
			return;
		}
		m_Started = true;
		m_Bytes = std::move(bytes);
		m_Worker = std::thread([this]() { worker_main(); });
	}

	void VideoPlayer::play() { m_Requests.push_back({ Request::Kind::Play, 0.0 }); }
	void VideoPlayer::pause() { m_Requests.push_back({ Request::Kind::Pause, 0.0 }); }
	void VideoPlayer::stop() { m_Requests.push_back({ Request::Kind::Stop, 0.0 }); }
	void VideoPlayer::seek(double seconds) { m_Requests.push_back({ Request::Kind::Seek, seconds }); }

	void VideoPlayer::set_volume(float volume)
	{
		m_Desc.Volume = volume;
		if (m_Audio)
		{
			m_Audio->set_volume(volume);
		}
	}

	void VideoPlayer::clock_run(i64 wallUs)
	{
		m_Clock.resume(wallUs);
		if (m_Audio)
		{
			m_Audio->start();
		}
	}

	void VideoPlayer::clock_stop(i64 wallUs)
	{
		m_Clock.pause(wallUs);
		if (m_Audio)
		{
			m_Audio->stop();
		}
	}

	void VideoPlayer::clock_hold(i64 us)
	{
		m_Clock.hold_at(us);
		if (m_Audio)
		{
			m_Audio->stop();
		}
	}

	std::optional<VideoFinished> VideoPlayer::take_finished()
	{
		if (m_Finished.empty())
		{
			return std::nullopt;
		}
		const VideoFinished front = m_Finished.front();
		m_Finished.pop_front();
		return front;
	}

	double VideoPlayer::time() const
	{
		return static_cast<double>(std::max<i64>(0, m_Clock.now_us(m_WallUs))) / 1e6;
	}

	// ---- main thread ----

	void VideoPlayer::update(i64 wallUs)
	{
		m_WallUs = wallUs;
		poll_open();
		if (m_State == VideoState::Failed)
		{
			m_Requests.clear();
			return;
		}
		if (m_State == VideoState::Loading)
		{
			// requests wait for the open; a play is remembered by poll_open
			for (const Request& request : m_Requests)
			{
				if (request.What == Request::Kind::Play)
				{
					m_PlayWanted = true;
				}
				else if (request.What == Request::Kind::Stop)
				{
					m_PlayWanted = false;
				}
			}
			m_Requests.clear();
			return;
		}

		std::vector<Request> requests;
		requests.swap(m_Requests);
		for (const Request& request : requests)
		{
			apply_request(request, wallUs);
		}

		check_audio_end(wallUs);
		take_frames(wallUs);
		check_end(wallUs);
	}

	// The sound ran out before the picture: once what is buffered has played, the wall clock carries the picture to its end. Otherwise the
	// clock would stop with the last sound and the last pictures would never finish.
	void VideoPlayer::check_audio_end(i64 wallUs)
	{
		if (!m_Audio || m_State != VideoState::Playing || !m_Clock.audio_master() || m_AwaitingSeek)
		{
			return;
		}
		bool audioEnded = false;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			audioEnded = m_AudioEnded;
		}
		if (audioEnded && m_Audio->buffered_frames() == 0)
		{
			m_Clock.use_wall_from(wallUs);
		}
	}

	void VideoPlayer::poll_open()
	{
		if (m_Opened)
		{
			return;
		}
		bool ok = false;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (!m_OpenDone)
			{
				return;
			}
			ok = m_OpenOk;
			m_Info = m_OpenInfo;
			m_Error = m_OpenError;
		}
		m_Opened = true;

		if (!ok)
		{
			m_State = VideoState::Failed;
			finish({ false, true });
			WI_VIDEO_LOG(Error, "Video failed to open: {0}", m_Error);
			return;
		}

		m_FrameDurationUs = static_cast<i64>(1e6 / m_Info.Fps);

		if (m_Info.HasAudio && m_Desc.AudioMode != VideoAudioMode::Off)
		{
			Scope<VideoAudioOut> audio = CreateScope<VideoAudioOut>();
			std::string audioError;
			if (audio->init(m_Info.SampleRate, m_Info.Channels, m_Desc.AudioMode == VideoAudioMode::Manual, audioError))
			{
				audio->set_clock(&m_Clock);
				audio->set_volume(m_Desc.Volume);
				m_Clock.set_audio_master(true);
				m_Audio = std::move(audio);
			}
			else
			{
				WI_VIDEO_LOG(Warning, "Video plays without sound: {0}", audioError);
			}
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_AudioDecided = true;
		}
		m_Cv.notify_all();

		m_State = VideoState::Idle;
		clock_hold(0);
		if (m_PlayWanted)
		{
			m_PlayWanted = false;
			m_Requests.insert(m_Requests.begin(), { Request::Kind::Play, 0.0 });
		}
	}

	void VideoPlayer::begin_seek(i64 us)
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Serial++;
			m_SeekPending = true;
			m_SeekTargetUs = us;
			m_Ring.clear();
			m_Ended = false;
			m_Errored = false;
			m_AudioEnded = false;
		}
		m_Cv.notify_all();
		if (m_Audio)
		{
			m_Clock.set_audio_master(true); // a clock that fell back to the wall goes back to the sound
		}
		m_AwaitingSeek = true;
		clock_hold(us);
	}

	void VideoPlayer::apply_request(const Request& request, i64 wallUs)
	{
		switch (request.What)
		{
		case Request::Kind::Play:
			if (m_State == VideoState::Ended)
			{
				begin_seek(0);
				m_State = VideoState::Playing;
			}
			else if (m_State == VideoState::Idle || m_State == VideoState::Paused)
			{
				m_State = VideoState::Playing;
				if (!m_AwaitingSeek)
				{
					clock_run(wallUs);
				}
			}
			break;

		case Request::Kind::Pause:
			if (m_State == VideoState::Playing)
			{
				clock_stop(wallUs);
				m_State = VideoState::Paused;
			}
			break;

		case Request::Kind::Stop:
			if (m_State == VideoState::Playing || m_State == VideoState::Paused)
			{
				finish({ true, false });
			}
			if (m_State != VideoState::Idle)
			{
				begin_seek(0);
				m_State = VideoState::Idle;
			}
			break;

		case Request::Kind::Seek:
		{
			double seconds = request.Seconds;
			if (std::isnan(seconds))
			{
				seconds = 0.0;
			}
			const double limit = m_Info.DurationSeconds > 0.0 ? m_Info.DurationSeconds : 0.0;
			seconds = std::clamp(seconds, 0.0, limit);
			begin_seek(static_cast<i64>(seconds * 1e6));
			if (m_State == VideoState::Ended)
			{
				m_State = VideoState::Paused;
			}
			break;
		}
		}
	}

	void VideoPlayer::take_frames(i64 wallUs)
	{
		std::shared_ptr<VideoFrame> chosen;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Ring.empty())
			{
				return;
			}

			if (m_AwaitingSeek)
			{
				// the first picture after a seek is the one at or after the target: show it, and let the clock go
				chosen = std::move(m_Ring.front().Frame);
				m_Ring.pop_front();
			}
			else
			{
				i64 stamps[RING_CAPACITY];
				size_t count = 0;
				for (const RingItem& item : m_Ring)
				{
					if (count < RING_CAPACITY)
					{
						stamps[count++] = item.TimestampUs;
					}
				}
				const size_t index = choose_frame(std::span<const i64>(stamps, count), m_Clock.now_us(wallUs));
				if (index == static_cast<size_t>(-1))
				{
					return;
				}
				for (size_t i = 0; i < index; i++)
				{
					m_Ring.pop_front();
				}
				chosen = std::move(m_Ring.front().Frame);
				m_Ring.pop_front();
			}
		}
		m_Cv.notify_all();

		m_Current = std::move(chosen);
		m_FrameSerial++;
		if (m_AwaitingSeek)
		{
			m_AwaitingSeek = false;
			if (m_State == VideoState::Playing)
			{
				clock_run(wallUs);
			}
		}
	}

	void VideoPlayer::check_end(i64 wallUs)
	{
		if (m_State != VideoState::Playing)
		{
			return;
		}

		bool ringEmpty = false;
		bool ended = false;
		bool errored = false;
		std::string streamError;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			ringEmpty = m_Ring.empty();
			ended = m_Ended;
			errored = m_Errored;
			streamError = m_StreamError;
		}
		if (!ringEmpty || m_AwaitingSeek || (!ended && !errored))
		{
			return;
		}

		// everything decoded is on its way to the screen: end once the last picture has had its time
		const i64 lastEnd = m_Current ? m_Current->TimestampUs + m_FrameDurationUs : 0;
		if (m_Clock.now_us(wallUs) < lastEnd)
		{
			return;
		}

		if (errored)
		{
			m_State = VideoState::Failed;
			m_Error = streamError;
			clock_hold(lastEnd);
			finish({ false, true });
			WI_VIDEO_LOG(Error, "Video stopped: {0}", m_Error);
			return;
		}
		m_State = VideoState::Ended;
		clock_hold(lastEnd);
		finish({ false, false });
	}

	// ---- worker ----

	std::shared_ptr<VideoFrame> VideoPlayer::acquire_frame()
	{
		for (std::shared_ptr<VideoFrame>& slot : m_Pool)
		{
			if (slot.use_count() == 1)
			{
				return slot;
			}
		}
		if (m_Pool.size() < POOL_SIZE)
		{
			m_Pool.push_back(std::make_shared<VideoFrame>());
			return m_Pool.back();
		}
		return nullptr;
	}

	void VideoPlayer::service_audio()
	{
		if (!m_Audio)
		{
			return;
		}
		const u32 channels = m_Audio->channels();
		for (int pass = 0; pass < 8; pass++)
		{
			if (m_ChunkUsed >= m_Chunk.Frames)
			{
				if (m_Audio->free_frames() < AUDIO_CHUNK_FRAMES)
				{
					return;
				}
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					if (m_AudioEnded)
					{
						return;
					}
				}
				m_Chunk.Frames = 0;
				m_ChunkUsed = 0;
				const DecodeStatus status = m_Decoder->read_audio(m_Chunk);
				if (status == DecodeStatus::Pending)
				{
					m_Chunk.Frames = 0;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					return;
				}
				if (status != DecodeStatus::Ok)
				{
					// the end, or a broken sound track: the picture carries on without it (a broken picture is what fails a video)
					m_Chunk.Frames = 0;
					std::lock_guard<std::mutex> lock(m_Mutex);
					m_AudioEnded = true;
					return;
				}
			}
			m_ChunkUsed += m_Audio->write(m_Chunk.Samples.data() + static_cast<size_t>(m_ChunkUsed) * channels, m_Chunk.Frames - m_ChunkUsed);
			if (m_ChunkUsed < m_Chunk.Frames)
			{
				return; // the ring is full
			}
		}
	}

	void VideoPlayer::worker_main()
	{
		VideoInfo info;
		std::string error;
		bool ok = m_Decoder->open(m_Bytes, info, error);
		if (ok)
		{
			if (info.Width == 0 || info.Height == 0 || info.Width > VIDEO_MAX_DIMENSION || info.Height > VIDEO_MAX_DIMENSION)
			{
				error = "invalid video size " + std::to_string(info.Width) + "x" + std::to_string(info.Height);
				ok = false;
			}
			else if (!(info.Fps <= MAX_FPS))
			{
				error = "unsupported frame rate " + std::to_string(info.Fps);
				ok = false;
			}
			else if (!(info.Fps > 0.0f))
			{
				info.Fps = UNKNOWN_FPS;
			}
		}
		if (!ok && error.empty())
		{
			error = "the video could not be opened";
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_OpenOk = ok;
			m_OpenInfo = info;
			m_OpenError = error;
			m_OpenDone = true;
		}
		if (!ok)
		{
			m_Decoder->close();
			return;
		}

		// the main thread decides whether there is sound (and makes the output) on its next update: until then nothing is decoded
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Cv.wait(lock, [this]() { return m_Stop || m_AudioDecided; });
			if (m_Stop)
			{
				m_Decoder->close();
				return;
			}
		}

		if (!m_Audio)
		{
			m_Decoder->disable_audio(); // nobody reads the sound: a decoder must not queue it
		}

		const i64 frameDurationUs = static_cast<i64>(1e6 / info.Fps);
		for (;;)
		{
			u64 serial = 0;
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Cv.wait_for(lock, std::chrono::milliseconds(2), [this]() {
					const bool audioWanted = m_Audio && !m_AudioEnded && m_Audio->free_frames() >= AUDIO_CHUNK_FRAMES;
					return m_Stop || m_SeekPending || audioWanted || (!m_Ended && !m_Errored && m_Ring.size() < RING_CAPACITY);
				});
				if (m_Stop)
				{
					break;
				}
				if (m_SeekPending)
				{
					m_SeekPending = false;
					serial = m_Serial;
					const i64 target = m_SeekTargetUs;
					lock.unlock();
					m_Decoder->seek(target);
					m_SkipUntilUs = target;
					m_LoopBaseUs = 0;
					m_PassFrames = 0;
					m_Chunk.Frames = 0;
					m_ChunkUsed = 0;
					if (m_Audio)
					{
						m_Audio->flush(); // what was decoded for before the seek is never heard
					}
					continue;
				}
				serial = m_Serial;
			}

			service_audio();
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				if (m_Ended || m_Errored || m_Ring.size() >= RING_CAPACITY)
				{
					continue;
				}
			}

			std::shared_ptr<VideoFrame> frame = acquire_frame();
			if (!frame)
			{
				continue; // every buffer is in use (the ring, the screen, an upload): the wait above polls again
			}

			switch (m_Decoder->read_video(*frame))
			{
			case DecodeStatus::Ok:
			{
				const i64 raw = frame->TimestampUs;
				if (raw < m_SkipUntilUs)
				{
					break; // before the seek target: decoded only to get there
				}
				m_SkipUntilUs = -1;
				m_LastRawUs = raw;
				m_PassFrames++;
				frame->TimestampUs = raw + m_LoopBaseUs;

				std::lock_guard<std::mutex> lock(m_Mutex);
				if (serial == m_Serial && !m_SeekPending)
				{
					m_Ring.push_back({ frame->TimestampUs, std::move(frame) });
				}
				break;
			}

			case DecodeStatus::Pending:
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				break;

			case DecodeStatus::End:
				if (m_Desc.Loop && m_PassFrames > 0)
				{
					// timestamps go on from where this pass ended, so the clock never goes back
					m_LoopBaseUs += m_LastRawUs + frameDurationUs;
					m_PassFrames = 0;
					m_SkipUntilUs = -1;
					m_Decoder->seek(0);
					m_Chunk.Frames = 0; // the sound starts over with the picture (what is already buffered still plays)
					m_ChunkUsed = 0;
					std::lock_guard<std::mutex> lock(m_Mutex);
					m_AudioEnded = false;
				}
				else
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					if (serial == m_Serial && !m_SeekPending)
					{
						m_Ended = true;
					}
				}
				break;

			case DecodeStatus::Error:
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				if (serial == m_Serial && !m_SeekPending)
				{
					m_Errored = true;
					m_StreamError = "the video stream is broken (decode error)";
				}
				break;
			}
			}
		}
		m_Decoder->close();
	}
}
