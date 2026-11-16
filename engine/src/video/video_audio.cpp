#include <pch.h>
#include "video_audio.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <miniaudio.h> // the implementation is in audio/mini/miniaudio_impl.cpp

#include "video_clock.h"

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

namespace wiwa
{
	struct VideoAudioState
	{
		u32 SampleRate = 0;
		u32 Channels = 0;
		u32 Capacity = 0; // frames
		std::vector<float> Ring{};
		std::atomic<u64> Head{ 0 };   // frames read (consumer only)
		std::atomic<u64> Tail{ 0 };   // frames written (producer only)
		std::atomic<u64> FlushTo{ 0 };// the consumer skips to here (producer sets it to Tail)
		std::atomic<bool> Playing{ false };
		std::atomic<float> Volume{ 1.0f };
		std::atomic<u64> Played{ 0 };
		std::atomic<VideoClock*> Clock{ nullptr };
		bool NoDevice = true;
		bool DeviceUp = false;
		std::unique_ptr<ma_device> Device{};

		// The device callback and render(): fills out with frames from the ring, silence for the rest. Returns the frames that came from the ring.
		u32 pull(float* out, u32 frames)
		{
			const size_t samples = static_cast<size_t>(frames) * Channels;
			u32 taken = 0;
			if (Playing.load(std::memory_order_acquire))
			{
				const u64 flush = FlushTo.load(std::memory_order_acquire);
				u64 head = Head.load(std::memory_order_relaxed);
				if (head < flush)
				{
					head = flush;
				}
				const u64 tail = Tail.load(std::memory_order_acquire);
				const u64 available = tail > head ? tail - head : 0;
				taken = static_cast<u32>(std::min<u64>(available, frames));
				for (u32 f = 0; f < taken; f++)
				{
					const size_t slot = static_cast<size_t>((head + f) % Capacity) * Channels;
					std::memcpy(out + static_cast<size_t>(f) * Channels, Ring.data() + slot, sizeof(float) * Channels);
				}
				Head.store(head + taken, std::memory_order_release);

				const float volume = Volume.load(std::memory_order_relaxed);
				if (volume != 1.0f)
				{
					for (size_t i = 0; i < static_cast<size_t>(taken) * Channels; i++)
					{
						out[i] *= volume;
					}
				}
			}
			std::memset(out + static_cast<size_t>(taken) * Channels, 0, (samples - static_cast<size_t>(taken) * Channels) * sizeof(float));

			if (taken > 0)
			{
				Played.fetch_add(taken, std::memory_order_relaxed);
				if (VideoClock* clock = Clock.load(std::memory_order_acquire))
				{
					clock->add_audio_frames(taken, SampleRate);
				}
			}
			return taken;
		}
	};

	namespace
	{
		void device_callback(ma_device* device, void* output, const void*, ma_uint32 frames)
		{
			static_cast<VideoAudioState*>(device->pUserData)->pull(static_cast<float*>(output), frames);
		}
	}

	VideoAudioOut::VideoAudioOut() : m_State(std::make_unique<VideoAudioState>()) {}

	VideoAudioOut::~VideoAudioOut() { shutdown(); }

	bool VideoAudioOut::init(u32 sampleRate, u32 channels, bool noDevice, std::string& error)
	{
		VideoAudioState& state = *m_State;
		if (sampleRate == 0 || channels == 0 || channels > 8)
		{
			error = "unsupported audio format (" + std::to_string(sampleRate) + " Hz, " + std::to_string(channels) + " channels)";
			return false;
		}
		state.SampleRate = sampleRate;
		state.Channels = channels;
		state.Capacity = std::max<u32>(sampleRate / 2, 4096); // half a second
		state.Ring.assign(static_cast<size_t>(state.Capacity) * channels, 0.0f);
		state.NoDevice = noDevice;

		if (!noDevice)
		{
			state.Device = std::make_unique<ma_device>();
			ma_device_config config = ma_device_config_init(ma_device_type_playback);
			config.playback.format = ma_format_f32;
			config.playback.channels = channels;
			config.sampleRate = sampleRate;
			config.dataCallback = &device_callback;
			config.pUserData = &state;
			if (ma_device_init(nullptr, &config, state.Device.get()) != MA_SUCCESS)
			{
				state.Device.reset();
				error = "no sound device could be opened";
				return false;
			}
			if (ma_device_start(state.Device.get()) != MA_SUCCESS)
			{
				ma_device_uninit(state.Device.get());
				state.Device.reset();
				error = "the sound device could not be started";
				return false;
			}
			state.DeviceUp = true;
		}
		return true;
	}

