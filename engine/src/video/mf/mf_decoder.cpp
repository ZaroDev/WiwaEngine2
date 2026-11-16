#include <pch.h>
#include "mf_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr DWORD VIDEO_STREAM = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		constexpr DWORD AUDIO_STREAM = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
		constexpr u32 MAX_AUDIO_CHANNELS = 8;

		std::string hr_text(HRESULT hr)
		{
			char text[16];
			std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(hr));
			return text;
		}

		class MfDecoder final : public IVideoDecoder
		{
		public:
			~MfDecoder() override { close(); }

			virtual bool open(std::span<const std::byte> bytes, VideoInfo& info, std::string& error) override
			{
				close();
				if (bytes.empty() || bytes.size() > 0x7FFFFFFFu)
				{
					error = bytes.empty() ? "the file is empty" : "the file is too large";
					return false;
				}

				// COM: a thread that is already single threaded keeps its mode (RPC_E_CHANGED_MODE) and is not uninitialised by us
				HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				m_CoInitialized = SUCCEEDED(hr);
				hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
				if (FAILED(hr))
				{
					error = "Media Foundation could not start (" + hr_text(hr) + ")";
					close();
					return false;
				}
				m_Started = true;

				ComPtr<IStream> stream;
				stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
				if (!stream)
				{
					error = "out of memory";
					close();
					return false;
				}
				ComPtr<IMFByteStream> byteStream;
				hr = MFCreateMFByteStreamOnStream(stream.Get(), &byteStream);
				if (FAILED(hr))
				{
					error = "the file could not be wrapped (" + hr_text(hr) + ")";
					close();
					return false;
				}

				ComPtr<IMFAttributes> attributes;
				MFCreateAttributes(&attributes, 1);
				if (attributes)
				{
					attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // lets the reader give RGB32
				}
				hr = MFCreateSourceReaderFromByteStream(byteStream.Get(), attributes.Get(), &m_Reader);
				if (FAILED(hr))
				{
					error = "not a video Media Foundation can read (" + hr_text(hr) + ")";
					close();
					return false;
				}

				m_Reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
				hr = m_Reader->SetStreamSelection(VIDEO_STREAM, TRUE);
				if (FAILED(hr))
				{
					error = "the file has no video stream";
					close();
					return false;
				}
				{
					ComPtr<IMFMediaType> type;
					MFCreateMediaType(&type);
					type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
					type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
					hr = m_Reader->SetCurrentMediaType(VIDEO_STREAM, nullptr, type.Get());
					if (FAILED(hr))
					{
						error = "the video cannot be decoded: its codec is not installed (" + hr_text(hr) + ")";
						close();
						return false;
					}
				}
				if (!read_video_type(error))
				{
					close();
					return false;
				}

				// the sound is optional: a file without it, or with a track that cannot be decoded, plays without
				m_HasAudio = false;
				if (SUCCEEDED(m_Reader->SetStreamSelection(AUDIO_STREAM, TRUE)))
				{
					ComPtr<IMFMediaType> type;
					MFCreateMediaType(&type);
					type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
					type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
					if (SUCCEEDED(m_Reader->SetCurrentMediaType(AUDIO_STREAM, nullptr, type.Get())))
					{
						ComPtr<IMFMediaType> current;
						if (SUCCEEDED(m_Reader->GetCurrentMediaType(AUDIO_STREAM, &current)))
						{
							UINT32 channels = 0;
							UINT32 rate = 0;
							current->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
							current->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
							if (channels >= 1 && channels <= MAX_AUDIO_CHANNELS && rate >= 8000 && rate <= 384000)
							{
								m_Channels = channels;
								m_SampleRate = rate;
								m_HasAudio = true;
							}
						}
					}
					if (!m_HasAudio)
					{
						m_Reader->SetStreamSelection(AUDIO_STREAM, FALSE);
					}
				}

				double duration = 0.0;
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(m_Reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)) && value.vt == VT_UI8)
				{
					duration = static_cast<double>(value.uhVal.QuadPart) / 1e7;
				}
				PropVariantClear(&value);

				info = VideoInfo{};
				info.Width = m_OutWidth;
				info.Height = m_OutHeight;
				info.Fps = m_Fps;
				info.DurationSeconds = duration;
				info.HasAudio = m_HasAudio;
				info.SampleRate = m_HasAudio ? m_SampleRate : 0;
				info.Channels = m_HasAudio ? m_Channels : 0;
				m_Opened = true;
				return true;
			}

			virtual DecodeStatus read_video(VideoFrame& out) override
			{
				if (!m_Opened)
				{
					return DecodeStatus::Error;
				}
				DWORD flags = 0;
				LONGLONG time = 0;
				ComPtr<IMFSample> sample;
				const HRESULT hr = m_Reader->ReadSample(VIDEO_STREAM, 0, nullptr, &flags, &time, &sample);
				if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
				{
					return DecodeStatus::Error;
				}
				if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
				{
					std::string ignored;
					if (!read_video_type(ignored))
					{
						return DecodeStatus::Error;
					}
				}
				if (!sample)
				{
					return (flags & MF_SOURCE_READERF_ENDOFSTREAM) ? DecodeStatus::End : DecodeStatus::Pending;
				}

				ComPtr<IMFMediaBuffer> buffer;
				if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
				{
					return DecodeStatus::Error;
				}
				out.Width = m_OutWidth;
				out.Height = m_OutHeight;
				out.TimestampUs = time / 10;
				out.Bgra.resize(static_cast<size_t>(m_OutWidth) * m_OutHeight * 4);
				return copy_picture(buffer.Get(), out) ? DecodeStatus::Ok : DecodeStatus::Error;
			}

			virtual DecodeStatus read_audio(AudioChunk& out) override
			{
				if (!m_Opened || !m_HasAudio)
				{
					return DecodeStatus::Error;
				}
				DWORD flags = 0;
				LONGLONG time = 0;
				ComPtr<IMFSample> sample;
				const HRESULT hr = m_Reader->ReadSample(AUDIO_STREAM, 0, nullptr, &flags, &time, &sample);
				if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
				{
					return DecodeStatus::Error;
				}
				if (!sample)
				{
					return (flags & MF_SOURCE_READERF_ENDOFSTREAM) ? DecodeStatus::End : DecodeStatus::Pending;
				}

				ComPtr<IMFMediaBuffer> buffer;
				if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
				{
					return DecodeStatus::Error;
				}
				BYTE* data = nullptr;
				DWORD length = 0;
				if (FAILED(buffer->Lock(&data, nullptr, &length)))
				{
					return DecodeStatus::Error;
				}
				const u32 frames = static_cast<u32>(length / (sizeof(float) * m_Channels));
				out.Frames = frames;
				out.TimestampUs = time / 10;
				out.Samples.resize(static_cast<size_t>(frames) * m_Channels);
				std::memcpy(out.Samples.data(), data, out.Samples.size() * sizeof(float));
				buffer->Unlock();
				return frames > 0 ? DecodeStatus::Ok : DecodeStatus::Pending;
			}

			virtual bool seek(i64 us) override
			{
				if (!m_Opened)
				{
					return false;
				}
				PROPVARIANT position;
				PropVariantInit(&position);
				position.vt = VT_I8;
				position.hVal.QuadPart = std::max<i64>(us, 0) * 10;
				const HRESULT hr = m_Reader->SetCurrentPosition(GUID_NULL, position);
				PropVariantClear(&position);
				return SUCCEEDED(hr);
			}

			virtual void disable_audio() override
			{
				if (m_Reader && m_HasAudio)
				{
					m_Reader->SetStreamSelection(AUDIO_STREAM, FALSE);
					m_HasAudio = false;
				}
			}

			virtual void close() override
			{
				m_Reader.Reset();
				m_Opened = false;
				m_HasAudio = false;
				if (m_Started)
				{
					MFShutdown();
					m_Started = false;
				}
				if (m_CoInitialized)
				{
					CoUninitialize();
					m_CoInitialized = false;
				}
			}

		private:
			// The size, frame rate, stride and visible area of the pictures the reader gives now (they can change in the middle of a stream).
			bool read_video_type(std::string& error)
			{
				ComPtr<IMFMediaType> current;
				const HRESULT hr = m_Reader->GetCurrentMediaType(VIDEO_STREAM, &current);
				if (FAILED(hr))
				{
					error = "the video format is unknown (" + hr_text(hr) + ")";
					return false;
				}
				UINT32 width = 0;
				UINT32 height = 0;
				if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0)
				{
					error = "the video has no size";
					return false;
				}
				UINT32 numerator = 0;
				UINT32 denominator = 0;
				if (SUCCEEDED(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && denominator != 0)
				{
					m_Fps = static_cast<float>(numerator) / static_cast<float>(denominator);
				}

				UINT32 stride = 0;
				if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
				{
					m_Stride = static_cast<INT32>(stride);
				}
				else
				{
					LONG computed = 0;
					m_Stride = SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &computed)) ? static_cast<INT32>(computed) : -static_cast<INT32>(width * 4);
				}

				// the part of the coded picture that is shown (a 1080 line picture is often coded as 1088 lines)
				m_CropX = 0;
				m_CropY = 0;
				m_CodedWidth = width;
				m_CodedHeight = height;
				m_OutWidth = width;
				m_OutHeight = height;
				MFVideoArea area{};
				UINT32 got = 0;
				if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &got)) && got == sizeof(area))
				{
					const u32 x = static_cast<u32>(std::max<LONG>(0, area.OffsetX.value));
					const u32 y = static_cast<u32>(std::max<LONG>(0, area.OffsetY.value));
					const u32 w = static_cast<u32>(std::max<LONG>(0, area.Area.cx));
					const u32 h = static_cast<u32>(std::max<LONG>(0, area.Area.cy));
					if (w > 0 && h > 0 && x + w <= width && y + h <= height)
					{
						m_CropX = x;
						m_CropY = y;
						m_OutWidth = w;
						m_OutHeight = h;
					}
				}
				return true;
			}

			// RGB32 (B, G, R, unused) to opaque BGRA, top row first, cropped to the visible area.
			bool copy_picture(IMFMediaBuffer* buffer, VideoFrame& out)
			{
				ComPtr<IMF2DBuffer> flat;
				BYTE* top = nullptr;
				LONG pitch = 0;
				bool locked2D = false;
				BYTE* data1D = nullptr;
				DWORD length1D = 0;
				if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&flat))) && SUCCEEDED(flat->Lock2D(&top, &pitch)))
				{
					locked2D = true; // the first row is the top one, whatever the sign of the pitch
				}
				else
				{
					if (FAILED(buffer->Lock(&data1D, nullptr, &length1D)))
					{
						return false;
					}
					const LONG stride = m_Stride != 0 ? m_Stride : -static_cast<LONG>(m_CodedWidth * 4);
					pitch = stride;
					// a negative stride is a picture stored bottom row first
					top = stride < 0 ? data1D + static_cast<size_t>(-stride) * (m_CodedHeight - 1) : data1D;
					if (static_cast<size_t>(std::abs(stride)) * m_CodedHeight > length1D)
					{
						buffer->Unlock();
						return false;
					}
				}

				for (u32 y = 0; y < m_OutHeight; y++)
				{
					const BYTE* source = top + static_cast<ptrdiff_t>(pitch) * (m_CropY + y) + static_cast<size_t>(m_CropX) * 4;
					u8* target = out.Bgra.data() + static_cast<size_t>(y) * m_OutWidth * 4;
					for (u32 x = 0; x < m_OutWidth; x++)
					{
						target[x * 4 + 0] = source[x * 4 + 0];
						target[x * 4 + 1] = source[x * 4 + 1];
						target[x * 4 + 2] = source[x * 4 + 2];
						target[x * 4 + 3] = 255; // the fourth byte of RGB32 is unused (often 0): the picture is opaque
					}
				}

				if (locked2D)
				{
					flat->Unlock2D();
				}
				else
				{
					buffer->Unlock();
				}
				return true;
			}

			ComPtr<IMFSourceReader> m_Reader{};
			bool m_CoInitialized = false;
			bool m_Started = false;
			bool m_Opened = false;
			bool m_HasAudio = false;
			u32 m_CodedWidth = 0;
			u32 m_CodedHeight = 0;
			u32 m_OutWidth = 0;
			u32 m_OutHeight = 0;
			u32 m_CropX = 0;
			u32 m_CropY = 0;
			INT32 m_Stride = 0;
			float m_Fps = 0.0f;
			u32 m_Channels = 0;
			u32 m_SampleRate = 0;
		};
	}

	Scope<IVideoDecoder> create_mf_decoder() { return CreateScope<MfDecoder>(); }
}
