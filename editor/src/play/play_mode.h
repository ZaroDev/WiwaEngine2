#pragma once

#include <core/common.h>
#include <scene/scene.h>
#include <scene/scene_editor.h>

namespace wiwa
{
	// What play mode needs from the editor and the engine. PlayMode only ever calls these, so its logic is tested with a host that writes
	// the calls down; EditorModule supplies the real one.
	class IPlayHost
	{
	public:
		virtual ~IPlayHost() = default;

		virtual Ref<Scene> edit_scene() = 0;                          // the scene the editor shows now
		virtual bool can_play() = 0;                                  // false while a scene is opening or a prefab expansion is pending
		virtual EditorDocument capture_document() = 0;                // path, dirty flag and selection of the edit scene
		virtual void show_scene(Ref<Scene> scene, bool playing) = 0;  // makes it the SceneModule's active scene, and the editor's while playing
		virtual void restore_document(Ref<Scene> scene, const EditorDocument& document) = 0;
		virtual Ref<Scene> active_scene() = 0;                        // the SceneModule's active scene now
		virtual void start_scripts() = 0;
		virtual void stop_scripts() = 0;
		virtual void cancel_loads() = 0;                              // scene loads still running: a scene that arrives after Stop must not replace the edit scene
		virtual void time_play() = 0;
		virtual void time_stop() = 0;
		virtual void time_pause(bool paused) = 0;
	};

	enum class PlayState
	{
		Edit,
		Playing,
		Paused,
	};

	// Runs the scripts on a clone of the edit scene, so that nothing they do can change what is being edited. Stop puts the edit scene
	// back exactly as it was. Controls used in the wrong state do nothing. Main thread.
	class PlayMode
	{
	public:
		explicit PlayMode(IPlayHost& host) : m_Host(host) {}

		PlayState state() const { return m_State; }
		// The scene that is running (the clone, or the scene a script loaded in its place); null in Edit.
		Ref<Scene> play_scene() const { return m_PlayScene; }

		void play();
		void pause();
		void resume();
		// Only while paused: lets the scripts run for exactly one frame (the next update() unpauses, the one after pauses again).
		void step();
		void stop();

		// Once per frame, after the scripts ran (the editor's post_update).
		void update();

	private:
		enum class Step { None, Requested, Running };

		IPlayHost& m_Host;
		PlayState m_State = PlayState::Edit;
		Step m_Step = Step::None;
		Ref<Scene> m_EditScene{};
		Ref<Scene> m_PlayScene{};
		EditorDocument m_Document{};
	};
}
