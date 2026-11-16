#include <editor_common.h>
#include "asset_preview.h"

#include <animation/clip_binding.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/view/view_camera.h>
#include <scene/components.h>
#include <scene/preview_scene.h>

using wiwa::LogLevel; // for WI_LOG
#define WI_PREVIEW_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr float FOV = 35.0f;

		AnimationModule* animation_module() { return Engine::get().get_module<AnimationModule>(); }
		AssetManager* asset_manager() { return Engine::get().get_module<AssetManager>(); }
	}

	entt::entity make_preview_entity(Scene& scene, const AssetRef& skeleton, const AssetRef& mesh, const AssetRef& clip, const AssetRef& graph)
	{
		const entt::entity entity = scene.create("Preview");
		Animator animator;
		animator.Skeleton = skeleton;
		animator.Clip = clip;
		animator.PlayOnStart = clip.valid();
		animator.Graph = graph; // a graph plays instead of the clip
		scene.registry().emplace<Animator>(entity, animator);
		if (mesh.valid())
		{
			scene.registry().emplace<MeshRenderer>(entity, MeshRenderer{ mesh, {} });
			scene.registry().emplace<SkinBinding>(entity, SkinBinding{ skeleton }); // the palette of the Animator on this entity skins it
		}
		scene.update_world_transforms();
		return entity;
	}

	void AssetPreview::release()
	{
		// the handle goes before the scene it shows
		m_View = RenderView{};
		if (AnimationModule* module = animation_module())
		{
			module->unregister_preview_scene(m_Scene);
		}
		m_Scene.reset();
		m_Entity = entt::null;
	}

	void AssetPreview::show(const AssetRef& skeleton, const AssetRef& mesh, const AssetRef& clip, const AssetRef& graph)
	{
		if (skeleton == m_Skeleton && mesh == m_Mesh && clip == m_Clip && graph == m_Graph && m_Scene != nullptr)
		{
			return;
		}
		const bool newSkeleton = !(skeleton == m_Skeleton) || !(mesh == m_Mesh);
		m_Skeleton = skeleton;
		m_Mesh = mesh;
		m_Clip = clip;
		m_Graph = graph;
		m_NeedFrame = m_NeedFrame || newSkeleton;
		rebuild();
	}


	// The scene gets one entity that is the Animator and the mesh at once (the extractor takes the Animator of the entity itself): a new one when
	// the skeleton, mesh or clip changes, so its instance starts from the new clip.
	void AssetPreview::rebuild()
	{
		AssetManager* assets = asset_manager();
		AnimationModule* module = animation_module();
		if (assets == nullptr || module == nullptr)
		{
			return;
		}
		if (m_Scene == nullptr)
		{
			m_Scene = make_preview_scene();
			module->register_preview_scene(m_Scene);
		}
		if (m_Entity != entt::null && m_Scene->registry().valid(m_Entity))
		{
			m_Scene->destroy(m_Entity);
		}
		m_Entity = entt::null;

		m_SkeletonHandle = m_Skeleton.valid() ? assets->load<SkeletonData>(m_Skeleton) : Handle<SkeletonData>{};
		m_MeshHandle = m_Mesh.valid() ? assets->load<MeshData>(m_Mesh) : Handle<MeshData>{};
		m_ClipHandle = m_Clip.valid() ? assets->load<AnimationClipData>(m_Clip) : Handle<AnimationClipData>{};
		if (!m_Skeleton.valid())
		{
			return;
		}

		m_Entity = make_preview_entity(*m_Scene, m_Skeleton, m_Mesh, m_Clip, m_Graph);
		module->set_preview_paused_in(m_Scene.get(), m_Entity, !m_Playing);
	}

	AnimatorInstance* AssetPreview::instance() const
	{
		AnimationModule* module = animation_module();
		return module != nullptr && m_Scene != nullptr && m_Entity != entt::null ? module->instance_in(m_Scene.get(), m_Entity) : nullptr;
	}

	bool AssetPreview::has_pose() const
	{
		const AnimatorInstance* current = instance();
		return current != nullptr && current->ready() && !current->model().empty();
	}

	glm::mat4 AssetPreview::joint_matrix(size_t joint) const
	{
		const AnimatorInstance* current = instance();
		return current != nullptr && joint < current->model().size() ? current->model()[joint] : glm::mat4(1.0f);
	}

	int AssetPreview::joint_index(const std::string& name) const
	{
		const AnimatorInstance* current = instance();
		if (current == nullptr || current->skeleton() == nullptr)
		{
			return -1;
		}
		const std::vector<std::string>& names = current->skeleton()->JointNames;
		const auto it = std::find(names.begin(), names.end(), name);
		return it == names.end() ? -1 : static_cast<int>(it - names.begin());
	}

	void AssetPreview::set_playing(bool playing)
	{
		m_Playing = playing;
		if (AnimationModule* module = animation_module())
		{
			module->set_preview_paused_in(m_Scene.get(), m_Entity, !playing);
		}
	}

	float AssetPreview::clip_time() const
	{
		const AnimatorInstance* current = instance();
		return current != nullptr ? current->time() : 0.0f;
	}

	float AssetPreview::clip_duration() const
	{
		const AnimationClipData* clip = m_ClipHandle.get();
		return clip != nullptr ? clip->Duration : 0.0f;
	}

	void AssetPreview::seek(float time)
	{
		if (AnimatorInstance* current = instance())
		{
			current->seek(time);
		}
	}

	void AssetPreview::update_notice()
	{
		m_Notice.clear();
		if (!m_Skeleton.valid())
		{
			return;
		}
		if (m_SkeletonHandle.state() == LoadState::Failed)
		{
			m_Notice = "The skeleton failed to load";
			return;
		}
		if (!m_SkeletonHandle.ready())
		{
			m_Notice = "Loading...";
			return;
		}
		if (m_Clip.valid())
		{
			if (m_ClipHandle.state() == LoadState::Failed)
			{
				m_Notice = "The clip failed to load";
				return;
			}
			const AnimationClipData* clip = m_ClipHandle.get();
			if (clip != nullptr)
			{
				const ClipCompatibilityReport report = clip_compatibility(*clip, *m_SkeletonHandle.get());
				if (report.Result == ClipCompatibility::None)
				{
					m_Notice = "The clip animates no joint of this skeleton";
				}
				else if (report.Result == ClipCompatibility::Partial)
				{
					m_Notice = "The clip animates only some joints of this skeleton (" + std::to_string(report.Missing.size()) + " of its joints are missing)";
				}
			}
		}
		if (!m_Mesh.valid())
		{
			m_Notice += std::string(m_Notice.empty() ? "" : "  ") + "No skinned mesh uses this skeleton: showing bones";
		}
		else if (m_MeshHandle.state() == LoadState::Failed)
		{
			m_Notice += std::string(m_Notice.empty() ? "" : "  ") + "The mesh failed to load";
		}
	}

	// Looks at the middle of what is shown from far enough to see all of it: the joints of the pose (they are in the space the mesh is drawn in)
	// with a margin for the skin around them; the mesh bounds only when there is no pose.
	void AssetPreview::frame()
	{
		glm::vec3 low(1e9f);
		glm::vec3 high(-1e9f);
		float margin = 1.0f;
		if (has_pose())
		{
			for (const glm::mat4& model : instance()->model())
			{
				const glm::vec3 position(model[3]);
				low = glm::min(low, position);
				high = glm::max(high, position);
			}
			margin = 1.35f;
		}
		else if (const MeshData* mesh = m_MeshHandle.get())
		{
			low = mesh->BoundsMin;
			high = mesh->BoundsMax;
		}
		if (!glm::all(glm::lessThanEqual(low, high)))
		{
			return;
		}
		const float radius = std::max(glm::length(high - low) * 0.5f * margin, 0.1f);
		m_Target = (low + high) * 0.5f;
		m_Distance = radius / std::sin(glm::radians(FOV * 0.5f)) * 1.1f;
		m_NeedFrame = false;
	}

	bool AssetPreview::project(const glm::vec3& world, ImVec2& out) const
	{
		const glm::vec4 clip = m_Camera.Projection * m_Camera.View * glm::vec4(world, 1.0f);
		if (clip.w <= 1e-4f)
		{
			return false;
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		out = ImVec2(m_ImageMin.x + (ndc.x * 0.5f + 0.5f) * m_ImageSize.x, m_ImageMin.y + (0.5f - ndc.y * 0.5f) * m_ImageSize.y);
		return true;
	}

	void AssetPreview::hide()
	{
		m_View = RenderView{};
	}

	void AssetPreview::draw(ImVec2 size)
	{
		size.x = std::max(size.x, 16.0f);
		size.y = std::max(size.y, 16.0f);
		update_notice();
		if (m_NeedFrame && (has_pose() || (!m_Skeleton.valid() && m_MeshHandle.ready())))
		{
			frame();
		}

		if (!m_View.valid())
		{
			RenderModule* render = Engine::get().get_module<RenderModule>();
			if (render == nullptr || !render->is_ready() || m_Scene == nullptr)
			{
				ImGui::Dummy(size);
				return;
			}
			ViewDesc description;
			description.Width = static_cast<u32>(size.x);
			description.Height = static_cast<u32>(size.y);
			description.Profile = ViewProfile::Preview;
			description.Mode = ViewMode::Continuous;
			m_View = render->create_view(description);
			if (!m_View.valid())
			{
				m_Notice = "Could not make a view (too many are live)";
				ImGui::Dummy(size);
				return;
			}
			m_View.set_scene(m_Scene);
		}

		m_View.resize(static_cast<u32>(size.x), static_cast<u32>(size.y));
		m_Camera = make_orbit_camera(m_Target, m_Distance, m_Yaw, m_Pitch, FOV, size.x / size.y);
		m_View.set_camera(m_Camera);

		m_ImageMin = ImGui::GetCursorScreenPos();
		m_ImageSize = size;
		ImGui::Image(static_cast<ImTextureID>(m_View.imgui_texture_id()), size);
		m_Hovered = ImGui::IsItemHovered();
		if (m_Hovered && !ImGui::IsAnyItemActive())
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				m_Yaw -= delta.x * 0.4f;
				m_Pitch = std::clamp(m_Pitch + delta.y * 0.4f, -85.0f, 85.0f);
			}
			m_Distance = std::clamp(m_Distance * (1.0f - ImGui::GetIO().MouseWheel * 0.1f), 0.2f, 200.0f);
		}
		if (m_View.failed())
		{
			ImGui::GetWindowDrawList()->AddText(ImVec2(m_ImageMin.x + 8.0f, m_ImageMin.y + 8.0f), IM_COL32(255, 100, 100, 255), "The view could not make its target");
		}

		// Overlays: the joints of the pose, projected over the image.
		const AnimatorInstance* current = instance();
		if (current == nullptr || !current->ready() || current->skeleton() == nullptr)
		{
			return;
		}
		const SkeletonData& skeleton = *current->skeleton();
		const std::vector<glm::mat4>& model = current->model();
		ImDrawList* list = ImGui::GetWindowDrawList();
		list->PushClipRect(m_ImageMin, ImVec2(m_ImageMin.x + m_ImageSize.x, m_ImageMin.y + m_ImageSize.y), true);

		std::vector<ImVec2> points(model.size());
		std::vector<bool> visible(model.size(), false);
		for (size_t i = 0; i < model.size(); i++)
		{
			visible[i] = project(glm::vec3(model[i][3]), points[i]);
		}
		if (m_Overlays.Bones)
		{
			for (size_t i = 0; i < model.size() && i < skeleton.Parents.size(); i++)
			{
				const bool selected = static_cast<int>(i) == m_SelectedJoint;
				const i32 parent = skeleton.Parents[i];
				if (parent >= 0 && visible[i] && visible[static_cast<size_t>(parent)])
				{
					list->AddLine(points[static_cast<size_t>(parent)], points[i], IM_COL32(255, 210, 90, 220), 1.5f);
				}
				if (visible[i])
				{
					list->AddCircleFilled(points[i], selected ? 5.0f : 2.5f, selected ? IM_COL32(255, 80, 60, 255) : IM_COL32(255, 235, 150, 255));
				}
			}
		}
		else if (m_SelectedJoint >= 0 && static_cast<size_t>(m_SelectedJoint) < model.size() && visible[static_cast<size_t>(m_SelectedJoint)])
		{
			list->AddCircleFilled(points[static_cast<size_t>(m_SelectedJoint)], 5.0f, IM_COL32(255, 80, 60, 255));
		}
		if (m_Overlays.JointNames)
		{
			const SkeletonData& names = m_Edit != nullptr ? *m_Edit : skeleton;
			for (size_t i = 0; i < model.size() && i < names.JointNames.size(); i++)
			{
				if (visible[i])
				{
					list->AddText(ImVec2(points[i].x + 5.0f, points[i].y - 6.0f), IM_COL32(230, 230, 230, 255), names.JointNames[i].c_str());
				}
			}
		}
		if (m_Overlays.Sockets)
		{
			const SkeletonData& source = m_Edit != nullptr ? *m_Edit : skeleton;
			for (size_t s = 0; s < source.Sockets.size(); s++)
			{
				const SkeletonSocket& socket = source.Sockets[s];
				const auto joint = std::find(source.JointNames.begin(), source.JointNames.end(), socket.Joint);
				const size_t index = static_cast<size_t>(joint - source.JointNames.begin());
				if (joint == source.JointNames.end() || index >= model.size())
				{
					continue;
				}
				const glm::mat4 world = model[index] * glm::translate(glm::mat4(1.0f), socket.Position) * glm::mat4_cast(socket.Rotation);
				ImVec2 at;
				if (project(glm::vec3(world[3]), at))
				{
					const bool selected = static_cast<int>(s) == m_SelectedSocket;
					const ImU32 color = selected ? IM_COL32(80, 220, 255, 255) : IM_COL32(80, 160, 255, 255);
					list->AddQuadFilled(ImVec2(at.x, at.y - 5.0f), ImVec2(at.x + 5.0f, at.y), ImVec2(at.x, at.y + 5.0f), ImVec2(at.x - 5.0f, at.y), color);
					list->AddText(ImVec2(at.x + 7.0f, at.y - 6.0f), color, socket.Name.c_str());
				}
			}
		}
		list->PopClipRect();
	}
}
