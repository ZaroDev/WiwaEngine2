#include <pch.h>
#include "tire_model.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		bool curve_valid(const TireCurve& c)
		{
			return std::isfinite(c.B) && std::isfinite(c.C) && std::isfinite(c.D) && std::isfinite(c.E) && c.B > 0.0f && c.C > 0.0f && c.D > 0.0f;
		}

		// The slips normalised by the curves' peaks, and the length of that vector.
		struct NormalisedSlip
		{
			float PeakLong = 0.0f;
			float PeakLat = 0.0f;
			float Sx = 0.0f;
			float Sa = 0.0f;
			float Rho = 0.0f;
		};

		NormalisedSlip normalise(const TireModel& tire, float slipRatio, float slipAngle)
		{
			NormalisedSlip n;
			n.PeakLong = magic_formula_peak_x(tire.Longitudinal);
			n.PeakLat = magic_formula_peak_x(tire.Lateral);
			n.Sx = slipRatio / n.PeakLong;
			n.Sa = slipAngle / n.PeakLat;
			n.Rho = std::hypot(n.Sx, n.Sa);
			return n;
		}
	}

	float magic_formula(const TireCurve& c, float x)
	{
		const float bx = c.B * x;
		return c.D * std::sin(c.C * std::atan(bx - c.E * (bx - std::atan(bx))));
	}

	float magic_formula_peak_x(const TireCurve& curve)
	{
		// Sampled, then refined around the best sample; the curves are smooth with one first maximum.
		float best = 1e-3f;
		float bestY = magic_formula(curve, best);
		for (float x = 2e-3f; x <= 1.0f; x += 1e-3f)
		{
			const float y = magic_formula(curve, x);
			if (y > bestY)
			{
				best = x;
				bestY = y;
			}
			else if (y < bestY - 1e-4f)
			{
				break; // past the first maximum
			}
		}
		float lo = std::max(best - 1e-3f, 1e-5f);
		float hi = best + 1e-3f;
		for (int i = 0; i < 40; i++)
		{
			const float a = lo + (hi - lo) / 3.0f;
			const float b = hi - (hi - lo) / 3.0f;
			if (magic_formula(curve, a) < magic_formula(curve, b))
			{
				lo = a;
			}
			else
			{
				hi = b;
			}
		}
		return 0.5f * (lo + hi);
	}

	float tire_load_scale(const TireModel& tire, float load)
	{
		return std::clamp(1.0f - tire.LoadSensitivity * (load - tire.NominalLoad) / tire.NominalLoad, 0.5f, 1.5f);
	}

	TireForces tire_forces(const TireModel& tire, float load, float slipRatio, float slipAngle, float grip)
	{
		TireForces out;
		if (!(load > 0.0f))
		{
			return out;
		}
		const NormalisedSlip n = normalise(tire, slipRatio, slipAngle);
		if (!(n.Rho > 1e-6f))
		{
			return out;
		}
		const float base = load * grip * tire_load_scale(tire, load);
		out.Longitudinal = base * magic_formula(tire.Longitudinal, n.Rho * n.PeakLong) * n.Sx / n.Rho;
		out.Lateral = base * magic_formula(tire.Lateral, n.Rho * n.PeakLat) * n.Sa / n.Rho;
		out.AligningTorque = -out.Lateral * tire.PneumaticTrail * std::max(0.0f, 1.0f - std::abs(slipAngle) / n.PeakLat);
		return out;
	}

	TireLimits tire_force_limits(const TireModel& tire, float load, float slipRatio, float slipAngle, float grip)
	{
		TireLimits out;
		if (!(load > 0.0f))
		{
			return out;
		}
		const float base = load * grip * tire_load_scale(tire, load);
		const float peakLong = base * tire.Longitudinal.D;
		const float peakLat = base * tire.Lateral.D;
		const NormalisedSlip n = normalise(tire, slipRatio, slipAngle);
		if (!(n.Rho > 1.0f))
		{
			const float sa = std::min(std::abs(n.Sa), 1.0f);
			const float sx = std::min(std::abs(n.Sx), 1.0f);
			out.Longitudinal = peakLong * std::sqrt(std::max(0.0f, 1.0f - sa * sa));
			out.Lateral = peakLat * std::sqrt(std::max(0.0f, 1.0f - sx * sx));
			return out;
		}
		const float fLong = magic_formula(tire.Longitudinal, n.Rho * n.PeakLong) / tire.Longitudinal.D;
		const float fLat = magic_formula(tire.Lateral, n.Rho * n.PeakLat) / tire.Lateral.D;
		out.Longitudinal = peakLong * std::max(fLong, 0.0f) * std::abs(n.Sx) / n.Rho;
		out.Lateral = peakLat * std::max(fLat, 0.0f) * std::abs(n.Sa) / n.Rho;
		return out;
	}

	bool tire_model_valid(const TireModel& tire)
	{
		return curve_valid(tire.Longitudinal) && curve_valid(tire.Lateral) && std::isfinite(tire.NominalLoad) && tire.NominalLoad > 0.0f
			&& std::isfinite(tire.LoadSensitivity) && tire.LoadSensitivity >= 0.0f && tire.LoadSensitivity < 1.0f
			&& std::isfinite(tire.PneumaticTrail) && tire.PneumaticTrail >= 0.0f;
	}
}
