#include <pch.h>
#include "vehicle_assists.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		// How far past the start of an assist's band a slip is, 0..1: the band starts at 0.8 of the tire's peak and is as wide again, so the
		// assist works before the tire passes its peak (and before 0.1, where Jolt stops shifting up while a wheel slips).
		float over_peak(float slip, float peak)
		{
			const float start = 0.8f * std::max(peak, 1e-3f);
			return std::clamp((slip - start) / start, 0.0f, 1.0f);
		}

		int find_wheel(const AssistInput& in, i32 axle, WheelSide side)
		{
			for (size_t i = 0; i < in.Wheels.size(); i++)
			{
				if (in.Wheels[i].Axle == axle && in.Wheels[i].Side == side)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}
	}

	AssistOutput run_assists(const AssistInput& in, const AssistStrengths& strengths, const VehicleSettingsData& settings, AssistState& state)
	{
		AssistOutput out;
		out.Throttle = std::clamp(in.Driver.Throttle, 0.0f, 1.0f);
		out.Handbrake = std::clamp(in.Driver.Handbrake, 0.0f, 1.0f);
		const float brake = std::clamp(in.Driver.Brake, 0.0f, 1.0f);
		out.WheelBrake.assign(in.Wheels.size(), 0.0f);
		for (size_t i = 0; i < in.Wheels.size(); i++)
		{
			out.WheelBrake[i] = in.Wheels[i].Brakes ? brake : 0.0f;
		}

		// steering
		const float help = std::clamp(strengths.SteeringHelp, 0.0f, 1.0f);
		const float driverSteer = std::clamp(in.Driver.Steer, -1.0f, 1.0f);
		if (help <= 0.0f)
		{
			out.Steer = driverSteer;
		}
		else
		{
			const float limit = 1.0f + (steer_limit_at(settings, std::abs(in.Speed)) - 1.0f) * help;
			const float target = driverSteer * limit;
			const float step = settings.SteerSpeed * in.Dt;
			out.Steer = state.Steer + std::clamp(target - state.Steer, -step, step);
		}
		state.Steer = out.Steer;

		// ABS
		const float abs = std::clamp(strengths.Abs, 0.0f, 1.0f);
		for (size_t i = 0; i < in.Wheels.size(); i++)
		{
			const AssistWheelState& w = in.Wheels[i];
			if (out.WheelBrake[i] > 0.0f && w.InContact && abs > 0.0f)
			{
				const float ease = abs * over_peak(-w.SlipRatio, w.PeakSlip);
				if (ease > 0.0f)
				{
					out.WheelBrake[i] *= 1.0f - ease;
					out.Active |= AssistAbs;
				}
			}
		}

		// traction control
		const float tc = std::clamp(strengths.TractionControl, 0.0f, 1.0f);
		if (tc > 0.0f)
		{
			float excess = 0.0f;
			for (size_t i = 0; i < in.Wheels.size(); i++)
			{
				const AssistWheelState& w = in.Wheels[i];
				if (w.Driven && w.InContact)
				{
					const float over = over_peak(w.SlipRatio, w.PeakSlip);
					excess = std::max(excess, over);
					if (over > 0.0f)
					{
						out.WheelBrake[i] = std::max(out.WheelBrake[i], tc * over);
					}
				}
			}
			constexpr float kRecoverPerSecond = 1.5f;
			state.TcCut = std::max(excess, state.TcCut - kRecoverPerSecond * in.Dt);
			if (state.TcCut > 0.0f && out.Throttle > 0.0f)
			{
				out.Throttle *= 1.0f - tc * state.TcCut;
				out.Active |= AssistTc;
			}
		}
		else
		{
			state.TcCut = 0.0f;
		}

		// stability
		const float stability = std::clamp(strengths.Stability, 0.0f, 1.0f);
		const float speed = std::abs(in.Speed);
		if (stability > 0.0f && speed > 3.0f && in.Wheelbase > 0.0f)
		{
			const float direction = in.Speed >= 0.0f ? 1.0f : -1.0f;
			float target = direction * speed * std::tan(out.Steer * in.MaxSteerAngle) / in.Wheelbase;
			const float gripCap = 9.81f / speed;
			target = std::clamp(target, -gripCap, gripCap);
			const float error = in.YawRate - target;
			const float amount = stability * std::clamp((std::abs(error) - 0.1f) / 0.5f, 0.0f, 1.0f) * 0.6f;
			if (amount > 0.0f)
			{
				const bool oversteer = std::abs(in.YawRate) > std::abs(target) && (in.YawRate * target >= 0.0f || std::abs(target) < 1e-3f);
				const float turn = oversteer ? in.YawRate : target; // the way the car is turning (oversteer) or asked to (understeer)
				const WheelSide outer = turn > 0.0f ? WheelSide::Left : WheelSide::Right; // turning right: the left side is outside
				const WheelSide inner = outer == WheelSide::Left ? WheelSide::Right : WheelSide::Left;
				i32 rearAxle = 0;
				for (const AssistWheelState& w : in.Wheels)
				{
					rearAxle = std::max(rearAxle, w.Axle);
				}
				const int wheel = oversteer ? find_wheel(in, 0, outer) : find_wheel(in, rearAxle, inner);
				if (wheel >= 0)
				{
					out.WheelBrake[static_cast<size_t>(wheel)] = std::max(out.WheelBrake[static_cast<size_t>(wheel)], amount);
					out.Throttle *= 1.0f - 0.5f * amount;
					out.Active |= AssistStability;
				}
			}
		}
		return out;
	}
}
