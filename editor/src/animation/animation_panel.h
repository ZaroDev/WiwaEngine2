#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../panels/panel.h"

#include <assets/asset_manager.h>
#include <scene/scene_editor.h>

#include "../asset_pipeline/asset_database.h"
#include "animation_edit_model.h"

// The "Animation" panel: for the selected entity with an Animator it previews a clip in the viewport (play, pause, scrub, speed, loop; also while
// the scene is edited, never saved, dropped when the selection changes or the game plays) and edits the clip events of the model on a timeline
// through an AnimationEditModel, so every edit can be undone. Save writes the events to the import settings of the model, which re-imports it.
// Main thread, between the ImGui frame's begin and end.
class AnimationPanel final : public Panel
{
public:
	// `editing` says whether the scene is being edited (not played); the preview only exists then.
	AnimationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, std::function<bool()> editing);
	virtual ~AnimationPanel() override;

	virtual void on_draw() override;

	// Opens the events of the clips of a model for editing: the source file of the model (its .meta holds the settings), and the name and
	// duration of each clip. False, and why in last_error, if the .meta cannot be read.
	bool open_events(const std::filesystem::path& modelSource, const std::vector<std::pair<std::string, float>>& clips);
	// Writes the events to the .meta of the model. False, and why in last_error, if there is none open or the write failed.
	bool save();
	wiwa::AnimationEditModel* model() { return m_Model.get(); }
	const std::filesystem::path& source() const { return m_Source; }

	const std::string& last_error() const { return m_Error; }
	wiwa::u32 frames_drawn() const { return m_Frames; }
	// Brings the window to the front of its dock on the next frame (the smoke check wants the contents drawn).
	void request_focus() { m_Focus = true; }

private:
	struct ClipInfo
	{
		wiwa::AssetRef Ref{};
		wiwa::Handle<wiwa::AnimationClipData> Handle{};
	};

	void follow_selection(bool editing);
	void end_preview();
	void preview_controls(entt::entity entity);
	void timeline();
	void event_list();
	void draw_skeleton(entt::entity entity);

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	std::function<bool()> m_Editing;

	entt::entity m_Entity = entt::null;       // the entity the clips below belong to
	std::vector<ClipInfo> m_Clips{};
	Scope<wiwa::AnimationEditModel> m_Model{};
	std::filesystem::path m_Source{};

	entt::entity m_Previewed = entt::null;
	size_t m_Clip = 0;                         // the clip the preview and the timeline show
	bool m_PreviewPlaying = false;
	bool m_PreviewLoop = true;
	float m_PreviewSpeed = 1.0f;
	float m_PreviewTime = 0.0f;
	bool m_ShowSkeleton = false;
	int m_Dragging = -1;                       // the event being dragged on the timeline
	std::string m_Error{};
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;
};
