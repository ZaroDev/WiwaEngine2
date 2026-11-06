#include <editor_common.h>
#include "animation_panel.h"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include <animation/animation_module.h>
#include <core/engine.h>
#include <renderer/render_module.h>

#include "../asset_pipeline/meta_file.h"
#include "animation_preview.h"

namespace
{
	const ImU32 MARKER_COLOR = IM_COL32(255, 190, 70, 255);
	const ImU32 MARKER_HOT_COLOR = IM_COL32(255, 240, 160, 255);
	const ImU32 PLAYHEAD_COLOR = IM_COL32(110, 190, 255, 255);
	const float MARKER_PICK_RADIUS = 7.0f;
}

AnimationPanel::AnimationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, std::function<bool()> editing)
	: Panel("Animation", PanelCategory::Window), m_Editor(editor), m_Database(database), m_Editing(std::move(editing))
{
}

AnimationPanel::~AnimationPanel()
{
	end_preview();
}

bool AnimationPanel::open_events(const std::filesystem::path& modelSource, const std::vector<std::pair<std::string, float>>& clips)
{
	const std::optional<wiwa::MetaFile> meta = wiwa::MetaFile::load(wiwa::MetaFile::meta_path_for(modelSource));
	if (!meta)
	{
		m_Error = "cannot read the .meta of " + modelSource.string();
		return false;
	}
	m_Model = CreateScope<wiwa::AnimationEditModel>(wiwa::AnimationEditModel::from_settings(clips, meta->Settings));
	m_Source = modelSource;
	m_Clip = 0;
	m_Error.clear();
	return true;
}

bool AnimationPanel::save()
{
	if (m_Model == nullptr || m_Source.empty())
	{
		m_Error = "there are no events open";
		return false;
	}

	const std::filesystem::path metaPath = wiwa::MetaFile::meta_path_for(m_Source);
	std::optional<wiwa::MetaFile> meta = wiwa::MetaFile::load(metaPath);
	if (!meta)
	{
		m_Error = "cannot read " + metaPath.string();
		return false;
	}
	m_Model->write_to(meta->Settings);
	if (!meta->save(metaPath))
	{
		m_Error = "cannot write " + metaPath.string();
		return false;
	}
	m_Model->mark_saved();
	m_Error.clear();
	return true;
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

// The panel follows the selection: the clips and events of the selected Animator, and a preview that goes with it.
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
		m_Model.reset();
		m_Source.clear();
		m_Clip = 0;
		m_Dragging = -1;
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

	if (m_Model == nullptr && !m_Clips.empty() && m_Error.empty())
	{
		std::vector<std::pair<std::string, float>> clips;
		for (const ClipInfo& clip : m_Clips)
		{
			const wiwa::AnimationClipData* data = clip.Handle.get();
			if (data == nullptr)
			{
				return; // still loading
			}
			clips.emplace_back(data->Name, data->Duration);
		}
		const std::optional<std::filesystem::path> source = m_Database.path_of(m_Clips.front().Ref.Guid);
		if (source)
		{
			open_events(*source, clips);
		}
		else
		{
			m_Error = "the model of these clips has no source file";
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
		if (!hasAnimator && m_Model == nullptr)
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

			if (m_Model != nullptr)
			{
				ImGui::Separator();
				ImGui::BeginDisabled(!m_Model->can_undo());
				if (ImGui::Button("Undo")) { m_Model->undo(); }
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!m_Model->can_redo());
				if (ImGui::Button("Redo")) { m_Model->redo(); }
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!m_Model->dirty());
				if (ImGui::Button("Save events")) { save(); }
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextDisabled("%s", m_Model->dirty() ? "unsaved" : "saved");

				timeline();
				event_list();
			}
			else if (m_Error.empty())
			{
				ImGui::TextDisabled("Loading the clips...");
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
	if (animation == nullptr || m_Model == nullptr)
	{
		return;
	}
	if (!(m_Editing ? m_Editing() : true))
	{
		ImGui::TextDisabled("The preview is for the edit mode");
		return;
	}

	const std::vector<wiwa::EditableClip>& clips = m_Model->clips();
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

// The events of the clip on a bar: a click on empty space adds one, a marker can be dragged (the move is one undo step, made on release).
void AnimationPanel::timeline()
{
	const std::vector<wiwa::EditableClip>& clips = m_Model->clips();
	if (clips.empty())
	{
		return;
	}
	const size_t clipIndex = std::min(m_Clip, clips.size() - 1);
	const wiwa::EditableClip& clip = clips[clipIndex];
	const float duration = std::max(clip.Duration, 0.001f);

	ImGui::Text("Events of %s (%.2f s)", clip.Name.c_str(), clip.Duration);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 50.0f), 44.0f);
	ImGui::InvisibleButton("##timeline", size);
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(30, 30, 36, 255));
	for (int tick = 0; tick <= 10; tick++)
	{
		const float x = origin.x + size.x * static_cast<float>(tick) / 10.0f;
		draw->AddLine(ImVec2(x, origin.y + size.y - 8.0f), ImVec2(x, origin.y + size.y), IM_COL32(110, 110, 120, 255));
	}

	auto time_at = [&](float x) { return std::clamp((x - origin.x) / size.x, 0.0f, 1.0f) * clip.Duration; };
	auto x_of = [&](float time) { return origin.x + size.x * std::clamp(time / duration, 0.0f, 1.0f); };

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	int hot = -1;
	if (hovered || m_Dragging >= 0)
	{
		for (size_t i = 0; i < clip.Events.size(); i++)
		{
			if (std::abs(mouse.x - x_of(clip.Events[i].Time)) <= MARKER_PICK_RADIUS)
			{
				hot = static_cast<int>(i);
				break;
			}
		}
	}

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hot >= 0)
		{
			m_Dragging = hot;
		}
		else
		{
			m_Model->add_event(clipIndex, time_at(mouse.x), "event");
		}
	}
	float dragTime = 0.0f;
	if (m_Dragging >= 0)
	{
		dragTime = time_at(mouse.x);
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			m_Model->move_event(clipIndex, static_cast<size_t>(m_Dragging), dragTime);
			m_Dragging = -1;
		}
	}

	for (size_t i = 0; i < clip.Events.size(); i++)
	{
		const bool dragged = m_Dragging == static_cast<int>(i);
		const float x = x_of(dragged ? dragTime : clip.Events[i].Time);
		const float y = origin.y + size.y * 0.4f;
		const ImU32 color = dragged || hot == static_cast<int>(i) ? MARKER_HOT_COLOR : MARKER_COLOR;
		draw->AddQuadFilled(ImVec2(x, y - 7.0f), ImVec2(x + 6.0f, y), ImVec2(x, y + 7.0f), ImVec2(x - 6.0f, y), color);
		draw->AddText(ImVec2(x + 8.0f, origin.y + 2.0f), IM_COL32(210, 210, 220, 255), clip.Events[i].Name.c_str());
	}

	if (m_Previewed != entt::null && m_Clip == clipIndex)
	{
		const float x = x_of(m_PreviewTime);
		draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), PLAYHEAD_COLOR, 2.0f);
	}
}

