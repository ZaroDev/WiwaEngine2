#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

#include "physics_commands.h"
#include "physics_snapshot.h"

namespace wiwa
{
	// How many commands may wait for the physics thread before push blocks.
	constexpr u32 PHYSICS_COMMAND_QUEUE_CAPACITY = 4096;
	// How many published snapshots the thread keeps; older ones are released when the ring wraps.
	constexpr u32 PHYSICS_SNAPSHOT_RING_SIZE = 4;

	// The thread that owns the Jolt world. It steps on its own clock at a fixed rate (an accumulator, at most MaxStepsPerWake steps per
	// wake), applies the queued commands at the start of each step and publishes an immutable snapshot after each step. Jolt types stay
	// in physics_thread.cpp; nothing here or in what it hands out refers to them. The thread never calls Engine::get_module.
	class PhysicsThread
	{
	public:
		PhysicsThread() = default;
		~PhysicsThread() { stop(); }

		PhysicsThread(const PhysicsThread&) = delete;
		PhysicsThread& operator=(const PhysicsThread&) = delete;

		// Builds a new, empty world on a new thread and returns once the world exists and the step-0 snapshot is published (so latest()
		// is not null afterwards). A running world is stopped first. Called by the owner (the main thread), like stop(). False (logged,
		// nothing runs) when the world could not be made. Every start begins a new generation().
		bool start(const PhysicsSettings& settings);

		// Stops the thread, joins it, and frees the world, the queued commands and the snapshots. Safe to call again.
		void stop();

		// Any thread: queues a command for the start of the next step. Blocks while the queue is full (and logs a warning), never drops
		// one silently. Returns false (the command is dropped and logged) when no world is running. With a generation other than 0, the
		// command is meant for that world only: it is dropped (false) when another world runs, or later when the queue is drained.
		bool push(PhysicsCommand&& command, u64 generation = 0);

		// Any thread: the newest published snapshot and the one before it. Null when no world runs (previous() also before the first step).
		Ref<const PhysicsStepSnapshot> latest();
		Ref<const PhysicsStepSnapshot> previous();

		bool is_running() const { return m_Running; }
		// The world that runs (or ran last): 1 for the first start, one more for each start after. Snapshots carry it.
		u64 generation() const { return m_Generation; }

		// Any thread: while paused the world does not step (the game clock is paused); time spent paused is not caught up afterwards.
		// Commands are still taken and applied while paused, so push never waits for good. start() clears the pause.
		void set_paused(bool paused) { m_Paused = paused; }

		// How many physics thread loops are alive in the process, for checks that stop() leaves nothing behind.
		static u32 live_threads() { return s_LiveThreads; }

	private:
		struct QueuedCommand
		{
			u64 Generation = 0; // 0: any world
			PhysicsCommand Command{};
		};

		void run(PhysicsSettings settings, u64 generation, std::promise<bool> ready);
		bool drain(std::deque<QueuedCommand>& into);
		void publish(Ref<const PhysicsStepSnapshot> snapshot);

		std::thread m_Thread{};
		std::atomic<bool> m_Running{ false };
		std::atomic<bool> m_Paused{ false };
		std::atomic<u64> m_Generation{ 0 };

		// command queue (MPSC): producers push, the physics thread drains at the start of each step
		std::mutex m_QueueMutex{};
		std::condition_variable m_QueueNotFull{};
		std::condition_variable m_Wake{}; // stop() wakes the thread early from its wait for the next step
		std::deque<QueuedCommand> m_Queue{};
		bool m_Stopping = false;
		bool m_Accepting = false;

		// snapshot ring
		std::mutex m_SnapshotMutex{};
		std::array<Ref<const PhysicsStepSnapshot>, PHYSICS_SNAPSHOT_RING_SIZE> m_Ring{};
		u64 m_Published = 0; // how many snapshots were published since start (the newest sits at (m_Published - 1) % ring size)

		inline static std::atomic<u32> s_LiveThreads{ 0 };
	};
}
