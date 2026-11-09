#pragma once

#include <array>
#include <unordered_map>

#include "../particles/particle_pool.h"
#include "../render_pass.h"
#include "../scene/render_scene.h"
#include "../shader_library.h"
#include "../view/per_view.h"

namespace wiwa
{
	// The particle simulation (vfx.md), once per frame in the main view, after the depth prepass: gives every sprite emitter of every system a ring in the
	// pool, writes the emitter records, spawns what the main thread asked for (RenderScene::take_pending_spawns), ages and moves every live particle
	// (colliding with the depth where an emitter asks for it), and sorts the particles of the emitters that blend back to front. It also lists what the
	// render pass draws (ParticlePool::Draws). Does nothing while there is no particle system.
	class ParticleSimPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Particle simulation"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); m_Views.erase(targetUid); m_Pool->forget_view(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.Particles; }

		// How far behind a surface (in view depth, meters) a particle still counts as touching it. A particle further behind passes through.
		static constexpr float COLLISION_THICKNESS = 1.0f;

	private:
		// Mirrors SimConstants of particle_sim_common.hlsli.
		struct SimConstants
		{
			u32 EmitterCount = 0;
			u32 TotalSpawn = 0;
			u32 SlotEnd = 0;
			u32 FrameIndex = 0;
			float DeltaTime = 0.0f;
			float Time = 0.0f;
			float Thickness = COLLISION_THICKNESS;
			u32 Collide = 0;
			glm::mat4 ViewProjection{ 1.0f };
			glm::mat4 InverseViewProjection{ 1.0f };
			glm::vec4 Viewport{ 1.0f };
		};

		// What the pass keeps of one system: its rings (by the emitter's place in the effect), where each ring's head is, how long each emitter still has
		// particles alive.
		struct SystemState
		{
			AssetRef Effect{};
			u32 Revision = 0;
			u32 ClearCount = 0;
			u64 Seen = 0;
			std::array<ParticleAllocation, PARTICLE_MAX_EMITTERS> Rings{};
			std::array<u32, PARTICLE_MAX_EMITTERS> Heads{};
			std::array<float, PARTICLE_MAX_EMITTERS> AliveFor{}; // seconds
			std::array<bool, PARTICLE_MAX_EMITTERS> Failed{};
		};

		// What belongs to one view (PerView): the set that reads its depth and normals. Made again with the viewport's targets and the pool's buffers.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSet{};
			u32 PoolGeneration = ~0u;
		};

		void release(SystemState& state, ParticlePool& pool, nvrhi::ICommandList* commandList);
		// What the pass keeps of one view: the pool it uses (a view that is not the main one has its own), the systems with rings in it, and the sort bindings.
		struct ViewSim
		{
			const ParticlePool* Pool = nullptr;
			u32 PoolGeneration = ~0u;
			std::unordered_map<EntityGuid, SystemState> Systems{};
			nvrhi::BindingSetHandle SortFillSet{};
			nvrhi::BindingSetHandle SortStepSet{};
			u32 SortSetPoolGeneration = ~0u;
			u32 SortSetSortGeneration = ~0u;
		};
		void sort_emitter(nvrhi::ICommandList* commandList, ParticlePool& pool, ViewSim& sim, const ParticleDraw& draw);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		ParticlePool* m_Pool = nullptr;
		bool m_Failed = false;

		nvrhi::BufferHandle m_Constants{};
		nvrhi::BindingLayoutHandle m_Layout{};     // b0, the emitters, the tables, the particles, the depth and the normals
		nvrhi::BindingLayoutHandle m_SortFillLayout{};
		nvrhi::BindingLayoutHandle m_SortStepLayout{};
		PerView<ViewCache> m_PerView{};
		nvrhi::ComputePipelineHandle m_SpawnPipeline{};
		nvrhi::ComputePipelineHandle m_SimPipeline{};
		nvrhi::ComputePipelineHandle m_SortFillPipeline{};
		nvrhi::ComputePipelineHandle m_SortStepPipeline{};
		u32 m_BuiltShaderGeneration = ~0u;

		std::unordered_map<u32, ViewSim> m_Views{};
		u64 m_Pass = 0;
		bool m_WarnedRenderer = false;
		bool m_WarnedFull = false;
	};
}
