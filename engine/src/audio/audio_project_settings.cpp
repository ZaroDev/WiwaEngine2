#include <pch.h>
#include "audio_project_settings.h"

#include <algorithm>

namespace wiwa
{
	AudioProjectSettings sanitized(AudioProjectSettings settings)
	{
		settings.MemoryPoolKb = std::clamp(settings.MemoryPoolKb, AUDIO_POOL_MIN_KB, AUDIO_POOL_MAX_KB);
		if (settings.Language.empty())
		{
			settings.Language = AudioProjectSettings{}.Language;
		}

		std::vector<AssetRef> banks;
		for (const AssetRef& bank : settings.Banks)
		{
			if (bank.valid() && std::none_of(banks.begin(), banks.end(), [&](const AssetRef& kept) { return kept.resolved() == bank.resolved(); }))
			{
				banks.push_back(bank);
			}
		}
		settings.Banks = std::move(banks);
		return settings;
	}

	std::optional<AudioBackendKind> audio_backend_from_name(const std::string& name)
	{
		if (name == "Null")
		{
			return AudioBackendKind::Null;
		}
		if (name == "Wwise")
		{
			return AudioBackendKind::Wwise;
		}
		if (name == "MiniAudio")
		{
			return AudioBackendKind::MiniAudio;
		}
		return std::nullopt;
	}

	void archive(BinaryArchive& ar, AudioProjectSettings& v)
	{
		u32 version = AUDIO_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != AUDIO_SETTINGS_VERSION)
		{
			ar.fail();
			return;
		}

		u8 backend = static_cast<u8>(v.Backend);
		ar.value(backend);
		ar.list(v.Banks, [&](AssetRef& bank) { ar.value(bank); });
		ar.value(v.MemoryPoolKb);
		ar.string(v.Language);

		if (ar.reading() && ar.ok())
		{
			if (backend > static_cast<u8>(AudioBackendKind::MiniAudio) || ar.remaining() != 0)
			{
				ar.fail();
				return;
			}
			v.Backend = static_cast<AudioBackendKind>(backend);
		}
	}

	std::optional<AudioProjectSettings> read_audio_settings(IAssetSource& source)
	{
		if (!source.contains(AUDIO_SETTINGS_GUID))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(AUDIO_SETTINGS_GUID);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<AudioProjectSettings>(*bytes);
	}
}
