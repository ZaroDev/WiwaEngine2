#include <editor_common.h>
#include "animation_panel.h"

#include <algorithm>

#include <imgui.h>

#include <animation/animation_module.h>
#include <core/engine.h>
#include <renderer/render_module.h>

#include "../asset_pipeline/asset_database.h"
#include "../asset_pipeline/file_utils.h"
#include "animation_preview.h"

AnimationPanel::AnimationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, std::function<bool()> editing)
	: Panel("Animation", PanelCategory::Window), m_Editor(editor), m_Database(database), m_Editing(std::move(editing))
{
}

AnimationPanel::~AnimationPanel()
{
	end_preview();
}

void AnimationPanel::end_preview()
{
	if (m_Previewed != entt::null)
	{
		if (wiwa::AnimationModule* animation = wiwa::Engine::get().get_module<wiwa::AnimationModule>())
		{
			animation->drop_previews();
		}
	}
	m_Previewed = entt::null;
	m_PreviewPlaying = false;
	m_PreviewTime = 0.0f;
}

// The panel follows the selection: the clips of the selected Animator, and a preview that goes with it.
void AnimationPanel::follow_selection(bool editing)
{
	const entt::entity selected = m_Editor.get_scene() != nullptr ? m_Editor.selected() : entt::null;
	if (!wiwa::preview_continues(m_Previewed, selected, editing))
	{
		end_preview();
	}

	if (selected != m_Entity)
	{
		m_Entity = selected;
		m_Clips.clear();
		m_Clip = 0;
	}

	if (m_Entity == entt::null || m_Editor.get_scene() == nullptr || !m_Editor.scene().registry().valid(m_Entity))
	{
		return;
	}
	const wiwa::Animator* animator = m_Editor.scene().registry().try_get<wiwa::Animator>(m_Entity);
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	if (animator == nullptr || assets == nullptr)
	{
		return;
	}

	if (m_Clips.empty())
	{
		for (const wiwa::AssetRef& ref : animator->Clips)
		{
			m_Clips.push_back(ClipInfo{ ref, assets->load<wiwa::AnimationClipData>(ref) });
		}
	}

}

void AnimationPanel::on_draw()
{
	m_Frames++; // counted even while the window is a hidden tab: the smoke check wants to know the panel was run
	if (m_Focus)
	{
		ImGui::SetNextWindowFocus();
		m_Focus = false;
	}

	const bool editing = m_Editing ? m_Editing() : true;
	follow_selection(editing);

	ImGui::SetNextWindowPos(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(620.0f, 300.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const bool hasAnimator = m_Entity != entt::null && m_Editor.get_scene() != nullptr && m_Editor.scene().registry().valid(m_Entity)
			&& m_Editor.scene().registry().all_of<wiwa::Animator>(m_Entity);
		if (!hasAnimator)
		{
			ImGui::TextDisabled("Select an entity with an Animator");
		}
		else
		{
			ImGui::Checkbox("Show skeleton", &m_ShowSkeleton);
			if (hasAnimator)
			{
				if (m_ShowSkeleton)
				{
					draw_skeleton(m_Entity);
				}
				ImGui::Separator();
				preview_controls(m_Entity);
			}

			if (!m_Error.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());
			}
		}
	}
	ImGui::End();
}

