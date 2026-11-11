#pragma once

#include <filesystem>
#include <string>

#include <audio/audio_project_settings.h>

// The project audio settings (audio.wsettings, in the editor's working directory next to input.wsettings): JSON with "backend" ("Null" or "Wwise"),
// "banks" (the guids, 32 hex characters, of the SoundBank assets), "memoryPoolKb" and "language". The packager puts them into assets.wpak
// (audio/audio_project_settings.h), the Settings page edits them.
namespace wiwa
{
	constexpr const char* AUDIO_SETTINGS_FILE = "audio.wsettings";

	// Missing keys keep their defaults, unknown keys are ignored, values are sanitized. False, and why in error, for text that is not a JSON object
	// or holds a key of the wrong type, an unknown backend or a bad guid.
	bool audio_settings_from_text(const std::string& text, AudioProjectSettings& out, std::string& error);
	std::string audio_settings_to_text(const AudioProjectSettings& settings);

	// A file that does not exist gives the default settings and true; a file that cannot be read or parsed gives false and the default.
	bool load_audio_settings(const std::filesystem::path& path, AudioProjectSettings& out, std::string& error);
	bool save_audio_settings(const std::filesystem::path& path, const AudioProjectSettings& settings, std::string& error);
}
