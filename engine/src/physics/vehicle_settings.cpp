#include <pch.h>
#include "vehicle_settings.h"

#include <cmath>

#include <assets/wasset_io.h>

namespace wiwa
{
	namespace
	{
		bool fail(std::string* why, const char* what)
		{
			if (why != nullptr)
			{
				*why = what;
			}
			return false;
		}

		bool finite(float v) { return std::isfinite(v); }
		bool unit(float v) { return finite(v) && v >= 0.0f && v <= 1.0f; }
		bool non_negative(float v) { return finite(v) && v >= 0.0f; }
		bool positive(float v) { return finite(v) && v > 0.0f; }
	}

	VehicleSettingsData default_vehicle_settings() { return VehicleSettingsData{}; }

	bool vehicle_settings_valid(const VehicleSettingsData& d, std::string* why)
	{
		if (d.TorqueCurve.empty())
		{
			return fail(why, "the torque curve has no points");
		}
		for (size_t i = 0; i < d.TorqueCurve.size(); i++)
		{
			const TorquePoint& p = d.TorqueCurve[i];
			if (!non_negative(p.Rpm) || !non_negative(p.Torque))
			{
				return fail(why, "a torque point is negative or not finite");
			}
			if (i > 0 && !(p.Rpm > d.TorqueCurve[i - 1].Rpm))
			{
				return fail(why, "the torque curve is not sorted by rpm");
			}
		}
		if (!non_negative(d.MinRpm) || !finite(d.MaxRpm) || !(d.MinRpm < d.MaxRpm))
		{
			return fail(why, "minRpm must be below maxRpm");
		}
		if (!positive(d.EngineInertia) || !non_negative(d.EngineDamping))
		{
			return fail(why, "the engine inertia must be above 0 and the damping at least 0");
		}
		if (d.Gears.empty())
		{
			return fail(why, "there is no forward gear");
		}
		for (const float ratio : d.Gears)
		{
			if (!positive(ratio))
			{
				return fail(why, "a gear ratio is not above 0");
			}
		}
		for (const float ratio : d.ReverseGears)
		{
			if (!positive(ratio))
			{
				return fail(why, "a reverse gear ratio is not above 0");
			}
		}
		// what Jolt asserts on (VehicleTransmission, VehicleDifferential): a clutch that grips, shift rpms above 0 and in order, and an
		// automatic gearbox that can shift up before the engine's top
		if (!positive(d.ShiftUpRpm) || !positive(d.ShiftDownRpm) || !non_negative(d.ShiftTime) || !positive(d.ClutchStrength))
		{
			return fail(why, "shift rpms and the clutch strength must be above 0, the shift time at least 0");
		}
		if (!(d.ShiftUpRpm > d.ShiftDownRpm))
		{
			return fail(why, "shiftUpRpm must be above shiftDownRpm");
		}
		if (d.AutomaticGearbox && !(d.ShiftUpRpm < d.MaxRpm))
		{
			return fail(why, "an automatic gearbox needs shiftUpRpm below maxRpm");
		}
		for (const VehicleDifferentialData& diff : d.Differentials)
		{
			if (diff.Axle < 0 || !positive(diff.Ratio) || !finite(diff.LimitedSlipRatio) || !(diff.LimitedSlipRatio > 1.0f) || !non_negative(diff.EngineTorqueRatio))
			{
				return fail(why, "a differential has a negative axle, a ratio not above 0, or a limited slip ratio not above 1");
			}
		}
		float torqueShares = 0.0f;
		for (const VehicleDifferentialData& diff : d.Differentials)
		{
			torqueShares += diff.EngineTorqueRatio;
		}
		if (!d.Differentials.empty() && !(torqueShares > 0.0f))
		{
			return fail(why, "the differentials share no engine torque");
		}
		for (const float stiffness : d.AntiRollStiffness)
		{
			if (!non_negative(stiffness))
			{
				return fail(why, "an anti-roll stiffness is negative");
			}
		}
		if (!tire_model_valid(d.FrontTire) || !tire_model_valid(d.RearTire))
		{
			return fail(why, "a tire is not valid");
		}
		if (!non_negative(d.DragArea) || !non_negative(d.DownforceArea) || !unit(d.DownforceFront))
		{
			return fail(why, "an aero value is out of range");
		}
		if (!non_negative(d.MaxBrakeTorque) || !non_negative(d.HandbrakeTorque) || !unit(d.BrakeBiasFront))
		{
			return fail(why, "a brake value is out of range");
		}
		if (!unit(d.TractionControl) || !unit(d.Abs) || !unit(d.Stability) || !unit(d.SteeringHelp))
		{
			return fail(why, "an assist is outside 0..1");
		}
		if (!positive(d.SteerSpeed))
		{
			return fail(why, "the steer speed must be above 0");
		}
		for (size_t i = 0; i < d.SteerLimit.size(); i++)
		{
			const SteerLimitPoint& p = d.SteerLimit[i];
			if (!non_negative(p.Speed) || !unit(p.Fraction) || (i > 0 && !(p.Speed > d.SteerLimit[i - 1].Speed)))
			{
				return fail(why, "the steer limit is not sorted by speed or a fraction is outside 0..1");
			}
		}
		return true;
	}

	float steer_limit_at(const VehicleSettingsData& data, float speed)
	{
		const std::vector<SteerLimitPoint>& points = data.SteerLimit;
		if (points.empty())
		{
			return 1.0f;
		}
		if (speed <= points.front().Speed)
		{
			return points.front().Fraction;
		}
		for (size_t i = 1; i < points.size(); i++)
		{
			if (speed <= points[i].Speed)
			{
				const float t = (speed - points[i - 1].Speed) / (points[i].Speed - points[i - 1].Speed);
				return points[i - 1].Fraction + t * (points[i].Fraction - points[i - 1].Fraction);
			}
		}
		return points.back().Fraction;
	}

	void archive(BinaryArchive& ar, VehicleSettingsData& v)
	{
		if (!ar.version(VEHICLE_SETTINGS_VERSION))
		{
			return;
		}
		ar.pod_vector(v.TorqueCurve);
		ar.value(v.MinRpm);
		ar.value(v.MaxRpm);
		ar.value(v.EngineInertia);
		ar.value(v.EngineDamping);
		ar.value(v.AutomaticGearbox);
		ar.pod_vector(v.Gears);
		ar.pod_vector(v.ReverseGears);
		ar.value(v.ShiftUpRpm);
		ar.value(v.ShiftDownRpm);
		ar.value(v.ShiftTime);
		ar.value(v.ClutchStrength);
		ar.pod_vector(v.Differentials);
		ar.pod_vector(v.AntiRollStiffness);
		ar.value(v.FrontTire);
		ar.value(v.RearTire);
		ar.value(v.DragArea);
		ar.value(v.DownforceArea);
		ar.value(v.DownforceFront);
		ar.value(v.MaxBrakeTorque);
		ar.value(v.HandbrakeTorque);
		ar.value(v.BrakeBiasFront);
		ar.value(v.TractionControl);
		ar.value(v.Abs);
		ar.value(v.Stability);
		ar.value(v.SteeringHelp);
		ar.value(v.SteerSpeed);
		ar.pod_vector(v.SteerLimit);
		if (ar.reading())
		{
			ar.expect_end();
			if (ar.ok() && !vehicle_settings_valid(v))
			{
				ar.fail();
			}
		}
	}
}
