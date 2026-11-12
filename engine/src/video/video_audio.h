#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <math/math.h>

namespace wiwa
{
	class VideoClock;
	struct VideoAudioState;

	// The sound of a video: a lock free single producer / single consumer ring of interleaved float frames, drained by miniaudio's device thread
	// (or by render() when there is no device, for checks on a machine without a sound card). The decode worker writes, the device reads.
	//
	// The device runs from init() on and outputs silence while stopped: start() and stop() only gate whether the callback takes frames from the ring,
	// so pausing or seeking costs nothing. Frames that really came out of the ring are counted (played_frames(), and the clock when one is
	// set); the silence of an underrun is not, so the picture never runs ahead of sound that was not heard.
	// No miniaudio type is visible here (video_audio.cpp). Not owned by the audio backend: a video plays whatever the audio settings choose.
	class VideoAudioOut
	{
	public:
		VideoAudioOut();
		~VideoAudioOut();
		VideoAudioOut(const VideoAudioOut&) = delete;
		VideoAudioOut& operator=(const VideoAudioOut&) = delete;

		// noDevice: no sound card is opened; the ring is only drained by render(). False with a reason if the device cannot start.
		bool init(u32 sampleRate, u32 channels, bool noDevice, std::string& error);
		// Stops and releases the device (waits for a callback in progress). Safe to call twice. Nothing may write after it.
		void shutdown();

		// The clock the played frames are added to (from the device thread). It must outlive shutdown().
		void set_clock(VideoClock* clock);

		// Producer: how many of the frames fit now are accepted.
		u32 write(const float* interleaved, u32 frames);
		// Producer: everything written so far is dropped (a seek). Frames written after the call are kept.
		void flush();
		u32 buffered_frames() const;
		u32 free_frames() const;

		void start();
		void stop();
		bool playing() const;
		void set_volume(float volume);

		u64 played_frames() const;
		u32 sample_rate() const;
		u32 channels() const;

		// noDevice only: does what the device callback would for this many frames.
		void render(u32 frames);

	private:
		std::unique_ptr<VideoAudioState> m_State;
	};
}
