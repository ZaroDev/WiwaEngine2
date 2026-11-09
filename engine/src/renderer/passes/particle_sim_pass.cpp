#include <pch.h>
#include "particle_sim_pass.h"

#include <algorithm>
#include <cmath>

#include <scene/particle_runtime.h> // particle_value_max

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"
#include "../view/view_state.h"

#define WI_PARTICLE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 GROUP_SIZE = 64; // [numthreads] of the particle compute shaders
		constexpr float MAX_STEP = 0.1f;
		constexpr float ALIVE_MARGIN = 0.1f; // seconds an emitter is still drawn after its last particle should be dead

		// The push constants of the sort shaders (SortConstantsData of particle_sort_fill.hlsl, SortStepData of particle_sort_step.hlsl).
		struct SortPush
		{
			u32 A, B, C, D;
		};

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

		// Alpha blended emitters that ask for it are sorted; additive and multiply blending do not depend on the order.
		bool sorts(const ParticleEmitterData& e)
		{
			return e.Sorted && e.Blend == ParticleBlend::Alpha;
		}

		u32 padded(u32 count)
		{
			u32 result = 2;
			while (result < count)
			{
				result <<= 1;
			}
			return result;
		}

		EmitterGpu make_record(const ParticleEmitterData& e, const RenderParticleSystem& system, const ParticleAllocation& ring, u32 head, u32 spawn, u32 spawnStart, u32 emitterIndex, u32 sortStart, bool collisions)
		{
			EmitterGpu record;
			for (int column = 0; column < 4; column++)
			{
				record.World[column] = system.World[column];
			}
			const u32 flags = (e.WorldSpace ? 1u : 0u) | (collisions && e.Collision == ParticleCollision::Depth ? 2u : 0u) | (static_cast<u32>(e.Response) << 2);
			record.Ring = glm::uvec4(ring.Base, ring.Capacity, head, spawn);
			record.Spawn = glm::uvec4(spawnStart, (system.Seed ^ (emitterIndex * 2654435761u)) | 1u, flags, static_cast<u32>(e.Shape));
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
			record.Render2 = glm::uvec4(static_cast<u32>(e.Blend), sortStart, 0u, glm::floatBitsToUint(e.Restitution));
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
		m_Shaders->request(engine_assets::PARTICLE_SORT_FILL_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::PARTICLE_SORT_STEP_SHADER, ShaderStage::Compute);

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(SimConstants);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = 64; // one write per view per frame (up to MAX_VIEWS), for the frames the render thread is behind
		constants.debugName = "Particle constants";
		m_Constants = m_Device->createBuffer(constants);

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match particle_spawn.hlsl and particle_sim.hlsl: b0 constants, SRV 1 the emitters, SRV 2 the tables, UAV 3 the particles, SRV 4 the depth,
		// SRV 5 the normals.
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Compute;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(4));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(5));
		m_Layout = m_Device->createBindingLayout(layout);

		// particle_sort_fill.hlsl: b0 constants, push constants b1, SRV 1 the emitters, SRV 2 the particles, UAV 3 the sort entries.
		nvrhi::BindingLayoutDesc fill;
		fill.visibility = nvrhi::ShaderType::Compute;
		fill.bindingOffsets = offsets;
		fill.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		fill.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(SortPush)));
		fill.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		fill.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
		fill.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3));
		m_SortFillLayout = m_Device->createBindingLayout(fill);

		// particle_sort_step.hlsl: push constants b1, UAV 3 the sort entries.
		nvrhi::BindingLayoutDesc step;
		step.visibility = nvrhi::ShaderType::Compute;
		step.bindingOffsets = offsets;
		step.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(SortPush)));
		step.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3));
		m_SortStepLayout = m_Device->createBindingLayout(step);
		return m_Constants && m_Layout && m_SortFillLayout && m_SortStepLayout;
	}

	bool ParticleSimPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::PARTICLE_SPAWN_SHADER, ShaderStage::Compute) != nullptr && m_Shaders->get(engine_assets::PARTICLE_SIM_SHADER, ShaderStage::Compute) != nullptr
			&& m_Shaders->get(engine_assets::PARTICLE_SORT_FILL_SHADER, ShaderStage::Compute) != nullptr && m_Shaders->get(engine_assets::PARTICLE_SORT_STEP_SHADER, ShaderStage::Compute) != nullptr;
	}

	void ParticleSimPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		for (const char* path : { engine_assets::PARTICLE_SPAWN_SHADER, engine_assets::PARTICLE_SIM_SHADER, engine_assets::PARTICLE_SORT_FILL_SHADER, engine_assets::PARTICLE_SORT_STEP_SHADER })
		{
			if (m_Shaders->has_failed(path, ShaderStage::Compute))
			{
				m_Failed = true;
			}
		}
	}

	void ParticleSimPass::release(SystemState& state, ParticlePool& pool, nvrhi::ICommandList* commandList)
	{
		for (ParticleAllocation& ring : state.Rings)
		{
			if (ring.Capacity != 0)
			{
				// its particles die with it: the slots stay in the range the simulation covers while other rings are there
				const std::vector<u8> zeros(static_cast<size_t>(ring.Capacity) * sizeof(ParticleGpu), 0);
				commandList->writeBuffer(pool.particles(), zeros.data(), zeros.size(), static_cast<u64>(ring.Base) * sizeof(ParticleGpu));
				pool.release(ring);
				ring = ParticleAllocation{};
			}
		}
	}

	// Sorts the entries of one emitter: fills them with the depth of every slot, then runs the bitonic network (log2(n) (log2(n) + 1) / 2 steps).
	void ParticleSimPass::sort_emitter(nvrhi::ICommandList* commandList, ParticlePool& pool, ViewSim& sim, const ParticleDraw& draw)
	{
		const u32 offset = draw.SortStart - 1;
		const u32 count = draw.SortCount;

		nvrhi::ComputeState fill;
		fill.pipeline = m_SortFillPipeline;
		fill.addBindingSet(sim.SortFillSet);
		commandList->setComputeState(fill);
		const SortPush fillPush{ offset, draw.Capacity, count, draw.Id };
		commandList->setPushConstants(&fillPush, sizeof(fillPush));
		commandList->dispatch((count + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
			commandList->setBufferState(pool.sort_buffer(), nvrhi::ResourceStates::UnorderedAccess); // the next step reads what this one wrote
			commandList->commitBarriers();

		for (u32 k = 2; k <= count; k <<= 1)
		{
			for (u32 j = k >> 1; j > 0; j >>= 1)
			{
				nvrhi::ComputeState step;
				step.pipeline = m_SortStepPipeline;
				step.addBindingSet(sim.SortStepSet);
				commandList->setComputeState(step);
				const SortPush stepPush{ offset, k, j, count };
				commandList->setPushConstants(&stepPush, sizeof(stepPush));
				commandList->dispatch((count + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
			commandList->setBufferState(pool.sort_buffer(), nvrhi::ResourceStates::UnorderedAccess); // the next step reads what this one wrote
			commandList->commitBarriers();
			}
		}
	}

	void ParticleSimPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		const u32 viewUid = frame.View->Target.uid();
		ParticlePool& pool = m_Pool->for_view(viewUid, frame.IsMainView);
		pool.Draws.clear();
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || frame.Scene->scene_mutable() == nullptr)
		{
			return;
		}

		RenderScene& scene = *frame.Scene->scene_mutable();
		const RenderQuality& quality = frame.Scene->quality();
		GpuResourceCache& cache = *frame.Scene->cache();
		ViewSim& sim = m_Views[viewUid];
		// a pool that is another object or was dropped: the systems of this view start over (their rings are gone with it)
		const auto adopt = [&]()
		{
			if (sim.Pool != &pool || sim.PoolGeneration != pool.generation())
			{
				sim.Pool = &pool;
				m_PerView.forget(viewUid);
				sim.PoolGeneration = pool.generation();
				sim.Systems.clear();
				sim.SortFillSet = nullptr;
				sim.SortStepSet = nullptr;
				sim.SortSetPoolGeneration = ~0u;
				sim.SortSetSortGeneration = ~0u;
			}
		};
		adopt();
		if (scene.particle_system_count() == 0 && sim.Systems.empty())
		{
			return;
		}
		if (!pool.ensure(m_Device))
		{
			return;
		}
		adopt(); // ensure() made the buffers: nothing was allocated before
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_SpawnPipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			const auto make = [&](const char* path, nvrhi::IBindingLayout* layout)
			{
				nvrhi::ComputePipelineDesc description;
				description.CS = m_Shaders->get(path, ShaderStage::Compute);
				description.addBindingLayout(layout);
				return m_Device->createComputePipeline(description);
			};
			m_SpawnPipeline = make(engine_assets::PARTICLE_SPAWN_SHADER, m_Layout);
			m_SimPipeline = make(engine_assets::PARTICLE_SIM_SHADER, m_Layout);
			m_SortFillPipeline = make(engine_assets::PARTICLE_SORT_FILL_SHADER, m_SortFillLayout);
			m_SortStepPipeline = make(engine_assets::PARTICLE_SORT_STEP_SHADER, m_SortStepLayout);
			if (!m_SpawnPipeline || !m_SimPipeline || !m_SortFillPipeline || !m_SortStepPipeline)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle pipelines");
				m_Failed = true;
				return;
			}
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		ViewCache& view = m_PerView.get(frame.View->Target);
		if (view.BindingSet == nullptr || view.PoolGeneration != pool.generation())
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pool.emitters()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, pool.tables()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(3, pool.particles()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(4, viewport.depth()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(5, viewport.normal()));
			view.BindingSet = m_Device->createBindingSet(set, m_Layout);
			view.PoolGeneration = pool.generation();
			if (view.BindingSet == nullptr)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle bindings");
				m_Failed = true;
				return;
			}
		}

		const float step = std::clamp(frame.DeltaSeconds, 0.0f, MAX_STEP);
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

			SystemState& state = sim.Systems[system.Guid];
			const u32 revision = cache.effect_revision(system.Effect);
			if (state.Effect != system.Effect || state.Revision != revision || state.ClearCount != system.ClearCount)
			{
				release(state, pool, commandList); // new effect, re-cooked, or the particles were discarded (Vfx.stop with clear, restart): start the rings over
				state = SystemState{};
			}
			state.Effect = system.Effect;
			state.Revision = revision;
			state.ClearCount = system.ClearCount;
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
					if (const std::optional<ParticleAllocation> ring = pool.allocate(std::min(emitter.MaxParticles, quality.ParticleMaxPerEmitter)))
					{
						state.Rings[i] = *ring;
						state.Heads[i] = 0;
						state.AliveFor[i] = 0.0f;
						// the slots hold what an earlier ring left there: all dead, and the curves of this emitter
						const std::vector<u8> zeros(static_cast<size_t>(ring->Capacity) * sizeof(ParticleGpu), 0);
						commandList->writeBuffer(pool.particles(), zeros.data(), zeros.size(), static_cast<u64>(ring->Base) * sizeof(ParticleGpu));
						write_tables(commandList, pool.tables(), ring->Id, emitter);
					}
					else
					{
						state.Failed[i] = true;
						if (!m_WarnedFull)
						{
							m_WarnedFull = true;
							WI_PARTICLE_LOG(Warning, "The particle pool ({0} particles) is full: an emitter is not played", pool.capacity());
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

		for (auto it = sim.Systems.begin(); it != sim.Systems.end();)
		{
			if (it->second.Seen != m_Pass)
			{
				release(it->second, pool, commandList);
				it = sim.Systems.erase(it);
			}
			else
			{
				++it;
			}
		}

		const u32 idEnd = pool.id_end();
		if (idEnd == 0)
		{
			return;
		}

		// the room the sorts need in the sort buffer
		u32 sortEntries = 0;
		for (const Work& item : work)
		{
			if (item.Draw && sorts(*item.Emitter))
			{
				sortEntries += padded(item.Ring.Capacity);
			}
		}
		if (!pool.ensure_sort(m_Device, sortEntries))
		{
			return;
		}
		if (sim.SortFillSet == nullptr || sim.SortSetPoolGeneration != pool.generation() || sim.SortSetSortGeneration != pool.sort_generation())
		{
			nvrhi::BindingSetDesc fill;
			fill.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants));
			fill.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(SortPush)));
			fill.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pool.emitters()));
			fill.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, pool.particles()));
			fill.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(3, pool.sort_buffer()));
			sim.SortFillSet = m_Device->createBindingSet(fill, m_SortFillLayout);
			nvrhi::BindingSetDesc step;
			step.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(SortPush)));
			step.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(3, pool.sort_buffer()));
			sim.SortStepSet = m_Device->createBindingSet(step, m_SortStepLayout);
			sim.SortSetPoolGeneration = pool.generation();
			sim.SortSetSortGeneration = pool.sort_generation();
			if (sim.SortFillSet == nullptr || sim.SortStepSet == nullptr)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle sort bindings");
				m_Failed = true;
				return;
			}
		}

		// the emitter records, and the order the spawn threads belong to them
		std::vector<EmitterGpu> records(idEnd);
		u32 totalSpawn = 0;
		u32 sortNext = 0;
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
			u32 sortStart = 0;
			u32 sortCount = 0;
			if (item.Draw && sorts(*item.Emitter))
			{
				sortCount = padded(item.Ring.Capacity);
				sortStart = sortNext + 1;
				sortNext += sortCount;
			}
			records[item.Ring.Id] = make_record(*item.Emitter, *item.System, item.Ring, item.Head, item.Spawn, totalSpawn, item.Index, sortStart, quality.ParticleCollision);
			totalSpawn += item.Spawn;
			if (item.Draw)
			{
				const glm::vec3 offset = glm::vec3(item.System->World[3]) - camera;
				ParticleDraw draw{ item.Ring.Id, item.Ring.Base, item.Ring.Capacity, item.Emitter->Material, item.Emitter->Blend, sortStart, sortCount };
				draws.push_back(Sorted{ draw, item.System->SortBias, glm::dot(offset, offset) });
			}
		}
		std::sort(draws.begin(), draws.end(), [](const Sorted& a, const Sorted& b)
			{
				return a.Bias != b.Bias ? a.Bias < b.Bias : a.Distance > b.Distance; // the lower bias first, the farther system first
			});
		for (const Sorted& sorted : draws)
		{
			pool.Draws.push_back(sorted.Draw);
		}

		commandList->writeBuffer(pool.emitters(), records.data(), records.size() * sizeof(EmitterGpu));

		const FrameConstants& frameConstants = frame.Scene->constants();
		SimConstants constants;
		constants.EmitterCount = idEnd;
		constants.TotalSpawn = totalSpawn;
		constants.SlotEnd = pool.slot_end();
		constants.FrameIndex = static_cast<u32>(frame.FrameIndex);
		constants.DeltaTime = step;
		constants.Time = frame.Time;
		constants.Collide = frame.Scene->has_camera() ? 1u : 0u;
		constants.ViewProjection = frameConstants.ViewProjection;
		constants.InverseViewProjection = frameConstants.InverseViewProjection;
		constants.Viewport = glm::vec4(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()), 1.0f / static_cast<float>(viewport.width()), 1.0f / static_cast<float>(viewport.height()));
		commandList->writeBuffer(m_Constants, &constants, sizeof(constants));

		if (totalSpawn > 0)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_SpawnPipeline;
			state.addBindingSet(view.BindingSet);
			commandList->setComputeState(state);
			commandList->dispatch((totalSpawn + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
		}

		if (constants.SlotEnd > 0)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_SimPipeline;
			state.addBindingSet(view.BindingSet);
			commandList->setComputeState(state);
			commandList->dispatch((constants.SlotEnd + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
		}

		for (const ParticleDraw& draw : pool.Draws)
		{
			if (draw.SortStart != 0)
			{
				sort_emitter(commandList, pool, sim, draw);
			}
		}
	}
}
