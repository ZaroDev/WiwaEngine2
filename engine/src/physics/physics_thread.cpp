#include <pch.h>
#include "physics_thread.h"
#include "jolt_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace wiwa
{
	namespace
	{
		constexpr JPH::uint kMaxBodies = 65536;
		constexpr JPH::uint kMaxBodyPairs = 65536;
		constexpr JPH::uint kMaxContactConstraints = 10240;
		constexpr size_t kTempAllocatorBytes = 10 * 1024 * 1024;
		constexpr u32 kMaxFixedRateHz = 1000;

		// Jolt's process-wide state (allocator hooks, factory, registered types), set up by the first world and torn down by the last.
		std::mutex s_RuntimeMutex;
		u32 s_RuntimeUsers = 0;

		void jolt_trace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			WI_PHYSICS_LOG(Info, "Jolt: {0}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool jolt_assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			WI_PHYSICS_LOG(Error, "Jolt assert {0}:{1}: ({2}) {3}", file, line, expression, message != nullptr ? message : "");
			return true; // break, as Jolt does by default
		}
#endif

		void acquire_runtime()
		{
			std::lock_guard<std::mutex> lock(s_RuntimeMutex);
			if (s_RuntimeUsers++ == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Trace = jolt_trace;
				JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = jolt_assert_failed;)
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}
		}

		void release_runtime()
		{
			std::lock_guard<std::mutex> lock(s_RuntimeMutex);
			if (--s_RuntimeUsers == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		// Two fixed layers until the project layers exist (a later task): static bodies never collide with each other.
		constexpr JPH::ObjectLayer kLayerStatic = 0;
		constexpr JPH::ObjectLayer kLayerMoving = 1;
		constexpr JPH::BroadPhaseLayer kBroadStatic(0);
		constexpr JPH::BroadPhaseLayer kBroadMoving(1);

		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return layer == kLayerStatic ? kBroadStatic : kBroadMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer == kBroadStatic ? "static" : "moving"; }
#endif
		};

		class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override { return layer == kLayerMoving || broad == kBroadMoving; }
		};

		class ObjectPairs final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == kLayerMoving || b == kLayerMoving; }
		};

		u32 worker_count()
		{
			const int hardware = static_cast<int>(std::thread::hardware_concurrency());
			return static_cast<u32>(std::clamp(hardware - 2, 1, 8)); // leave room for the main and render threads
		}

		PhysicsSettings sanitized(PhysicsSettings settings)
		{
			settings.FixedRateHz = std::clamp<u32>(settings.FixedRateHz, 1, kMaxFixedRateHz);
			settings.MaxStepsPerWake = std::max<u32>(settings.MaxStepsPerWake, 1);
			settings.VelocityIterations = std::max<u32>(settings.VelocityIterations, 1);
			settings.PositionIterations = std::max<u32>(settings.PositionIterations, 1);
			settings.SleepThreshold = std::max(settings.SleepThreshold, 0.0f);
			return settings;
		}
	}

	// Everything Jolt that one world needs. Created and destroyed on the physics thread, never seen by any other.
	struct PhysicsWorld
	{
		// the layer interfaces must outlive the system
		BroadPhaseLayers BroadPhase{};
		ObjectVsBroadPhase ObjectVsBroad{};
		ObjectPairs Pairs{};
		JPH::TempAllocatorImpl Temp{ kTempAllocatorBytes };
		JPH::JobSystemThreadPool Jobs{ JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(worker_count()) };
		JPH::PhysicsSystem System{};
		PhysicsSettings Settings{};

		PhysicsWorld()
		{
			System.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, BroadPhase, ObjectVsBroad, Pairs);
		}

		void apply(const PhysicsSettings& settings)
		{
			Settings = sanitized(settings);
			System.SetGravity(JPH::Vec3(Settings.Gravity.x, Settings.Gravity.y, Settings.Gravity.z));
			JPH::PhysicsSettings jolt = System.GetPhysicsSettings();
			jolt.mNumVelocitySteps = Settings.VelocityIterations;
			jolt.mNumPositionSteps = Settings.PositionIterations;
			jolt.mPointVelocitySleepThreshold = Settings.SleepThreshold;
			System.SetPhysicsSettings(jolt);
		}

		double fixed_dt() const { return 1.0 / static_cast<double>(Settings.FixedRateHz); }

		void execute(const PhysicsCommand& command)
		{
			std::visit([this](const auto& c)
			{
				using T = std::decay_t<decltype(c)>;
				if constexpr (std::is_same_v<T, SetSettings>)
				{
					apply(c.Settings);
				}
			}, command);
		}
	};

	void PhysicsThread::start(const PhysicsSettings& settings)
	{
		stop();

		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Queue.clear();
			m_Stopping = false;
			m_Accepting = true;
		}

		std::promise<void> ready;
		std::future<void> readyResult = ready.get_future();
		m_Thread = std::thread([this, settings, &ready]() { run(settings, ready); });
		readyResult.wait();
		m_Running = true;
	}

	void PhysicsThread::stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Stopping = true;
			m_Accepting = false;
		}
		m_Wake.notify_all();
		m_QueueNotFull.notify_all();

		if (m_Thread.joinable())
		{
			m_Thread.join();
		}

		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Queue.clear();
		}
		{
			std::lock_guard<std::mutex> lock(m_SnapshotMutex);
			m_Ring.fill(nullptr);
			m_Published = 0;
		}
		m_Running = false;
	}

	bool PhysicsThread::push(PhysicsCommand&& command)
	{
		std::unique_lock<std::mutex> lock(m_QueueMutex);
		if (m_Accepting && m_Queue.size() >= PHYSICS_COMMAND_QUEUE_CAPACITY)
		{
			WI_PHYSICS_LOG(Warning, "Physics command queue is full ({0} commands), waiting for the physics thread", PHYSICS_COMMAND_QUEUE_CAPACITY);
			m_QueueNotFull.wait(lock, [this]() { return !m_Accepting || m_Queue.size() < PHYSICS_COMMAND_QUEUE_CAPACITY; });
		}

		if (!m_Accepting)
		{
			WI_PHYSICS_LOG(Warning, "Physics command dropped: no physics world is running");
			return false;
		}

		m_Queue.push_back(std::move(command));
		return true;
	}

	Ref<const PhysicsStepSnapshot> PhysicsThread::latest()
	{
		std::lock_guard<std::mutex> lock(m_SnapshotMutex);
		return m_Published >= 1 ? m_Ring[(m_Published - 1) % PHYSICS_SNAPSHOT_RING_SIZE] : nullptr;
	}

	Ref<const PhysicsStepSnapshot> PhysicsThread::previous()
	{
		std::lock_guard<std::mutex> lock(m_SnapshotMutex);
		return m_Published >= 2 ? m_Ring[(m_Published - 2) % PHYSICS_SNAPSHOT_RING_SIZE] : nullptr;
	}

	bool PhysicsThread::drain(std::deque<PhysicsCommand>& into)
	{
		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			into.swap(m_Queue);
		}
		m_QueueNotFull.notify_all();
		return !into.empty();
	}

	void PhysicsThread::publish(Ref<const PhysicsStepSnapshot> snapshot)
	{
		std::lock_guard<std::mutex> lock(m_SnapshotMutex);
		m_Ring[m_Published % PHYSICS_SNAPSHOT_RING_SIZE] = std::move(snapshot); // releases the oldest one
		m_Published++;
	}

	void PhysicsThread::run(PhysicsSettings settings, std::promise<void>& ready)
	{
		using Clock = std::chrono::steady_clock;
		using Seconds = std::chrono::duration<double>;

		s_LiveThreads++;
		acquire_runtime();
		{
			Scope<PhysicsWorld> world = CreateScope<PhysicsWorld>();
			world->apply(settings);

			u64 stepIndex = 0;
			double time = 0.0;

			// the world before its first step, so latest() is not null once start() returns
			{
				Ref<PhysicsStepSnapshot> snapshot = CreateRef<PhysicsStepSnapshot>();
				snapshot->FixedDt = static_cast<float>(world->fixed_dt());
				publish(std::move(snapshot));
			}
			ready.set_value(); // start() returns; `ready` is gone from here on

			std::deque<PhysicsCommand> commands;
			double accumulator = 0.0;
			Clock::time_point last = Clock::now();
			while (true)
			{
				const Clock::time_point now = Clock::now();
				accumulator += Seconds(now - last).count();
				last = now;

				u32 steps = 0;
				while (accumulator >= world->fixed_dt() && steps < world->Settings.MaxStepsPerWake)
				{
					// the commands submitted so far apply to this step, in submission order
					if (drain(commands))
					{
						for (const PhysicsCommand& command : commands)
						{
							world->execute(command);
						}
						commands.clear();
					}

					const double dt = world->fixed_dt();
					const JPH::EPhysicsUpdateError error = world->System.Update(static_cast<float>(dt), 1, &world->Temp, &world->Jobs);
					if (error != JPH::EPhysicsUpdateError::None)
					{
						WI_PHYSICS_LOG(Warning, "Physics step {0} reported error flags {1}", stepIndex + 1, static_cast<u32>(error));
					}

					stepIndex++;
					time += dt;
					accumulator -= dt;
					steps++;

					Ref<PhysicsStepSnapshot> snapshot = CreateRef<PhysicsStepSnapshot>();
					snapshot->StepIndex = stepIndex;
					snapshot->Time = time;
					snapshot->FixedDt = static_cast<float>(dt);
					publish(std::move(snapshot));
				}

				// past the cap the simulation falls behind real time instead of stalling: the backlog is dropped
				if (accumulator >= world->fixed_dt())
				{
					accumulator = std::fmod(accumulator, world->fixed_dt());
				}

				const auto untilNextStep = std::chrono::duration_cast<Clock::duration>(Seconds(world->fixed_dt() - accumulator));
				std::unique_lock<std::mutex> lock(m_QueueMutex);
				if (m_Wake.wait_for(lock, untilNextStep, [this]() { return m_Stopping; }))
				{
					break;
				}
			}
		}
		release_runtime();
		s_LiveThreads--;
	}
}
