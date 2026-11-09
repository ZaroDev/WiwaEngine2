#pragma once

#include <filesystem>
#include <string>

#include <math/math.h>

class AssetEditorPanel;

namespace wiwa
{
	class AssetDatabase;

	// --vfx-editor-smoke (editor.md): imports a bright effect, opens it in the Asset Editor, and checks over the next frames that the editor draws, that the
	// preview plays it on the GPU (green pixels in the preview image), that a silenced preview shows none (the negative control), and that the edit model
	// (undo, gestures, solo and mute, validation, save) behaves. update() is called every frame; it returns true when the run is over.
	class VfxEditorSmoke
	{
	public:
		VfxEditorSmoke(AssetEditorPanel& panel, AssetDatabase& database) : m_Panel(panel), m_Database(database) {}
		~VfxEditorSmoke();

		bool update(u32 frame, std::string& failure);

	private:
		std::string check_model();
		void read_back_lit(u32& into, bool& done);

		AssetEditorPanel& m_Panel;
		AssetDatabase& m_Database;
		std::filesystem::path m_Dir{};
		std::filesystem::path m_File{};

		u32 m_Lit = 0;          // green pixels of the playing preview
		bool m_LitDone = false;
		u32 m_LitSilent = 0;    // and of the silenced one
		bool m_LitSilentDone = false;
		u32 m_MaxAlive = 0;
		std::string m_ModelFailure{};
		bool m_ModelChecked = false;
	};
}
