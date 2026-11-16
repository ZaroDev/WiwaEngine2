#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <core/common.h>

#include "video_audio.h"
#include "video_clock.h"
#include "video_decoder.h"

namespace wiwa
{
	enum class VideoState : u32 { Idle, Loading, Playing, Paused, Ended, Failed };

	// Why a video stopped: it ran out (both false), the script stopped it (Skipped) or it broke (Failed).
	struct VideoFinished
	{
		bool Skipped = false;
		bool Failed = false;
	};

	// What plays the sound of a file that has some.
	enum class VideoAudioMode : u32
	{
		Off,    // no sound: the picture runs on the wall clock (the default, and what a file without a sound track gets)
		Device, // the sound card, through miniaudio; the played sound is the clock. If no device opens the player logs it and falls back to Off
		Manual, // no sound card: the checks drain the sound with audio()->render()
	};

	struct VideoPlayerDesc
	{
		bool Loop = false;
		float Volume = 1.0f;
		VideoAudioMode AudioMode = VideoAudioMode::Off;
	};

	// One video: a decode worker that owns the decoder, a short ring of decoded pictures, and the clock that says which picture is due. The main
	// thread calls update() once per frame with a monotonic wall time in microseconds (passed in so that a check can drive it).
	//
	// Threads: everything public is main thread. The worker (started by open()) is the only thread that touches the decoder. They share the
	// ring, the seek request and the end / error flags under one mutex. A seek bumps a serial; a picture decoded for an older serial is dropped,
	// so nothing from before a seek is ever shown. The destructor joins the worker before anything it uses is destroyed.
	class VideoPlayer
	{
	public:
		VideoPlayer(Scope<IVideoDecoder> decoder, const VideoPlayerDesc& desc);
		~VideoPlayer();
		VideoPlayer(const VideoPlayer&) = delete;
		VideoPlayer& operator=(const VideoPlayer&) = delete;

		// Starts the worker, which opens the bytes: the state is Loading until update() sees the result (Idle, or Failed with error()).
		void open(std::vector<std::byte> bytes);

		// Requests, applied by the next update() (which knows the time). play() on a video that is still loading plays once it is ready.
		void play();
		void pause();
		void stop();             // back to the first picture, Idle; a playing or paused video reports Finished{Skipped}
		void seek(double seconds); // clamped to 0..duration; NaN is 0

		void update(i64 wallUs);

		VideoState state() const { return m_State; }
		double time() const;
		double duration() const { return m_Info.DurationSeconds; }
		const VideoInfo& info() const { return m_Info; }
		const std::string& error() const { return m_Error; }

		// The picture due now (null before the first one), and a counter that changes whenever it does.
		std::shared_ptr<const VideoFrame> current_frame() const { return m_Current; }
		u64 frame_serial() const { return m_FrameSerial; }

		std::optional<VideoFinished> take_finished();

		void set_volume(float volume);
		bool uses_audio() const { return m_Audio != nullptr; }
		// Frames of sound that were decoded for a time before a seek target and dropped instead of played.
		u64 audio_frames_dropped() const { return m_AudioDropped.load(std::memory_order_relaxed); }
		VideoAudioOut* audio() const { return m_Audio.get(); }

	private:
		struct Request
		{
			enum class Kind : u32 { Play, Pause, Stop, Seek } What = Kind::Play;
			double Seconds = 0.0;
		};
		struct RingItem
		{
			i64 TimestampUs = 0;
			std::shared_ptr<VideoFrame> Frame{};
		};

		static constexpr size_t RING_CAPACITY = 4;
		static constexpr size_t POOL_SIZE = 6;

		void worker_main();
		std::shared_ptr<VideoFrame> acquire_frame(); // worker: a picture buffer nobody else holds, or null

		void poll_open();
		void apply_request(const Request& request, i64 wallUs);
		void begin_seek(i64 us);
		void take_frames(i64 wallUs);
		void check_end(i64 wallUs);
		void finish(VideoFinished how) { m_Finished.push_back(how); }
		void clock_run(i64 wallUs);   // the clock (and the sound) go
		void clock_stop(i64 wallUs);  // the clock and the sound stand still
		void clock_hold(i64 us);      // the clock stands at this time (a seek, the end)
		void check_audio_end(i64 wallUs);
		void service_audio();         // worker: moves decoded sound into the ring

		Scope<IVideoDecoder> m_Decoder;
		VideoPlayerDesc m_Desc;
		std::vector<std::byte> m_Bytes{}; // the decoder reads them for as long as it is open

		// main thread
		VideoState m_State = VideoState::Loading;
		VideoInfo m_Info{};
		std::string m_Error{};
		VideoClock m_Clock{};
		Scope<VideoAudioOut> m_Audio{};
		std::vector<Request> m_Requests{};
		std::deque<VideoFinished> m_Finished{};
		std::shared_ptr<const VideoFrame> m_Current{};
		u64 m_FrameSerial = 0;
		i64 m_WallUs = 0;
		i64 m_FrameDurationUs = 33333;
		bool m_Opened = false;       // poll_open consumed the worker's answer
		bool m_AwaitingSeek = false; // a seek was requested and its first picture has not arrived: the clock waits
		bool m_Started = false;      // open() ran
		i64 m_AwaitTargetUs = 0;     // the target of the seek whose first picture has not arrived

		// shared, under m_Mutex
		std::mutex m_Mutex{};
		std::condition_variable m_Cv{};
		std::thread m_Worker{};
		bool m_Stop = false;
		bool m_OpenDone = false;
		bool m_OpenOk = false;
		VideoInfo m_OpenInfo{};
		std::string m_OpenError{};
		std::deque<RingItem> m_Ring{};
		u64 m_Serial = 0;            // bumped by every seek
		bool m_SeekPending = false;
		i64 m_SeekTargetUs = 0;
		bool m_AudioDecided = false; // poll_open has made (or declined) the audio output: the worker may use m_Audio
		bool m_AudioEnded = false;   // the decoder is out of sound
		bool m_Ended = false;        // the decoder is out of pictures (never set while looping)
		bool m_Errored = false;
		std::string m_StreamError{};

		// worker only
		std::vector<std::shared_ptr<VideoFrame>> m_Pool{};
		i64 m_SkipUntilUs = -1;
		i64 m_LoopBaseUs = 0;
		i64 m_LastRawUs = 0;
		u32 m_PassFrames = 0;
		AudioChunk m_Chunk{};        // the sound being moved into the ring
		u32 m_ChunkUsed = 0;         // frames of m_Chunk already written
		i64 m_AudioSkipUntilUs = -1; // after a seek: sound before this time is dropped
		std::atomic<u64> m_AudioDropped{ 0 };
	};
}
