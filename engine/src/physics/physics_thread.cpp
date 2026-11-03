#include <pch.h>
#include "physics_thread.h"
#include "jolt_config.h"
#include "jolt_runtime.h"
#include "physics_world.h"

#include <algorithm>
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
	bool PhysicsThread::start(const PhysicsSettings& settings)
	{
		stop();

		const u64 generation = ++m_Generation;
		m_Paused = false; // a new world runs until its owner pauses it (a pause belongs to the world it was set for)
		m_FoldedSteps = 0;
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
			m_Pending.clear();
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

	std::vector<Ref<const PhysicsStepSnapshot>> PhysicsThread::take_steps()
	{
		std::lock_guard<std::mutex> lock(m_SnapshotMutex);
		std::vector<Ref<const PhysicsStepSnapshot>> steps(std::make_move_iterator(m_Pending.begin()), std::make_move_iterator(m_Pending.end()));
		m_Pending.clear();
		return steps;
	}

	void PhysicsThread::publish(Ref<PhysicsStepSnapshot> snapshot)
	{
		std::lock_guard<std::mutex> lock(m_SnapshotMutex);
		if (m_Pending.size() >= PHYSICS_PENDING_STEP_LIMIT)
		{
			// nobody took the steps for a long while: the oldest goes, but its begin and end events go into the next one (in front, so
			// they keep their order). That one is past the ring (the limit is larger), so nobody else holds it and a copy can replace it.
			const Ref<const PhysicsStepSnapshot> oldest = std::move(m_Pending.front());
			m_Pending.pop_front();
			Ref<PhysicsStepSnapshot> folded = CreateRef<PhysicsStepSnapshot>(*m_Pending.front());
			std::vector<PhysicsEvent> events;
			events.reserve(oldest->Events.size() + folded->Events.size());
			for (const PhysicsEvent& event : oldest->Events)
			{
				if (event.Kind != PhysicsEventKind::CollisionStay)
				{
					events.push_back(event);
				}
			}
			events.insert(events.end(), folded->Events.begin(), folded->Events.end());
			folded->Events = std::move(events);
			m_Pending.front() = std::move(folded);
			if (m_FoldedSteps++ == 0)
			{
				WI_PHYSICS_LOG(Warning, "Physics steps are not taken by the main thread: the oldest are folded (their Stay events are dropped)");
			}
		}
		if (snapshot->StepIndex != 0)
		{
			m_Pending.push_back(snapshot); // the world before its first step is not a completed step
		}
		m_Ring[m_Published % PHYSICS_SNAPSHOT_RING_SIZE] = std::move(snapshot); // releases the oldest one
		m_Published++;
	}

	void PhysicsThread::run(PhysicsSettings settings, u64 generation, std::promise<bool> ready)
	{
		using Clock = std::chrono::steady_clock;
		using Seconds = std::chrono::duration<double>;

		s_LiveThreads++;
		acquire_jolt_runtime();
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

					const auto execute_queued = [&]()
					{
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
					};

					// paused: no step, but the commands are still taken (in order), so a producer never waits on a full queue for good
					if (m_Paused)
					{
						execute_queued();
					}

					u32 steps = 0;
					while (accumulator >= world->fixed_dt() && steps < world->max_steps_per_wake())
					{
						// the commands submitted so far apply to this step, in submission order
						execute_queued();

						const double dt = world->fixed_dt();
						const Clock::time_point stepBegan = Clock::now();
						world->step();
						const float stepMs = std::chrono::duration<float, std::milli>(Clock::now() - stepBegan).count();

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
						snapshot->BodyCount = static_cast<u32>(snapshot->Poses.size());
						snapshot->ActiveBodies = static_cast<u32>(std::count_if(snapshot->Poses.begin(), snapshot->Poses.end(), [](const BodyPose& pose) { return pose.Active; }));
						snapshot->StepMs = stepMs;
						world->take_events(*snapshot);
						world->write_query(*snapshot);
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
		release_jolt_runtime();
		s_LiveThreads--;
	}
}
