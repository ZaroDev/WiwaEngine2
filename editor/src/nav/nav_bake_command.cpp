#include <editor_common.h>
#include "nav_bake_command.h"

#include <core/engine.h>

#include "nav_edit_model.h"
#include "../scene/scene_editor.h"

namespace wiwa
{
	NavBakeCommand::NavBakeCommand(const std::vector<std::string>& arguments)
	{
		for (size_t i = 0; i + 1 < arguments.size(); i++)
		{
			if (arguments[i] == "--nav-bake")
			{
				m_Out = arguments[i + 1];
			}
		}
	}

	void NavBakeCommand::update(SceneEditor& scene, NavEditModel& model, u32 frame, bool assetScanFinished)
	{
		if (!active())
		{
			return;
		}

		if (!m_Started && frame > FIRST_FRAME && assetScanFinished && scene.get_scene() != nullptr && !scene.is_opening() && !scene.has_pending())
		{
			entt::entity volume = entt::null;
			for (const entt::entity entity : scene.scene().registry().view<NavMeshVolume>())
			{
				if (scene.scene().registry().get<NavMeshVolume>(entity).Enabled && (volume == entt::null || entt::to_integral(entity) < entt::to_integral(volume)))
				{
					volume = entity;
				}
			}

			// no volume yet: the scene may still be the empty startup scene, so look again next frame until the give-up frame
			if (volume != entt::null || frame >= GIVE_UP_FRAME)
			{
				m_Started = true;
				if (volume == entt::null || !model.start_bake(scene.scene(), volume, m_Out))
				{
					WI_ERROR("Editor", "Nav bake: {0}", volume == entt::null ? std::string("the scene has no enabled NavMeshVolume") : model.error());
					Engine::get().set_exit_code(1);
					Engine::get().request_quit();
				}
			}
		}

		if (m_Started && model.state() != NavEditModel::BakeState::Running && model.state() != NavEditModel::BakeState::Idle)
		{
			model.poll();
			const bool done = model.state() == NavEditModel::BakeState::Done;
			if (done)
			{
				WI_INFO("Editor", "Nav bake done: {0} ({1} polygons)", m_Out.string(), model.last().PolyCount);
			}
			else
			{
				WI_ERROR("Editor", "Nav bake FAILED: {0}", model.error());
			}
			Engine::get().set_exit_code(done ? 0 : 1);
			Engine::get().request_quit();
			m_Out.clear();
		}
	}
}
