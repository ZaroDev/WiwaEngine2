#pragma once

#include <filesystem>
#include <string>

#include <audio/audio_project_settings.h>

#include "settings_section.h"

// The project audio settings (audio.wsettings): the backend, the soundbanks to load, the memory pool and the language. Each setter saves at once
// and applies to the audio module; a failed save leaves the value as it was.
class AudioSection final : public ISettingsSection
{
public:
	// `file`: where the settings are saved (audio.wsettings in the editor working directory).
	// `applyToEngine` false: the settings are only saved (a check must not touch the running audio module from its own thread).
	explicit AudioSection(std::filesystem::path file, bool applyToEngine = true);

	virtual const char* name() const override { return "Audio"; }
	virtual bool load() override;
	virtual bool save() override;
	// Selects the backend and loads the banks. A backend that is not built in or fails falls back to Null; the reason is the module last_error().
	virtual void apply() override;
	virtual const std::string& last_error() const override { return m_Error; }

	const wiwa::AudioProjectSettings& settings() const { return m_Settings; }

	bool set_backend(wiwa::AudioBackendKind backend);
	bool add_bank(const wiwa::AssetRef& bank);
	bool remove_bank(size_t index);
	bool set_memory_pool(wiwa::u32 kilobytes);
	bool set_language(const std::string& language);

private:
	// Changes the settings, saves, applies; puts the old value back and reports false if the save failed.
	bool change(wiwa::AudioProjectSettings next);

	std::filesystem::path m_File;
	wiwa::AudioProjectSettings m_Settings{};
	std::string m_Error{};
	bool m_ApplyToEngine = true;
};
