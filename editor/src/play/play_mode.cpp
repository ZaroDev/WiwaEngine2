#include <editor_common.h>
#include "play_mode.h"

#include <scene/scene_clone.h>

namespace wiwa
{
	void PlayMode::play()
	{
		if (m_State != PlayState::Edit || !m_Host.can_play())
		{
			return;
		}

		m_EditScene = m_Host.edit_scene();
		if (m_EditScene == nullptr)
		{
			return;
		}

		m_Document = m_Host.capture_document();
		m_PlayScene = clone_scene(*m_EditScene);
		m_Host.show_scene(m_PlayScene, true);
		m_Host.time_play();
		m_Host.start_scripts();
		m_Step = Step::None;
		m_State = PlayState::Playing;
	}

	void PlayMode::pause()
	{
		if (m_State != PlayState::Playing)
		{
			return;
		}
		m_Host.time_pause(true);
		m_State = PlayState::Paused;
	}

	void PlayMode::resume()
	{
		if (m_State != PlayState::Paused)
		{
			return;
		}
		m_Host.time_pause(false);
		m_Step = Step::None;
		m_State = PlayState::Playing;
	}

	void PlayMode::step()
	{
		if (m_State == PlayState::Paused && m_Step == Step::None)
		{
			m_Step = Step::Requested;
		}
	}

	void PlayMode::stop()
	{
		if (m_State == PlayState::Edit)
		{
			return;
		}

		m_Host.stop_scripts(); // on_destroy runs while the scene it ran on is still the active one
		m_Host.cancel_loads();
		m_Host.time_stop();
		m_Host.show_scene(m_EditScene, false);
		m_Host.restore_document(m_EditScene, m_Document);

		m_PlayScene.reset();
		m_EditScene.reset();
		m_Step = Step::None;
		m_State = PlayState::Edit;
	}

	void PlayMode::update()
	{
		if (m_State == PlayState::Edit)
		{
			return;
		}

		// a script loaded another scene: that is the scene that runs now, and its scripts begin
		const Ref<Scene> active = m_Host.active_scene();
		if (active != nullptr && active != m_PlayScene)
		{
			m_PlayScene = active;
			m_Host.stop_scripts(); // the scripts may still hold the scene that was replaced, or already have stopped: start() needs them stopped
			m_Host.start_scripts();
		}

		if (m_Step == Step::Requested)
		{
			m_Host.time_pause(false);
			m_Step = Step::Running;
		}
		else if (m_Step == Step::Running)
		{
			m_Host.time_pause(true);
			m_Step = Step::None;
		}
	}
}
