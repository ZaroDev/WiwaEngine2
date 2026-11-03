#include <pch.h>
#include "physics_thread.h"
#include "jolt_config.h"
#include "physics_world.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <exception>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>

namespace wiwa
{
	namespace
	{
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
	}

	bool PhysicsThread::start(const PhysicsSettings& settings)
	{
		stop();

		const u64 generation = ++m_Generation;
		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Queue.clear();
			m_Stopping = false;
			m_Accepting = true;
		}

		// the promise belongs to the thread: whatever happens there, it is fulfilled (or broken) and the wait below ends
		std::promise<bool> ready;
		std::future<bool> readyResult = ready.get_future();
		m_Thread = std::thread([this, settings, generation, promise = std::move(ready)]() mutable { run(settings, generation, std::move(promise)); });

		bool started = false;
		try
		{
			started = readyResult.get();
		}
		catch (const std::exception& error)
		{
			WI_PHYSICS_LOG(Error, "The physics world could not start: {0}", error.what());
		}

		if (!started)
		{
			stop(); // joins the thread, which has ended
			return false;
		}
		m_Running = true;
		return true;
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

	bool PhysicsThread::push(PhysicsCommand&& command, u64 generation)
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
		if (generation != 0 && generation != m_Generation)
		{
			return false; // meant for a world that is gone: dropped on purpose, not an error
		}

		m_Queue.push_back(QueuedCommand{ generation, std::move(command) });
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

	bool PhysicsThread::drain(std::deque<QueuedCommand>& into)
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

	void PhysicsThread::run(PhysicsSettings settings, u64 generation, std::promise<bool> ready)
	{
		using Clock = std::chrono::steady_clock;
		using Seconds = std::chrono::duration<double>;

		s_LiveThreads++;
		acquire_runtime();
		{
			Scope<IPhysicsWorld> world;
			try
			{
				world = create_physics_world(settings, PhysicsLayers{});
			}
			catch (const std::exception& error)
			{
				WI_PHYSICS_LOG(Error, "Making the physics world failed: {0}", error.what());
			}
			catch (...)
			{
				WI_PHYSICS_LOG(Error, "Making the physics world failed");
			}

			if (world == nullptr)
			{
				ready.set_value(false);
			}
			else
			{
				u64 stepIndex = 0;
				double time = 0.0;

				// the world before its first step, so latest() is not null once start() returns
				{
					Ref<PhysicsStepSnapshot> snapshot = CreateRef<PhysicsStepSnapshot>();
					snapshot->World = generation;
					snapshot->FixedDt = static_cast<float>(world->fixed_dt());
					publish(std::move(snapshot));
				}
				ready.set_value(true); // start() returns

				std::deque<QueuedCommand> commands;
				double accumulator = 0.0;
				Clock::time_point last = Clock::now();
				while (true)
				{
					const Clock::time_point now = Clock::now();
					if (!m_Paused)
					{
						accumulator += Seconds(now - last).count();
					}
					last = now;

					u32 steps = 0;
					while (accumulator >= world->fixed_dt() && steps < world->max_steps_per_wake())
					{
						// the commands submitted so far apply to this step, in submission order
						if (drain(commands))
						{
							for (const QueuedCommand& queued : commands)
							{
								if (queued.Generation == 0 || queued.Generation == generation)
								{
									world->execute(queued.Command);
								}
							}
							commands.clear();
						}

						const double dt = world->fixed_dt();
						world->step();

						stepIndex++;
						time += dt;
						accumulator -= dt;
						steps++;

						Ref<PhysicsStepSnapshot> snapshot = CreateRef<PhysicsStepSnapshot>();
						snapshot->World = generation;
						snapshot->StepIndex = stepIndex;
						snapshot->Time = time;
						snapshot->FixedDt = static_cast<float>(dt);
						world->write_poses(*snapshot);
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
		}
		release_runtime();
		s_LiveThreads--;
	}
}
