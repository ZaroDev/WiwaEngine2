#pragma once

#include <optional>
#include <string>

#include <imgui.h>

#include <animation/animation_module.h>
#include <assets/asset_manager.h>
#include <assets/handle.h>
#include <renderer/view/render_view.h>
#include <scene/scene.h>

namespace wiwa
{
	// The entity a preview scene shows: one that is the Animator (skeleton, and the clip that plays from the start, if any) and, with a mesh, the skinned
	// mesh at once (the extractor takes the Animator of the entity itself). The skeleton has to be valid.
	entt::entity make_preview_entity(Scene& scene, const AssetRef& skeleton, const AssetRef& mesh, const AssetRef& clip, const AssetRef& graph = {});

	// The preview of the Asset Editor: a skeleton posed in its own preview scene (with the skinned mesh that is previewed with it, or bones only),
	// drawn by an offscreen RenderView, with the joints, joint names and sockets drawn over it by ImGui (the debug renderer is the main view's, so
	// the overlays are projected from the pose instead). The animation module plays the preview scene (register_preview_scene). The view exists
	// only while draw() is called; hide() lets it go. Orbit with the left mouse button, zoom with the wheel. Main thread, between the ImGui frame's
	// begin and end.
	class AssetPreview
	{
	public:
		struct Overlays
		{
			bool Mesh = true;
			bool Bones = true;
			bool JointNames = false;
			bool Sockets = true;
		};

		AssetPreview() = default;
		~AssetPreview() { release(); }
		// Lets go of the view and the scene (before the modules they live in shut down).
		void release();
		AssetPreview(const AssetPreview&) = delete;
		AssetPreview& operator=(const AssetPreview&) = delete;

		// What is shown: the skeleton, the mesh that skins to it (an invalid reference: bones only) and the clip that plays (invalid: the bind pose).
		// graph: an animation graph that plays instead of the clip.
		void show(const AssetRef& skeleton, const AssetRef& mesh, const AssetRef& clip = {}, const AssetRef& graph = {});
		void set_clip(const AssetRef& clip);
		// The data the overlays name: the skeleton being edited (its sockets and joint names), whose joints keep the order of the saved one.
		void set_edit_skeleton(const SkeletonData* skeleton) { m_Edit = skeleton; }
		void set_selection(int joint, int socket) { m_SelectedJoint = joint; m_SelectedSocket = socket; }
		Overlays& overlays() { return m_Overlays; }

		void set_playing(bool playing);
		bool playing() const { return m_Playing; }
		float clip_time() const;
		float clip_duration() const;
		void seek(float time);

		// Draws the image in size and the overlays, and takes the mouse. size is clamped to at least 16 pixels.
		void draw(ImVec2 size);
		// Lets the view go (the window is hidden); the scene stays.
		void hide();

		// What the last frame knows, for the gizmo and the tool pane: the camera the image was drawn with, the rectangle on screen, a joint's
		// model-space matrix (identity when there is no pose yet) and the mesh and skeleton being shown.
		const RenderCamera& camera() const { return m_Camera; }
		ImVec2 image_min() const { return m_ImageMin; }
		ImVec2 image_size() const { return m_ImageSize; }
		glm::mat4 joint_matrix(size_t joint) const;
		// The instance that plays in the preview scene (null while there is none): its graph parameters and states.
		AnimatorInstance* animator() const { return instance(); }
		bool has_pose() const;
		// The index of the joint with this name in the skeleton being posed, -1 when there is none.
		int joint_index(const std::string& name) const;
		// A one-line note under the image: loading, a clip that fits only some joints, bones only. Empty when all is well.
		const std::string& notice() const { return m_Notice; }
		bool hovered() const { return m_Hovered; }

	private:
		void rebuild();
		void update_notice();
		void frame();
		bool project(const glm::vec3& world, ImVec2& out) const;
		AnimatorInstance* instance() const;

		Ref<Scene> m_Scene{};
		entt::entity m_Entity = entt::null;
		AssetRef m_Skeleton{};
		AssetRef m_Mesh{};
		AssetRef m_Clip{};
		AssetRef m_Graph{};
		Handle<SkeletonData> m_SkeletonHandle{};
		Handle<MeshData> m_MeshHandle{};
		Handle<AnimationClipData> m_ClipHandle{};
		const SkeletonData* m_Edit = nullptr;
		RenderView m_View{};
		RenderCamera m_Camera{};
		Overlays m_Overlays{};
		std::string m_Notice{};

		ImVec2 m_ImageMin{};
		ImVec2 m_ImageSize{};
		bool m_Hovered = false;
		bool m_Playing = true;
		bool m_NeedFrame = true;
		int m_SelectedJoint = -1;
		int m_SelectedSocket = -1;

		glm::vec3 m_Target{ 0.0f, 1.0f, 0.0f };
		float m_Distance = 4.0f;
		float m_Yaw = 20.0f;
		float m_Pitch = 12.0f;
	};
}
