#include <pch.h>
#include "mf_clip_writer.h"

#include <algorithm>
#include <cmath>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace wiwa
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		std::string hr_text(const char* what, HRESULT hr)
		{
			char text[64];
			std::snprintf(text, sizeof(text), " failed (0x%08X)", static_cast<unsigned>(hr));
			return std::string(what) + text;
		}

		// The picture of frame n: bars of seven colours that scroll to the left, a white square that crosses the picture over the length of the clip.
		void paint_frame(u8* bgra, u32 width, u32 height, u32 frame, u32 frames)
		{
			static const u8 bars[7][3] = { { 255, 255, 255 }, { 0, 255, 255 }, { 255, 255, 0 }, { 0, 255, 0 }, { 255, 0, 255 }, { 0, 0, 255 }, { 255, 0, 0 } }; // R, G, B
			const u32 barWidth = std::max(width / 7, 1u);
			const u32 square = std::max(height / 6, 2u);
			const u32 squareX = frames > 1 ? static_cast<u32>(static_cast<u64>(width - square) * frame / (frames - 1)) : 0;
			const u32 squareY = (height - square) / 2;
			for (u32 y = 0; y < height; y++)
			{
				for (u32 x = 0; x < width; x++)
				{
					const u32 shifted = (x + frame * 4) / barWidth % 7;
					u8 r = bars[shifted][0], g = bars[shifted][1], b = bars[shifted][2];
					const u32 shade = 96 + 159 * y / height; // darker toward the top: the picture is not flat
					r = static_cast<u8>(r * shade / 255);
					g = static_cast<u8>(g * shade / 255);
					b = static_cast<u8>(b * shade / 255);
					if (x >= squareX && x < squareX + square && y >= squareY && y < squareY + square)
					{
						r = g = b = 255;
					}
					u8* pixel = bgra + (static_cast<size_t>(y) * width + x) * 4;
					pixel[0] = b;
					pixel[1] = g;
					pixel[2] = r;
					pixel[3] = 255;
				}
			}
		}
	}

	bool write_mf_test_clip(const std::filesystem::path& file, const MfClipConfig& config, std::string& error)
	{
		if (config.Width < 16 || config.Height < 16 || config.Width % 2 != 0 || config.Height % 2 != 0 || config.Width > 4096 || config.Height > 4096 ||
			config.Fps == 0 || config.Fps > 120 || !(config.Seconds > 0.0) || config.Seconds > 600.0 || (config.SampleRate != 44100 && config.SampleRate != 48000))
		{
			error = "unsupported clip settings";
			return false;
		}

		const bool coInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		bool started = false;
		bool ok = false;
		{
			HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
			if (FAILED(hr))
			{
				error = hr_text("MFStartup", hr);
			}
			else
			{
				started = true;
				ComPtr<IMFAttributes> attributes;
				MFCreateAttributes(&attributes, 2);
				attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
				attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);

				std::error_code ignored;
				std::filesystem::remove(file, ignored);
				ComPtr<IMFSinkWriter> writer;
				hr = MFCreateSinkWriterFromURL(file.wstring().c_str(), nullptr, attributes.Get(), &writer);
				if (FAILED(hr))
				{
					error = hr_text("MFCreateSinkWriterFromURL", hr);
				}
				else
				{
					const u32 channels = 2;
					DWORD videoStream = 0;
					DWORD audioStream = 0;
					bool configured = true;

					// the picture: RGB32 in, H.264 out
					{
						ComPtr<IMFMediaType> out;
						MFCreateMediaType(&out);
						out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
						out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
						out->SetUINT32(MF_MT_AVG_BITRATE, 1'500'000);
						out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
						out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
						MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, config.Width, config.Height);
						MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, config.Fps, 1);
						MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
						hr = writer->AddStream(out.Get(), &videoStream);

						ComPtr<IMFMediaType> in;
						MFCreateMediaType(&in);
						in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
						in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
						in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
						in->SetUINT32(MF_MT_DEFAULT_STRIDE, config.Width * 4); // positive: the rows are top first
						MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, config.Width, config.Height);
						MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, config.Fps, 1);
						MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
						if (SUCCEEDED(hr))
						{
							hr = writer->SetInputMediaType(videoStream, in.Get(), nullptr);
						}
						if (FAILED(hr))
						{
							error = hr_text("configuring the H.264 stream", hr);
							configured = false;
						}
					}

					// the sound: PCM 16 bit in, AAC out
					if (configured)
					{
						ComPtr<IMFMediaType> out;
						MFCreateMediaType(&out);
						out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
						out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
						out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
						out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, config.SampleRate);
						out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
						out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbit/s
						out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
						out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
						hr = writer->AddStream(out.Get(), &audioStream);

						ComPtr<IMFMediaType> in;
						MFCreateMediaType(&in);
						in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
						in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
						in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
						in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, config.SampleRate);
						in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
						in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
						in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, config.SampleRate * channels * 2);
						if (SUCCEEDED(hr))
						{
							hr = writer->SetInputMediaType(audioStream, in.Get(), nullptr);
						}
						if (FAILED(hr))
						{
							error = hr_text("configuring the AAC stream", hr);
							configured = false;
						}
					}

					if (configured)
					{
						hr = writer->BeginWriting();
						if (FAILED(hr))
						{
							error = hr_text("BeginWriting", hr);
							configured = false;
						}
					}

					if (configured)
					{
						const u32 frames = std::max(1u, static_cast<u32>(std::llround(config.Seconds * config.Fps)));
						const LONGLONG frameTicks = 10'000'000LL / config.Fps;
						const u32 audioChunk = config.SampleRate / config.Fps; // the sound of one picture
						u64 audioWritten = 0;
						const u64 audioTotal = static_cast<u64>(std::llround(config.Seconds * config.SampleRate));
						double phase = 0.0;

						bool failed = false;
						for (u32 frame = 0; frame < frames && !failed; frame++)
						{
							// picture
							{
								const DWORD bytes = config.Width * config.Height * 4;
								ComPtr<IMFMediaBuffer> buffer;
								ComPtr<IMFSample> sample;
								BYTE* data = nullptr;
								if (FAILED(MFCreateMemoryBuffer(bytes, &buffer)) || FAILED(buffer->Lock(&data, nullptr, nullptr)))
								{
									error = "out of memory";
									failed = true;
									break;
								}
								paint_frame(data, config.Width, config.Height, frame, frames);
								buffer->Unlock();
								buffer->SetCurrentLength(bytes);
								MFCreateSample(&sample);
								sample->AddBuffer(buffer.Get());
								sample->SetSampleTime(frameTicks * frame);
								sample->SetSampleDuration(frameTicks);
								hr = writer->WriteSample(videoStream, sample.Get());
								if (FAILED(hr))
								{
									error = hr_text("writing a picture", hr);
									failed = true;
									break;
								}
							}

							// sound: one steady tone per half, written with its picture
							const u64 chunk = std::min<u64>(audioChunk, audioTotal - audioWritten);
							if (chunk > 0)
							{
								const DWORD bytes = static_cast<DWORD>(chunk * channels * 2);
								ComPtr<IMFMediaBuffer> buffer;
								ComPtr<IMFSample> sample;
								BYTE* data = nullptr;
								if (FAILED(MFCreateMemoryBuffer(bytes, &buffer)) || FAILED(buffer->Lock(&data, nullptr, nullptr)))
								{
									error = "out of memory";
									failed = true;
									break;
								}
								i16* pcm = reinterpret_cast<i16*>(data);
								for (u64 i = 0; i < chunk; i++)
								{
									const bool firstHalf = (audioWritten + i) * 2 < audioTotal;
									const double hz = firstHalf ? config.FirstHz : config.SecondHz;
									phase += 2.0 * 3.14159265358979323846 * hz / config.SampleRate;
									const i16 value = static_cast<i16>(std::lround(std::sin(phase) * 0.5 * 32767.0));
									for (u32 c = 0; c < channels; c++)
									{
										pcm[i * channels + c] = value;
									}
								}
								buffer->Unlock();
								buffer->SetCurrentLength(bytes);
								MFCreateSample(&sample);
								sample->AddBuffer(buffer.Get());
								sample->SetSampleTime(static_cast<LONGLONG>(audioWritten) * 10'000'000LL / config.SampleRate);
								sample->SetSampleDuration(static_cast<LONGLONG>(chunk) * 10'000'000LL / config.SampleRate);
								hr = writer->WriteSample(audioStream, sample.Get());
								if (FAILED(hr))
								{
									error = hr_text("writing sound", hr);
									failed = true;
									break;
								}
								audioWritten += chunk;
							}
						}

						if (!failed)
						{
							hr = writer->Finalize();
							if (FAILED(hr))
							{
								error = hr_text("Finalize", hr);
							}
							else
							{
								ok = true;
							}
						}
					}
				}
			}
		}

		if (started)
		{
			MFShutdown();
		}
		if (coInitialized)
		{
			CoUninitialize();
		}
		return ok;
	}
}
