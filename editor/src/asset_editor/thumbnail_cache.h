#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <animation/animator_instance.h>
#include <assets/asset_manager.h>
#include <assets/handle.h>
#include <renderer/view/render_view.h>
#include <scene/preview_scene.h>
#include <scene/scene.h>

#include "editor_state.h"
#include "skeleton_index.h"

namespace wiwa
{
	class AssetDatabase;

	// The thumbnails of the animation assets (.wskel, .wanim, .wmesh) in the Asset Browser. A thumbnail is made the first time a cell asks for it and
	// kept on disk (Library/thumbnails/<guid>-<key>.png, the key a hash of the files it was made from and the renderer's version), so a later run
	// only reads PNGs: no GPU work. One shared small view renders one thumbnail at a time (a mesh posed, a clip on its first frame; bone lines drawn
	// on the CPU where no mesh uses the skeleton). Main thread.
	class ThumbnailCache
	{
	public:
		static constexpr u32 SIZE = 128;                 // pixels
		static constexpr u32 VERSION = 4;             // changes whenever what a thumbnail shows changes: every cached PNG is made again
		static constexpr size_t MAX_TEXTURES = 512;      // on the GPU at once
		static constexpr u64 MAX_DISK_BYTES = 512ull * 1024 * 1024;

		ThumbnailCache(AssetDatabase& database, SkeletonIndex& index, EditorState& state);
		~ThumbnailCache();
		ThumbnailCache(const ThumbnailCache&) = delete;
		ThumbnailCache& operator=(const ThumbnailCache&) = delete;

		// Whether the file gets a thumbnail: the animation assets, materials, prefabs, environments (.hdr), color grading tables (.cube), fonts.
		static bool handles(const std::filesystem::path& source);
		// A model file (.glb, .gltf, .fbx): shown as the prefab it imports to.
		static bool is_model(const std::filesystem::path& source);
		// The animation assets (.wskel, .wanim, .wmesh) alone: the ones the Asset Editor opens.
		static bool is_animation_asset(const std::filesystem::path& source);

		// The texture to draw for the asset's source file, or 0 while there is none (a placeholder shows). Call it every frame the cell is visible: a
		// cell that is not asked for any more leaves the queue.
		u32 request(const std::filesystem::path& source);

		// Advances the work: loads cached PNGs, runs the render of the current thumbnail. Once per frame.
		void update();

	private:
		// Skeleton, Clip, Mesh: posed by the animation module. Material (a sphere) and Prefab: drawn by the view. Effect (.wfx): played for a moment in the view,
		// CPU from the cooked data, no view.
		enum class Kind { Skeleton, Clip, Mesh, Material, Prefab, Environment, Lut, Font, Effect };
		enum class State { New, Queued, Working, Ready, Failed };

		struct Entry
		{
			std::filesystem::path Source{};
			Kind Type = Kind::Skeleton;
			State Status = State::New;
			AssetRef Ref{};
			u64 Key = 0;                       // the hash of the files the thumbnail is made from
			u32 Texture = 0;
			int LastRequested = 0;
			int NextCheck = 0;
			std::filesystem::file_time_type FileTime{};
			AssetRef Skeleton{};               // what the thumbnail poses: filled when the key is made
			AssetRef Mesh{};
			AssetRef Clip{};
		};

		// The render of one thumbnail, step by step.
		struct Job
		{
			std::filesystem::path Source{};
			u64 Key = 0;
			AssetRef Skeleton{};
			AssetRef Mesh{};
			AssetRef Clip{};
			Handle<SkeletonData> SkeletonHandle{};
			Handle<MeshData> MeshHandle{};
			Handle<AnimationClipData> ClipHandle{};
			AssetRef Asset{};                  // the asset itself (the kinds without a skeleton)
			Kind Type = Kind::Skeleton;
			float Zoom = 1.0f;                 // the camera's distance is scaled by it: grows while the picture touches its border
			int Zooms = 0;
			Handle<PrefabData> PrefabHandle{};
			Handle<EnvironmentData> EnvironmentHandle{};
			Handle<LutData> LutHandle{};
			Handle<FontData> FontHandle{};
			std::unique_ptr<PreviewModel> Model{}; // Prefab
			entt::entity Entity = entt::null;
			int Frames = 0;
			std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now(); // a job also ends by the clock: frames can be a millisecond each
			int Settled = 0;
			int Attempts = 0;
			bool Posed = false;
			bool ReadBack = false;
			float EffectTime = 0.0f;           // Effect: the second of its play the picture shows
			float EffectRadius = 1.0f;         // and the size the camera frames
			bool EffectStopped = false;       // emission is over: only what is in the air is drawn
		};

		Entry& entry_for(const std::filesystem::path& source);
		bool make_key(Entry& entry);
		void begin(Entry& entry);
		void step_job();
		void step_static_job();   // Material and Prefab
		void step_cpu_job();      // Environment, Lut, Font
		void step_effect_job();   // Effect
		void render_job(const RenderCamera& camera, int settleFrames);
		void end_job();
		bool load_from_disk(Entry& entry);
		void finish(const std::filesystem::path& source, u64 key, const ImageData& image);
		void fail(const std::filesystem::path& source, const char* why);
		void release_texture(Entry& entry);
		std::filesystem::path file_for(const Entry& entry) const;
		void trim_disk() const;
		ImageData draw_bones(const AnimatorInstance& instance, const RenderCamera& camera) const;

		AssetDatabase& m_Database;
		SkeletonIndex& m_Index;
		EditorState& m_State;
		std::filesystem::path m_Folder{};
		std::unordered_map<std::string, Entry> m_Entries{};
		std::vector<std::string> m_Queue{};          // keys of m_Entries waiting for a render, oldest first
		std::optional<Job> m_Job{};
		Ref<Scene> m_Scene{};
		RenderView m_View{};
		ViewMode m_ViewMode = ViewMode::OneShot; // an effect is played in a continuous view (its simulation needs every frame), the rest in a one-shot one
		int m_Frame = 0;
		int m_KeysThisFrame = 0;
	};
}