void AnimationPanel::preview_controls(entt::entity entity)
{
	wiwa::AnimationModule* animation = wiwa::Engine::get().get_module<wiwa::AnimationModule>();
	if (animation == nullptr)
	{
		return;
	}
	if (!(m_Editing ? m_Editing() : true))
	{
		ImGui::TextDisabled("The preview is for the edit mode");
		return;
	}

	// the clips of the Animator, once they have loaded (the events are edited in the Asset Editor)
	struct ClipRow
	{
		std::string Name{};
		float Duration = 0.0f;
	};
	std::vector<ClipRow> clips;
	for (const ClipInfo& info : m_Clips)
	{
		const wiwa::AnimationClipData* data = info.Handle.get();
		if (data == nullptr)
		{
			ImGui::TextDisabled("Loading the clips...");
			return;
		}
		clips.push_back(ClipRow{ data->Name, data->Duration });
	}
	if (clips.empty())
	{
		ImGui::TextDisabled("The model has no clips");
		return;
	}
	m_Clip = std::min(m_Clip, clips.size() - 1);

	auto start = [&]()
	{
		wiwa::AnimatorInstance* instance = animation->preview(entity);
		if (instance == nullptr)
		{
			return static_cast<wiwa::AnimatorInstance*>(nullptr);
		}
		instance->play(wiwa::ClipSelector{ clips[m_Clip].Name, {} }, wiwa::PlayOptions{ m_PreviewLoop, m_PreviewSpeed });
		m_Previewed = entity;
		return instance;
	};

	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo("Clip", clips[m_Clip].Name.c_str()))
	{
		for (size_t i = 0; i < clips.size(); i++)
		{
			if (ImGui::Selectable(clips[i].Name.c_str(), i == m_Clip) && i != m_Clip)
			{
				m_Clip = i;
				if (m_Previewed == entity)
				{
					start();
					animation->set_preview_paused(entity, !m_PreviewPlaying);
				}
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button(m_Previewed == entity && m_PreviewPlaying ? "Pause" : "Play"))
	{
		if (m_Previewed != entity && start() == nullptr)
		{
			m_Error = "the preview could not start";
		}
		else
		{
			m_PreviewPlaying = !(m_Previewed == entity && m_PreviewPlaying);
			animation->set_preview_paused(entity, !m_PreviewPlaying);
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(m_Previewed != entity);
	if (ImGui::Button("Stop preview"))
	{
		end_preview();
	}
	ImGui::EndDisabled();

	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::SliderFloat("Speed", &m_PreviewSpeed, 0.1f, 3.0f) && m_Previewed == entity)
	{
		const float time = m_PreviewTime;
		start();
		if (wiwa::AnimatorInstance* instance = animation->preview(entity))
		{
			instance->seek(time);
		}
		animation->set_preview_paused(entity, !m_PreviewPlaying);
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Loop", &m_PreviewLoop) && m_Previewed == entity)
	{
		start();
		animation->set_preview_paused(entity, !m_PreviewPlaying);
	}

	// the time of the preview, which can be scrubbed
	if (const wiwa::AnimatorInstance* instance = m_Previewed == entity ? animation->instance_for(entity) : nullptr)
	{
		m_PreviewTime = instance->time();
	}
	float time = m_PreviewTime;
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::SliderFloat("##scrub", &time, 0.0f, std::max(clips[m_Clip].Duration, 0.001f), "%.3f s"))
	{
		wiwa::AnimatorInstance* instance = m_Previewed == entity ? animation->preview(entity) : start();
		if (instance != nullptr)
		{
			m_PreviewPlaying = false;
			animation->set_preview_paused(entity, true);
			instance->seek(time);
			m_PreviewTime = time;
		}
	}
}

void AnimationPanel::draw_skeleton(entt::entity entity)
{
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	wiwa::RenderModule* renderer = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (assets == nullptr || renderer == nullptr)
	{
		return;
	}

	entt::registry& registry = m_Editor.scene().registry();
	const wiwa::Animator& animator = registry.get<wiwa::Animator>(entity);
	const wiwa::Handle<wiwa::SkeletonData> handle = assets->load<wiwa::SkeletonData>(animator.Skeleton);
	const wiwa::SkeletonData* skeleton = handle.get();
	if (skeleton == nullptr)
	{
		return;
	}

	std::vector<glm::mat4> rest;
	const std::vector<glm::mat4>* model = &rest;
	const wiwa::AnimationModule* animation = wiwa::Engine::get().get_module<wiwa::AnimationModule>();
	if (const wiwa::AnimatorInstance* instance = animation != nullptr ? animation->instance_for(entity) : nullptr; instance != nullptr && !instance->model().empty())
	{
		model = &instance->model();
	}
	else
	{
		wiwa::model_matrices(*skeleton, wiwa::rest_pose(*skeleton), rest);
	}

	const wiwa::WorldTransform* world = registry.try_get<wiwa::WorldTransform>(entity);
	const glm::mat4 root = world != nullptr ? world->Matrix : glm::mat4(1.0f);
	wiwa::DebugDraw& debug = renderer->get_debug_draw();
	for (size_t j = 0; j < model->size() && j < skeleton->Parents.size(); j++)
	{
		const glm::vec3 position = glm::vec3(root * (*model)[j][3]);
		debug.sphere(position, 0.02f, glm::vec4(1.0f, 0.8f, 0.2f, 1.0f), wiwa::DebugDepth::AlwaysOnTop);
		if (skeleton->Parents[j] >= 0)
		{
			const glm::vec3 parent = glm::vec3(root * (*model)[static_cast<size_t>(skeleton->Parents[j])][3]);
			debug.line(position, parent, glm::vec4(0.4f, 0.9f, 1.0f, 1.0f), wiwa::DebugDepth::AlwaysOnTop);
		}
	}
}
