#include <editor_common.h>
#include "audio_section.h"

#include <assets/asset_manager.h>
#include <audio/audio_module.h>
#include <core/engine.h>

#include <asset_pipeline/audio_settings_file.h>

AudioSection::AudioSection(std::filesystem::path file, bool applyToEngine)
	: m_File(std::move(file)), m_ApplyToEngine(applyToEngine)
{
	std::string error;
	if (!wiwa::load_audio_settings(m_File, m_Settings, error))
	{
		WI_WARN("Audio", "{0}: {1}", m_File.string(), error);
	}
}

bool AudioSection::load()
{
	wiwa::AudioProjectSettings loaded;
	std::string error;
	if (!wiwa::load_audio_settings(m_File, loaded, error))
	{
		m_Error = error;
		return false;
	}
	m_Error.clear();
	m_Settings = loaded;
	apply();
	return true;
}

bool AudioSection::save()
{
	m_Error.clear();
	return wiwa::save_audio_settings(m_File, m_Settings, m_Error);
}

void AudioSection::apply()
{
	if (!m_ApplyToEngine)
	{
		return;
	}
	wiwa::AudioModule* module = wiwa::Engine::get().get_module<wiwa::AudioModule>();
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	if (module != nullptr && assets != nullptr)
	{
		module->apply_settings(*assets, m_Settings);
	}
}

bool AudioSection::change(wiwa::AudioProjectSettings next)
{
	next = wiwa::sanitized(std::move(next));
	if (next == m_Settings)
	{
		return true;
	}

	const wiwa::AudioProjectSettings previous = m_Settings;
	m_Settings = std::move(next);
	if (!save())
	{
		m_Settings = previous;
		return false;
	}
	apply();
	return true;
}

bool AudioSection::set_backend(wiwa::AudioBackendKind backend)
{
	wiwa::AudioProjectSettings next = m_Settings;
	next.Backend = backend;
	return change(std::move(next));
}

bool AudioSection::add_bank(const wiwa::AssetRef& bank)
{
	wiwa::AudioProjectSettings next = m_Settings;
	next.Banks.push_back(bank);
	return change(std::move(next));
}

bool AudioSection::remove_bank(size_t index)
{
	if (index >= m_Settings.Banks.size())
	{
		return false;
	}
	wiwa::AudioProjectSettings next = m_Settings;
	next.Banks.erase(next.Banks.begin() + static_cast<std::ptrdiff_t>(index));
	return change(std::move(next));
}

bool AudioSection::set_memory_pool(wiwa::u32 kilobytes)
{
	wiwa::AudioProjectSettings next = m_Settings;
	next.MemoryPoolKb = kilobytes;
	return change(std::move(next));
}

bool AudioSection::set_language(const std::string& language)
{
	wiwa::AudioProjectSettings next = m_Settings;
	next.Language = language;
	return change(std::move(next));
}
