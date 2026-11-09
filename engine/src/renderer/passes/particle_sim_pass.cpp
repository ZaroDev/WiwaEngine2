#include <pch.h>
#include "particle_sim_pass.h"

#include <algorithm>
#include <cmath>

#include <scene/particle_runtime.h> // particle_value_max

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_PARTICLE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 GROUP_SIZE = 64; // [numthreads] of particle_spawn.hlsl and particle_sim.hlsl
		constexpr float MAX_STEP = 0.1f;
		constexpr float ALIVE_MARGIN = 0.1f; // seconds an emitter is still drawn after its last particle should be dead

		glm::vec4 encode(const ParticleValue& value)
		{
			return glm::vec4(static_cast<float>(value.Kind), value.A, value.B, 0.0f);
		}

		// The curves of the modules the GPU reads, in the order of EmitterGpu::Values.
		std::array<const ParticleValue*, PARTICLE_TABLE_CURVES> gpu_values(const ParticleEmitterData& e)
		{
			return { &e.Lifetime, &e.StartSpeed, &e.StartSize, &e.StartRotation, &e.SizeOverLife, &e.RotationSpeed, &e.AlphaOverLife, &e.FrameOverLife };
		}

		void write_tables(nvrhi::ICommandList* commandList, nvrhi::IBuffer* tables, u32 id, const ParticleEmitterData& emitter)
		{
			std::array<glm::vec4, PARTICLE_TABLE_ROWS> rows{};
			const auto values = gpu_values(emitter);
			for (u32 curve = 0; curve < PARTICLE_TABLE_CURVES; curve++)
			{
				if (values[curve]->Kind != ParticleValue::Mode::Curve)
				{
					continue;
				}
				for (u32 s = 0; s < PARTICLE_CURVE_SAMPLES; s++)
				{
					rows[curve * (PARTICLE_CURVE_SAMPLES / 4) + s / 4][s % 4] = values[curve]->Samples[s];
				}
			}
			for (u32 s = 0; s < PARTICLE_CURVE_SAMPLES; s++)
			{
				rows[PARTICLE_CURVE_SAMPLES * 2 + s] = emitter.ColorSamples[s];
			}
			commandList->writeBuffer(tables, rows.data(), sizeof(rows), static_cast<u64>(id) * sizeof(rows));
		}

		EmitterGpu make_record(const ParticleEmitterData& e, const RenderParticleSystem& system, const ParticleAllocation& ring, u32 head, u32 spawn, u32 spawnStart, u32 emitterIndex)
		{
			EmitterGpu record;
			for (int column = 0; column < 4; column++)
			{
				record.World[column] = system.World[column];
			}
			record.Ring = glm::uvec4(ring.Base, ring.Capacity, head, spawn);
			record.Spawn = glm::uvec4(spawnStart, (system.Seed ^ (emitterIndex * 2654435761u)) | 1u, e.WorldSpace ? 1u : 0u, static_cast<u32>(e.Shape));
			record.Shape = glm::vec4(e.ShapeSize, glm::radians(e.ConeAngle));
			record.Force = glm::vec4(e.Gravity, e.Drag);
			record.Noise = glm::vec4(e.NoiseStrength, e.NoiseFrequency, system.SizeScale, e.Intensity);
			record.StartColor = e.StartColor * system.ColorTint;
			const auto values = gpu_values(e);
			for (u32 i = 0; i < PARTICLE_TABLE_CURVES; i++)
			{
				record.Values[i] = encode(*values[i]);
			}
			record.Render = glm::vec4(e.SoftDistance, static_cast<float>(e.SheetColumns), static_cast<float>(e.SheetRows), static_cast<float>(e.Align));
			record.Render2 = glm::uvec4(static_cast<u32>(e.Blend), 0u, 0u, 0u);
			return record;
		}
	}

	bool ParticleSimPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		m_Pool = description.Particles;
		if (m_Shaders == nullptr || m_Pool == nullptr)
		{
			WI_PARTICLE_LOG(Error, "The particle simulation needs the shader library and the pool");
			return false;
		}

		m_Shaders->request(engine_assets::PARTICLE_SPAWN_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::PARTICLE_SIM_SHADER, ShaderStage::Compute);

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(SimConstants);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = 4;
		constants.debugName = "Particle constants";
		m_Constants = m_Device->createBuffer(constants);

		// Slots match particle_spawn.hlsl and particle_sim.hlsl: b0 constants, SRV 1 the emitters, SRV 2 the tables, UAV 3 the particles.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Compute;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3));
		m_Layout = m_Device->createBindingLayout(layout);
		return m_Constants && m_Layout;
	}

	bool ParticleSimPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::PARTICLE_SPAWN_SHADER, ShaderStage::Compute) != nullptr && m_Shaders->get(engine_assets::PARTICLE_SIM_SHADER, ShaderStage::Compute) != nullptr;
	}

	void ParticleSimPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::PARTICLE_SPAWN_SHADER, ShaderStage::Compute) || m_Shaders->has_failed(engine_assets::PARTICLE_SIM_SHADER, ShaderStage::Compute))
		{
			m_Failed = true;
		}
	}

	void ParticleSimPass::release(SystemState& state)
	{
		for (ParticleAllocation& ring : state.Rings)
		{
			if (ring.Capacity != 0)
			{
				m_Pool->release(ring);
				ring = ParticleAllocation{};
			}
		}
	}

	void ParticleSimPass::reset_state()
	{
		m_Systems.clear(); // the pool they point into is gone
		m_BindingSet = nullptr;
	}

	void ParticleSimPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		m_Pool->Draws.clear();
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || frame.Scene->scene_mutable() == nullptr)
		{
			return;
		}

		RenderScene& scene = *frame.Scene->scene_mutable();
		GpuResourceCache& cache = *frame.Scene->cache();
		if (m_PoolGeneration != m_Pool->generation())
		{
			reset_state();
			m_PoolGeneration = m_Pool->generation();
		}
		if (scene.particle_system_count() == 0 && m_Systems.empty())
		{
			return;
		}
		if (!m_Pool->ensure(m_Device))
		{
			return;
		}
		if (m_PoolGeneration != m_Pool->generation())
		{
			reset_state(); // ensure() made the buffers: nothing was allocated before
			m_PoolGeneration = m_Pool->generation();
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_SpawnPipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			nvrhi::ComputePipelineDesc spawn;
			spawn.CS = m_Shaders->get(engine_assets::PARTICLE_SPAWN_SHADER, ShaderStage::Compute);
			spawn.addBindingLayout(m_Layout);
			m_SpawnPipeline = m_Device->createComputePipeline(spawn);
			nvrhi::ComputePipelineDesc sim;
			sim.CS = m_Shaders->get(engine_assets::PARTICLE_SIM_SHADER, ShaderStage::Compute);
			sim.addBindingLayout(m_Layout);
			m_SimPipeline = m_Device->createComputePipeline(sim);
			if (m_SpawnPipeline == nullptr || m_SimPipeline == nullptr)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle pipelines");
				m_Failed = true;
				return;
			}
		}
		if (m_BindingSet == nullptr)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_Pool->emitters()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Pool->tables()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_Pool->particles()));
			m_BindingSet = m_Device->createBindingSet(set, m_Layout);
			if (m_BindingSet == nullptr)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle bindings");
				m_Failed = true;
				return;
			}
		}

		const float step = std::clamp(frame.DeltaTime, 0.0f, MAX_STEP);
		m_Pass++;

		struct Work
		{
			const ParticleEmitterData* Emitter = nullptr;
			const RenderParticleSystem* System = nullptr;
			ParticleAllocation Ring{};
			u32 Head = 0;
			u32 Spawn = 0;
			u32 Index = 0;
			bool Draw = false;
		};
		std::vector<Work> work;

		const std::vector<RenderParticleSystem>& systems = scene.particle_systems();
		for (size_t slot = 0; slot < systems.size(); slot++)
		{
			const RenderParticleSystem& system = systems[slot];
			if (!system.Alive)
			{
				continue;
			}
			const ParticleEffectData* effect = cache.get_particle_effect(system.Effect);
			if (effect == nullptr)
			{
				continue; // still loading, or it failed: nothing happens, and the spawns wait
			}

			SystemState& state = m_Systems[system.Guid];
			const u32 revision = cache.effect_revision(system.Effect);
			if (state.Effect != system.Effect || state.Revision != revision)
			{
				release(state);
				state = SystemState{};
			}
			state.Effect = system.Effect;
			state.Revision = revision;
			state.Seen = m_Pass;

			const std::array<u32, PARTICLE_MAX_EMITTERS> spawns = scene.take_pending_spawns(slot);
			const size_t emitters = std::min<size_t>(effect->Emitters.size(), PARTICLE_MAX_EMITTERS);
			for (u32 i = 0; i < emitters; i++)
			{
				const ParticleEmitterData& emitter = effect->Emitters[i];
				if (emitter.Renderer != ParticleRenderer::Sprite)
				{
					if (!m_WarnedRenderer)
					{
						m_WarnedRenderer = true;
						WI_PARTICLE_LOG(Warning, "Ribbon and mesh particle emitters are not drawn yet");
					}
					continue;
				}

				if (state.Rings[i].Capacity == 0 && !state.Failed[i])
				{
					if (const std::optional<ParticleAllocation> ring = m_Pool->allocate(emitter.MaxParticles))
					{
						state.Rings[i] = *ring;
						state.Heads[i] = 0;
						state.AliveFor[i] = 0.0f;
						// the slots hold what an earlier ring left there: all dead, and the curves of this emitter
						const std::vector<u8> zeros(static_cast<size_t>(ring->Capacity) * sizeof(ParticleGpu), 0);
						commandList->writeBuffer(m_Pool->particles(), zeros.data(), zeros.size(), static_cast<u64>(ring->Base) * sizeof(ParticleGpu));
						write_tables(commandList, m_Pool->tables(), ring->Id, emitter);
					}
					else
					{
						state.Failed[i] = true;
						if (!m_WarnedFull)
						{
							m_WarnedFull = true;
							WI_PARTICLE_LOG(Warning, "The particle pool ({0} particles) is full: an emitter is not played", m_Pool->capacity());
						}
					}
				}
				if (state.Rings[i].Capacity == 0)
				{
					continue;
				}

				Work item;
				item.Emitter = &emitter;
				item.System = &system;
				item.Ring = state.Rings[i];
				item.Head = state.Heads[i];
				item.Spawn = std::min(spawns[i], item.Ring.Capacity);
				item.Index = i;
				state.Heads[i] = (state.Heads[i] + item.Spawn) % item.Ring.Capacity;
				state.AliveFor[i] = item.Spawn > 0 ? particle_value_max(emitter.Lifetime) + ALIVE_MARGIN : state.AliveFor[i] - step;
				item.Draw = state.AliveFor[i] > 0.0f;
				work.push_back(item);
			}
		}

		for (auto it = m_Systems.begin(); it != m_Systems.end();)
		{
			if (it->second.Seen != m_Pass)
			{
				release(it->second);
				it = m_Systems.erase(it);
			}
			else
			{
				++it;
			}
		}

		const u32 idEnd = m_Pool->id_end();
		if (idEnd == 0)
		{
			return;
		}

		// the emitter records, and the order the spawn threads belong to them
		std::vector<EmitterGpu> records(idEnd);
		u32 totalSpawn = 0;
		const glm::vec3 camera = glm::vec3(frame.Scene->constants().CameraPosition);
		struct Sorted
		{
			ParticleDraw Draw;
			i32 Bias;
			float Distance;
		};
		std::vector<Sorted> draws;
		for (const Work& item : work)
		{
			records[item.Ring.Id] = make_record(*item.Emitter, *item.System, item.Ring, item.Head, item.Spawn, totalSpawn, item.Index);
			totalSpawn += item.Spawn;
			if (item.Draw)
			{
				const glm::vec3 offset = glm::vec3(item.System->World[3]) - camera;
				draws.push_back(Sorted{ ParticleDraw{ item.Ring.Id, item.Ring.Base, item.Ring.Capacity, item.Emitter->Material, item.Emitter->Blend }, item.System->SortBias, glm::dot(offset, offset) });
			}
		}
		std::sort(draws.begin(), draws.end(), [](const Sorted& a, const Sorted& b)
			{
				return a.Bias != b.Bias ? a.Bias < b.Bias : a.Distance > b.Distance; // the lower bias first, the farther system first
			});
		for (const Sorted& sorted : draws)
		{
			m_Pool->Draws.push_back(sorted.Draw);
		}

		commandList->writeBuffer(m_Pool->emitters(), records.data(), records.size() * sizeof(EmitterGpu));

		SimConstants constants;
		constants.EmitterCount = idEnd;
		constants.TotalSpawn = totalSpawn;
		constants.SlotEnd = m_Pool->slot_end();
		constants.FrameIndex = static_cast<u32>(frame.FrameIndex);
		constants.DeltaTime = step;
		constants.Time = frame.Time;
		commandList->writeBuffer(m_Constants, &constants, sizeof(constants));

		if (totalSpawn > 0)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_SpawnPipeline;
			state.addBindingSet(m_BindingSet);
			commandList->setComputeState(state);
			commandList->dispatch((totalSpawn + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
		}

		if (constants.SlotEnd > 0)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_SimPipeline;
			state.addBindingSet(m_BindingSet);
			commandList->setComputeState(state);
			commandList->dispatch((constants.SlotEnd + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
		}
	}
}
