#pragma once

#include <editor_common.h>
#include <asset_editor/vfx_editor_smoke.h>
#include <play/demo_smoke.h>

class EditorModule;

// The smokes that need the live editor: the panel smokes, --play-smoke, --demo-smoke and --vfx-editor-smoke. The editor calls update() once per frame
// through EditorTestSetup::OnFrame; this class is a friend of EditorModule for the panel pointers, the scene editor and the play mode.
class EditorSmokeDriver
{
public:
	explicit EditorSmokeDriver(const std::vector<std::string>& arguments);

	// True when one of the smokes the driver runs was asked for.
	bool any_requested() const;

	bool physics_panel_smoke() const { return m_PhysicsPanelSmoke; }
	bool input_panel_smoke() const { return m_InputPanelSmoke; }
	bool audio_panel_smoke() const { return m_AudioPanelSmoke; }
	bool nav_panel_smoke() const { return m_NavPanelSmoke; }

	void update(EditorModule& editor);

private:
	std::string run_play_smoke(EditorModule& editor);

	bool m_PlaySmoke = false;
	bool m_UiEditorSmoke = false;              // --ui-editor-smoke: open a panel in the UI Editor, run for a while, check it is previewing
	bool m_PhysicsPanelSmoke = false;          // --physics-panel-smoke: draw the Physics panel for a while with ImGui asserts on, edit the matrix, check it was saved; and the vehicle editor
	std::filesystem::path m_VehiclePanelFile{};
	std::string m_VehicleFailure{};
	bool m_InputPanelSmoke = false;            // --input-panel-smoke: draw the Input panel for a while with ImGui asserts on, edit a map through the panel, check it was saved
	bool m_AudioPanelSmoke = false;            // --audio-panel-smoke: draw the Audio panel for a while, audition an event through it, check it stops
	bool m_NavPanelSmoke = false;              // --nav-panel-smoke: draw the Navigation panel for a while, make a volume through it, check it
	bool m_AiPanelSmoke = false;               // --ai-panel-smoke: open a behaviour tree in the Asset Editor for a while with ImGui asserts on, edit it through the model, save it
	bool m_AnimPanelSmoke = false;             // --anim-panel-smoke: the Animation panel and the Graph editor (on the demo warrior's graph, whose preview must play it)
	bool m_VfxEditorSmokeRequested = false;    // --vfx-editor-smoke
	bool m_DemoSmokeRequested = false;         // --demo-smoke
	bool m_DemoSmokeDone = false;
	bool m_DemoSmokeQueued = false;            // the demo smoke is waiting for the startup scan (EditorModule::when_scan_finished)

	Scope<wiwa::DemoSmoke> m_DemoSmoke{};
	Scope<wiwa::VfxEditorSmoke> m_VfxEditorSmoke{};

	bool m_AudioPanelAuditioned = false;
	wiwa::u32 m_AudioNullFrame = 0;            // the frame the audio panel smoke switched to the Null backend (0: not yet)
	entt::entity m_NavPanelVolume{ entt::null };
	std::filesystem::path m_AiPanelFile{};
	std::string m_AiPanelFailure{};
	std::filesystem::path m_InputPanelFile{};
	std::filesystem::path m_UiEditorSmokeFile{};
	wiwa::u32 m_PlaySmokeFinalFrame = 0;
	Ref<wiwa::Scene> m_PlaySmokeEdit{};
	size_t m_PlaySmokeCount = 0;
};
