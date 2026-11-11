#pragma once

#include <string>
#include <string_view>

#include <core/common.h>

// Vendor-neutral audio types. No vendor header (AK::, FMOD::) may be included from here or from audio_backend.h.
namespace wiwa
{
	// A lookup key for a name (FNV-1a). Each backend maps names to its own ids.
	using AudioId = u32;

	inline AudioId audio_id(std::string_view name)
	{
		u32 hash = 2166136261u;
		for (const char c : name)
		{
			hash ^= static_cast<u8>(c);
			hash *= 16777619u;
		}
		return hash;
	}

	struct EmitterHandle
	{
		u32 Value = 0;
		bool valid() const { return Value != 0; }
		bool operator==(const EmitterHandle&) const = default;
	};

	struct PlayingId
	{
		u64 Value = 0;
		bool valid() const { return Value != 0; }
		bool operator==(const PlayingId&) const = default;
	};

	struct AudioVec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		bool operator==(const AudioVec3&) const = default;
	};

	// Position and orientation of an emitter or the listener (Forward and Up are unit vectors).
	struct AudioTransform
	{
		AudioVec3 Position{};
		AudioVec3 Forward{ 0.0f, 0.0f, -1.0f };
		AudioVec3 Up{ 0.0f, 1.0f, 0.0f };
		bool operator==(const AudioTransform&) const = default;
	};

	// Written to files (project settings): append, never reorder.
	enum class AudioBackendKind : u8
	{
		Null,
		Wwise,
		MiniAudio,
	};

	inline const char* audio_backend_name(AudioBackendKind kind)
	{
		switch (kind)
		{
		case AudioBackendKind::Null: return "Null";
		case AudioBackendKind::Wwise: return "Wwise";
		case AudioBackendKind::MiniAudio: return "MiniAudio";
		}
		return "Null";
	}

	struct AudioInitInfo
	{
		u32 MemoryPoolKb = 16384;
		std::string Language = "English(US)";
	};
}
