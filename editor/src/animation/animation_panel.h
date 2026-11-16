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

// The "Animation" panel: for the selected entity with an Animator it previews a clip in the viewport (play, pause, scrub, speed, loop; also while
// the scene is edited, never saved, dropped when the selection changes or the game plays). Clips are edited (keys, events) in the Asset Editor.
// Main thread, between the ImGui frame's begin and end.
class AnimationPanel final : public Panel
{
public:
	// `editing` says whether the scene is being edited (not played); the preview only exists then.
	AnimationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, std::function<bool()> editing);
	virtual ~AnimationPanel() override;

	virtual void on_draw() override;

	const std::string& last_error() const { return m_Error; }
private:
	struct ClipInfo
	{
		wiwa::AssetRef Ref{};
		wiwa::Handle<wiwa::AnimationClipData> Handle{};
	};

	void follow_selection(bool editing);
	void end_preview();
	void preview_controls(entt::entity entity);
	void draw_skeleton(entt::entity entity);

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	std::function<bool()> m_Editing;

	entt::entity m_Entity = entt::null;       // the entity the clips below belong to
	std::vector<ClipInfo> m_Clips{};

	entt::entity m_Previewed = entt::null;
	size_t m_Clip = 0;                         // the clip the preview and the timeline show
	bool m_PreviewPlaying = false;
	bool m_PreviewLoop = true;
	float m_PreviewSpeed = 1.0f;
	float m_PreviewTime = 0.0f;
	bool m_ShowSkeleton = false;
	std::string m_Error{};
};