// The events as rows: rename and retime by typing, delete with the button.
void AnimationPanel::event_list()
{
	const std::vector<wiwa::EditableClip>& clips = m_Model->clips();
	if (clips.empty())
	{
		return;
	}
	const size_t clipIndex = std::min(m_Clip, clips.size() - 1);
	const wiwa::EditableClip& clip = clips[clipIndex];

	if (clip.Events.empty())
	{
		ImGui::TextDisabled("No events: click the bar above to add one");
		return;
	}

	// edits are applied after the loop: they change the list that is being drawn
	enum class Action { None, Rename, Move, Remove };
	Action action = Action::None;
	size_t target = 0;
	std::string newName;
	float newTime = 0.0f;

	for (size_t i = 0; i < clip.Events.size(); i++)
	{
		ImGui::PushID(static_cast<int>(i));

		float time = clip.Events[i].Time;
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputFloat("##time", &time, 0.0f, 0.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit() && time != clip.Events[i].Time)
		{
			action = Action::Move;
			target = i;
			newTime = time;
		}

		ImGui::SameLine();
		char buffer[wiwa::AnimationEditModel::MAX_NAME_LENGTH + 1] = {};
		std::strncpy(buffer, clip.Events[i].Name.c_str(), sizeof(buffer) - 1);
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputText("##name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::IsItemDeactivatedAfterEdit() && clip.Events[i].Name != buffer)
		{
			action = Action::Rename;
			target = i;
			newName = buffer;
		}

		ImGui::SameLine();
		if (ImGui::Button("Delete"))
		{
			action = Action::Remove;
			target = i;
		}
		ImGui::PopID();
	}

	switch (action)
	{
	case Action::Rename: m_Model->rename_event(clipIndex, target, newName); break;
	case Action::Move: m_Model->move_event(clipIndex, target, newTime); break;
	case Action::Remove: m_Model->remove_event(clipIndex, target); break;
	case Action::None: break;
	}
}

// The joints as lines from each to its parent, over the scene: the pose of the animator if it runs, else the rest pose.
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
