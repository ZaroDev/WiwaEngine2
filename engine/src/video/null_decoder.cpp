#include <pch.h>
#include "null_decoder.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace wiwa
{
	namespace
	{
		constexpr u32 AUDIO_CHUNK_FRAMES = 1024;

		class NullDecoder final : public IVideoDecoder
		{
		public:
			explicit NullDecoder(const NullDecoderConfig& config) : m_Config(config) {}

			virtual bool open(std::span<const std::byte>, VideoInfo& info, std::string& error) override
			{
				if (m_Config.OpenDelayMs > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(m_Config.OpenDelayMs));
				}
				if (m_Config.FailOpen)
				{
					error = "null decoder: open failed (FailOpen)";
					return false;
				}
				info.Width = m_Config.Width;
				info.Height = m_Config.Height;
				info.Fps = m_Config.Fps;
				info.DurationSeconds = m_Config.DurationSeconds;
				info.HasAudio = m_Config.HasAudio;
				info.SampleRate = m_Config.HasAudio ? m_Config.SampleRate : 0;
				info.Channels = m_Config.HasAudio ? m_Config.Channels : 0;
				m_FrameCount = static_cast<i64>(std::floor(m_Config.DurationSeconds * m_Config.Fps + 1e-6));
				m_Opened = true;
				return true;
			}

			virtual DecodeStatus read_video(VideoFrame& out) override
			{
				if (!m_Opened || m_Failed)
				{
					return DecodeStatus::Error;
				}
				m_Calls++;
				if (m_Config.PendingEvery != 0 && m_Calls % m_Config.PendingEvery == 0)
				{
					return DecodeStatus::Pending;
				}
				if (m_Config.FailAfterFrames >= 0 && m_ReadSinceOpen >= m_Config.FailAfterFrames)
				{
					m_Failed = true;
					return DecodeStatus::Error;
				}
				if (m_NextFrame >= m_FrameCount)
				{
					return DecodeStatus::End;
				}

				out.Width = m_Config.Width;
				out.Height = m_Config.Height;
				out.TimestampUs = static_cast<i64>(std::llround(static_cast<double>(m_NextFrame) * 1e6 / m_Config.Fps));
				out.Bgra.resize(static_cast<size_t>(out.Width) * out.Height * 4);
				for (size_t i = 0; i < out.Bgra.size(); i++)
				{
					out.Bgra[i] = static_cast<u8>(i * 7 + static_cast<size_t>(m_NextFrame));
				}
				const u32 index = static_cast<u32>(m_NextFrame);
				if (out.Bgra.size() >= sizeof(index))
				{
					std::memcpy(out.Bgra.data(), &index, sizeof(index));
				}
				m_NextFrame++;
				m_ReadSinceOpen++;
				return DecodeStatus::Ok;
			}

			virtual DecodeStatus read_audio(AudioChunk& out) override
			{
				if (!m_Opened || !m_Config.HasAudio)
				{
					return DecodeStatus::Error;
				}
				const double audioSeconds = m_Config.AudioDurationSeconds >= 0.0 ? m_Config.AudioDurationSeconds : m_Config.DurationSeconds;
				const i64 total = static_cast<i64>(audioSeconds * m_Config.SampleRate);
				if (m_AudioPos >= total)
				{
					return DecodeStatus::End;
				}
				const u32 frames = static_cast<u32>(std::min<i64>(AUDIO_CHUNK_FRAMES, total - m_AudioPos));
				out.Frames = frames;
				out.TimestampUs = m_AudioPos * 1'000'000 / m_Config.SampleRate;
				out.Samples.resize(static_cast<size_t>(frames) * m_Config.Channels);
				for (u32 f = 0; f < frames; f++)
				{
					const double t = static_cast<double>(m_AudioPos + f) / m_Config.SampleRate;
					const float value = 0.25f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t));
					for (u32 c = 0; c < m_Config.Channels; c++)
					{
						out.Samples[static_cast<size_t>(f) * m_Config.Channels + c] = value;
					}
				}
				m_AudioPos += frames;
				return DecodeStatus::Ok;
			}

			virtual bool seek(i64 us) override
			{
				if (!m_Opened)
				{
					return false;
				}
				us = std::max<i64>(us, 0);
				m_NextFrame = static_cast<i64>(std::ceil(static_cast<double>(us) * m_Config.Fps / 1e6 - 1e-9));
				m_AudioPos = std::max<i64>(0, us - m_Config.AudioSeekBackUs) * m_Config.SampleRate / 1'000'000;
				m_Failed = false;
				return true;
			}

			virtual void close() override { m_Opened = false; }

		private:
			NullDecoderConfig m_Config;
			bool m_Opened = false;
			bool m_Failed = false;
			i64 m_FrameCount = 0;
			i64 m_NextFrame = 0;
			i64 m_ReadSinceOpen = 0;
			i64 m_AudioPos = 0;
			u32 m_Calls = 0;
		};
	}

	Scope<IVideoDecoder> create_null_decoder(const NullDecoderConfig& config) { return CreateScope<NullDecoder>(config); }
}