	void VideoAudioOut::shutdown()
	{
		VideoAudioState& state = *m_State;
		state.Playing.store(false, std::memory_order_release);
		if (state.Device)
		{
			// uninit stops the device and waits for a callback in progress: nothing reads the ring or the clock after this
			ma_device_uninit(state.Device.get());
			state.Device.reset();
			state.DeviceUp = false;
		}
		state.Clock.store(nullptr, std::memory_order_release);
	}

	void VideoAudioOut::set_clock(VideoClock* clock) { m_State->Clock.store(clock, std::memory_order_release); }

	u32 VideoAudioOut::write(const float* interleaved, u32 frames)
	{
		VideoAudioState& state = *m_State;
		if (state.Capacity == 0)
		{
			return 0;
		}
		const u64 tail = state.Tail.load(std::memory_order_relaxed);
		u64 head = state.Head.load(std::memory_order_acquire);
		head = std::max(head, state.FlushTo.load(std::memory_order_relaxed));
		const u64 used = tail > head ? tail - head : 0;
		const u32 room = used >= state.Capacity ? 0 : static_cast<u32>(state.Capacity - used);
		const u32 count = std::min(room, frames);
		for (u32 f = 0; f < count; f++)
		{
			const size_t slot = static_cast<size_t>((tail + f) % state.Capacity) * state.Channels;
			std::memcpy(state.Ring.data() + slot, interleaved + static_cast<size_t>(f) * state.Channels, sizeof(float) * state.Channels);
		}
		state.Tail.store(tail + count, std::memory_order_release);
		return count;
	}

	void VideoAudioOut::flush()
	{
		m_State->FlushTo.store(m_State->Tail.load(std::memory_order_relaxed), std::memory_order_release);
	}

	u32 VideoAudioOut::buffered_frames() const
	{
		const VideoAudioState& state = *m_State;
		const u64 head = std::max(state.Head.load(std::memory_order_acquire), state.FlushTo.load(std::memory_order_acquire));
		const u64 tail = state.Tail.load(std::memory_order_acquire);
		return tail > head ? static_cast<u32>(tail - head) : 0;
	}

	u32 VideoAudioOut::free_frames() const
	{
		const u32 used = buffered_frames();
		return used >= m_State->Capacity ? 0 : m_State->Capacity - used;
	}

	void VideoAudioOut::start() { m_State->Playing.store(true, std::memory_order_release); }
	void VideoAudioOut::stop() { m_State->Playing.store(false, std::memory_order_release); }
	bool VideoAudioOut::playing() const { return m_State->Playing.load(std::memory_order_acquire); }
	void VideoAudioOut::set_volume(float volume) { m_State->Volume.store(std::clamp(volume, 0.0f, 4.0f), std::memory_order_relaxed); }
	u64 VideoAudioOut::played_frames() const { return m_State->Played.load(std::memory_order_relaxed); }
	u32 VideoAudioOut::sample_rate() const { return m_State->SampleRate; }
	u32 VideoAudioOut::channels() const { return m_State->Channels; }

	void VideoAudioOut::render(u32 frames)
	{
		VideoAudioState& state = *m_State;
		if (!state.NoDevice || state.Channels == 0)
		{
			return;
		}
		std::vector<float> scratch(static_cast<size_t>(frames) * state.Channels);
		state.pull(scratch.data(), frames);
	}
}
