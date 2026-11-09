#pragma once

#include <array>
#include <unordered_map>

#include "../particles/particle_pool.h"
#include "../render_pass.h"
#include "../scene/render_scene.h"
#include "../shader_library.h"

namespace wiwa
{
	// The particle simulation (vfx.md), once per frame in the main view: gives every sprite emitter of every system a ring in the pool, writes the emitter
	// records, spawns what the main thread asked for (RenderScene::take_pending_spawns) and ages and moves every live particle, in two compute
	// dispatches. It also lists what the render pass draws (ParticlePool::Draws). Does nothing while there is no particle system.
	class ParticleSimPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Particle simulation"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

	private:
		// Mirrors SimConstants of particle_spawn.hlsl and particle_sim.hlsl.
		struct SimConstants
		{
			u32 EmitterCount = 0;
			u32 TotalSpawn = 0;
			u32 SlotEnd = 0;
			u32 FrameIndex = 0;
			float DeltaTime = 0.0f;
			float Time = 0.0f;
			float Padding[2]{};
		};

		// What the pass keeps of one system: its rings (by the emitter's place in the effect), where each ring's head is, how long each emitter still has
		// particles alive.
		struct SystemState
		{
			AssetRef Effect{};
			u32 Revision = 0;
			u64 Seen = 0;
			std::array<ParticleAllocation, PARTICLE_MAX_EMITTERS> Rings{};
			std::array<u32, PARTICLE_MAX_EMITTERS> Heads{};
			std::array<float, PARTICLE_MAX_EMITTERS> AliveFor{}; // seconds
			std::array<bool, PARTICLE_MAX_EMITTERS> Failed{};
		};

		void release(SystemState& state);
		void reset_state();

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		ParticlePool* m_Pool = nullptr;
		bool m_Failed = false;

		nvrhi::BufferHandle m_Constants{};
		nvrhi::BindingLayoutHandle m_Layout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		nvrhi::ComputePipelineHandle m_SpawnPipeline{};
		nvrhi::ComputePipelineHandle m_SimPipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_PoolGeneration = ~0u;

		std::unordered_map<EntityGuid, SystemState> m_Systems{};
		u64 m_Pass = 0;
		bool m_WarnedRenderer = false;
		bool m_WarnedFull = false;
	};
}
