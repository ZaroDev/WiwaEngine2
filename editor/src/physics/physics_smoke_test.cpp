#include <editor_common.h>
#include "physics_smoke_test.h"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include <core/engine.h>
#include <physics/physics_module.h>

namespace wiwa
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_PHYSICS_LOG(Error, "Smoke test FAILED: {0}", what);
				}
			}
		};

		bool near_equal(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

		void check_module_registered(Check& check)
		{
			check(Engine::get().get_module<PhysicsModule>() != nullptr, "module_registered: the engine has a PhysicsModule");
		}

		// Started at 60 Hz and left alone for 500 ms, the thread has stepped about 30 times, and the published time only moves forward.
		void check_fixed_rate(Check& check)
		{
			PhysicsThread thread;
			PhysicsSettings settings;
			settings.FixedRateHz = 60;
			thread.start(settings);

			const Ref<const PhysicsStepSnapshot> first = thread.latest();
			check(first != nullptr, "fixed_rate_first_snapshot: start() publishes a step-0 snapshot");
			check(thread.is_running(), "fixed_rate_running: the thread runs after start()");

			const Clock::time_point begin = Clock::now();
			double lastTime = first ? first->Time : 0.0;
			u64 lastStep = first ? first->StepIndex : 0;
			bool monotonic = true;
			bool consistent = true;
			bool ordered = true;
			u32 samples = 0;
			while (Clock::now() - begin < std::chrono::milliseconds(500))
			{
				const Ref<const PhysicsStepSnapshot> previous = thread.previous();
				const Ref<const PhysicsStepSnapshot> latest = thread.latest();
				if (latest)
				{
					monotonic = monotonic && latest->Time >= lastTime && latest->StepIndex >= lastStep;
					consistent = consistent && near_equal(latest->Time, static_cast<double>(latest->StepIndex) / 60.0, 1e-6);
					lastTime = latest->Time;
					lastStep = latest->StepIndex;
					samples++;
				}
				if (previous && latest)
				{
					ordered = ordered && previous->StepIndex < latest->StepIndex && previous->Time < latest->Time;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}

			const Ref<const PhysicsStepSnapshot> latest = thread.latest();
			const u64 steps = latest ? latest->StepIndex : 0;
			WI_PHYSICS_LOG(Info, "physics smoke: {0} steps in 500 ms at 60 Hz ({1} samples)", steps, samples);
			check(latest != nullptr && steps >= 25 && steps <= 35, "fixed_rate_60hz: 500 ms at 60 Hz gives 25..35 steps, got " + std::to_string(steps));
			check(latest != nullptr && latest->Time > (first ? first->Time : 0.0), "fixed_rate_time_advances: Time grows while the thread runs");
			check(monotonic, "fixed_rate_time_monotonic: Time and StepIndex never go back");
			check(consistent, "fixed_rate_time_matches_steps: Time is StepIndex fixed steps");
			check(latest != nullptr && near_equal(latest->FixedDt, 1.0 / 60.0, 1e-6), "fixed_rate_dt: FixedDt is 1/60");
			check(ordered, "fixed_rate_previous: previous() is older than latest()");

			// a SetSettings command takes effect on a later step
			PhysicsSettings faster;
			faster.FixedRateHz = 120;
			check(thread.push(SetSettings{ faster }), "set_settings_push: push succeeds while the thread runs");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			const Ref<const PhysicsStepSnapshot> after = thread.latest();
			check(after != nullptr && near_equal(after->FixedDt, 1.0 / 120.0, 1e-6), "set_settings_applied: FixedDt is 1/120 after SetSettings(120 Hz)");
			check(after != nullptr && after->StepIndex > steps, "set_settings_keeps_stepping: the world keeps stepping after SetSettings");

			thread.stop();
			check(!thread.is_running(), "stop_not_running: is_running() is false after stop()");
			check(PhysicsThread::live_threads() == 0, "stop_no_thread_left: stop() leaves no physics thread running, " + std::to_string(PhysicsThread::live_threads()) + " left");
			check(thread.latest() == nullptr && thread.previous() == nullptr, "stop_frees_snapshots: stop() releases the snapshots");
			check(!thread.push(SetSettings{}), "stop_push_refused: push fails once the thread is stopped");
			thread.stop(); // a second stop does nothing
			check(PhysicsThread::live_threads() == 0, "stop_twice: a second stop() is harmless");
		}

		// Play, Stop, Play: the module builds a fresh world each time and stop leaves nothing behind.
		void check_start_stop_loop(Check& check)
		{
			PhysicsModule* module = Engine::get().get_module<PhysicsModule>();
			if (module == nullptr)
			{
				return;
			}

			check(!module->running(), "loop_idle: no world runs before play");
			for (u32 round = 0; round < 3; round++)
			{
				const std::string tag = " (round " + std::to_string(round) + ")";
				module->start_world(PhysicsSettings{});
				check(module->running(), "loop_running: start_world runs a world" + tag);
				check(PhysicsThread::live_threads() == 1, "loop_one_thread: one physics thread runs" + tag);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				const Ref<const PhysicsStepSnapshot> latest = module->thread().latest();
				check(latest != nullptr && latest->StepIndex >= 2, "loop_steps: the world steps" + tag);
				check(latest != nullptr && latest->StepIndex <= 12, "loop_fresh: each world starts again from step 0" + tag);

				// start_world while a world runs replaces it
				module->start_world(PhysicsSettings{});
				check(PhysicsThread::live_threads() == 1, "loop_restart_one_thread: start_world on a running world replaces it" + tag);
				const Ref<const PhysicsStepSnapshot> restarted = module->thread().latest();
				check(restarted != nullptr && restarted->StepIndex <= 1, "loop_restart_fresh: the replacement world starts from step 0" + tag);

				module->stop_world();
				check(!module->running(), "loop_stopped: stop_world stops the world" + tag);
				check(PhysicsThread::live_threads() == 0, "loop_no_thread_left: stop_world leaves no physics thread" + tag);
				check(module->thread().latest() == nullptr, "loop_snapshots_freed: stop_world releases the snapshots" + tag);
			}
			module->stop_world(); // stop without a world does nothing
			check(!module->running(), "loop_stop_idle: stop_world without a world is harmless");
		}
	}

	bool run_physics_smoke_test()
	{
		Check check;

		check_module_registered(check);
		check_fixed_rate(check);
		check_start_stop_loop(check);

		if (check.Failures == 0)
		{
			WI_PHYSICS_LOG(Info, "Physics smoke test passed");
		}
		return check.Failures == 0;
	}
}
