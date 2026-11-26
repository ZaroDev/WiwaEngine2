#include <editor_common.h>
#include "vehicle_file.h"

#include <cmath>

namespace wiwa
{
	namespace
	{
		using Json = nlohmann::json;

		// Reads JSON values into fields; the first problem is kept in Error with the key's path, and every later read does nothing.
		struct Reader
		{
			std::string Error{};

			bool ok() const { return Error.empty(); }

			void fail(const std::string& path, const char* what)
			{
				if (Error.empty())
				{
					Error = "\"" + path + "\" " + what;
				}
			}

			const Json* find(const Json& object, const char* key)
			{
				if (!ok() || !object.is_object())
				{
					return nullptr;
				}
				const auto found = object.find(key);
				return found == object.end() ? nullptr : &*found;
			}

			void number(const Json& object, const char* key, float& value, const std::string& path)
			{
				if (const Json* found = find(object, key))
				{
					if (!found->is_number() || !std::isfinite(found->get<double>()) || !std::isfinite(static_cast<float>(found->get<double>())))
					{
						fail(path + "." + key, "must be a finite number");
						return;
					}
					value = static_cast<float>(found->get<double>());
				}
			}

			void integer(const Json& object, const char* key, i32& value, const std::string& path)
			{
				if (const Json* found = find(object, key))
				{
					if (!found->is_number_integer())
					{
						fail(path + "." + key, "must be a whole number");
						return;
					}
					value = found->get<i32>();
				}
			}

			void boolean(const Json& object, const char* key, bool& value, const std::string& path)
			{
				if (const Json* found = find(object, key))
				{
					if (!found->is_boolean())
					{
						fail(path + "." + key, "must be true or false");
						return;
					}
					value = found->get<bool>();
				}
			}

			// The object at key (null when missing or after a failure; a value that is not an object fails).
			const Json* object(const Json& parent, const char* key, const std::string& path)
			{
				const Json* found = find(parent, key);
				if (found != nullptr && !found->is_object())
				{
					fail(path + key, "must be an object");
					return nullptr;
				}
				return found;
			}

			void numbers(const Json& parent, const char* key, std::vector<float>& values, const std::string& path)
			{
				if (const Json* found = find(parent, key))
				{
					if (!found->is_array())
					{
						fail(path + "." + key, "must be an array of numbers");
						return;
					}
					std::vector<float> read;
					for (const Json& item : *found)
					{
						if (!item.is_number() || !std::isfinite(static_cast<float>(item.get<double>())))
						{
							fail(path + "." + key, "must be an array of numbers");
							return;
						}
						read.push_back(static_cast<float>(item.get<double>()));
					}
					values = std::move(read);
				}
			}

			// An array of [a, b] pairs.
			bool pairs(const Json& parent, const char* key, std::vector<std::pair<float, float>>& values, const std::string& path)
			{
				const Json* found = find(parent, key);
				if (found == nullptr)
				{
					return false;
				}
				if (!found->is_array())
				{
					fail(path + "." + key, "must be an array of [number, number] pairs");
					return false;
				}
				for (const Json& item : *found)
				{
					if (!item.is_array() || item.size() != 2 || !item[0].is_number() || !item[1].is_number())
					{
						fail(path + "." + key, "must be an array of [number, number] pairs");
						return false;
					}
					values.emplace_back(static_cast<float>(item[0].get<double>()), static_cast<float>(item[1].get<double>()));
				}
				return true;
			}

			void curve(const Json& parent, const char* key, TireCurve& c, const std::string& path)
			{
				if (const Json* o = object(parent, key, path + "."))
				{
					const std::string here = path + "." + key;
					number(*o, "B", c.B, here);
					number(*o, "C", c.C, here);
					number(*o, "D", c.D, here);
					number(*o, "E", c.E, here);
				}
			}

			void tire(const Json& root, const char* key, TireModel& t)
			{
				if (const Json* o = object(root, key, ""))
				{
					curve(*o, "longitudinal", t.Longitudinal, key);
					curve(*o, "lateral", t.Lateral, key);
					number(*o, "nominalLoad", t.NominalLoad, key);
					number(*o, "loadSensitivity", t.LoadSensitivity, key);
					number(*o, "pneumaticTrail", t.PneumaticTrail, key);
				}
			}
		};

		Json curve_json(const TireCurve& c) { return Json{ { "B", c.B }, { "C", c.C }, { "D", c.D }, { "E", c.E } }; }

		Json tire_json(const TireModel& t)
		{
			return Json{ { "longitudinal", curve_json(t.Longitudinal) }, { "lateral", curve_json(t.Lateral) }, { "nominalLoad", t.NominalLoad },
				{ "loadSensitivity", t.LoadSensitivity }, { "pneumaticTrail", t.PneumaticTrail } };
		}
	}

	bool vehicle_from_text(const std::string& text, VehicleSettingsData& out, std::string& error)
	{
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}
		const auto version = root.find("version");
		if (version != root.end() && (!version->is_number_integer() || version->get<i64>() != 1))
		{
			error = "\"version\" must be 1";
			return false;
		}

		VehicleSettingsData d = default_vehicle_settings();
		Reader r;
		if (const Json* engine = r.object(root, "engine", ""))
		{
			std::vector<std::pair<float, float>> curve;
			if (r.pairs(*engine, "torqueCurve", curve, "engine"))
			{
				d.TorqueCurve.clear();
				for (const auto& [rpm, torque] : curve)
				{
					d.TorqueCurve.push_back({ rpm, torque });
				}
			}
			r.number(*engine, "minRpm", d.MinRpm, "engine");
			r.number(*engine, "maxRpm", d.MaxRpm, "engine");
			r.number(*engine, "inertia", d.EngineInertia, "engine");
			r.number(*engine, "damping", d.EngineDamping, "engine");
		}
		if (const Json* t = r.object(root, "transmission", ""))
		{
			r.boolean(*t, "automatic", d.AutomaticGearbox, "transmission");
			r.numbers(*t, "gears", d.Gears, "transmission");
			r.numbers(*t, "reverseGears", d.ReverseGears, "transmission");
			r.number(*t, "shiftUpRpm", d.ShiftUpRpm, "transmission");
			r.number(*t, "shiftDownRpm", d.ShiftDownRpm, "transmission");
			r.number(*t, "shiftTime", d.ShiftTime, "transmission");
			r.number(*t, "clutchStrength", d.ClutchStrength, "transmission");
		}
		if (const Json* diffs = r.find(root, "differentials"))
		{
			if (!diffs->is_array())
			{
				r.fail("differentials", "must be an array of objects");
			}
			else
			{
				d.Differentials.clear();
				for (const Json& item : *diffs)
				{
					VehicleDifferentialData diff;
					if (!item.is_object())
					{
						r.fail("differentials", "must be an array of objects");
						break;
					}
					r.integer(item, "axle", diff.Axle, "differentials");
					r.number(item, "ratio", diff.Ratio, "differentials");
					r.number(item, "limitedSlipRatio", diff.LimitedSlipRatio, "differentials");
					r.number(item, "engineTorqueRatio", diff.EngineTorqueRatio, "differentials");
					d.Differentials.push_back(diff);
				}
			}
		}
		r.numbers(root, "antiRoll", d.AntiRollStiffness, "");
		r.tire(root, "frontTire", d.FrontTire);
		r.tire(root, "rearTire", d.RearTire);
		if (const Json* aero = r.object(root, "aero", ""))
		{
			r.number(*aero, "dragArea", d.DragArea, "aero");
			r.number(*aero, "downforceArea", d.DownforceArea, "aero");
			r.number(*aero, "downforceFront", d.DownforceFront, "aero");
		}
		if (const Json* brakes = r.object(root, "brakes", ""))
		{
			r.number(*brakes, "maxTorque", d.MaxBrakeTorque, "brakes");
			r.number(*brakes, "handbrakeTorque", d.HandbrakeTorque, "brakes");
			r.number(*brakes, "biasFront", d.BrakeBiasFront, "brakes");
		}
		if (const Json* assists = r.object(root, "assists", ""))
		{
			r.number(*assists, "tractionControl", d.TractionControl, "assists");
			r.number(*assists, "abs", d.Abs, "assists");
			r.number(*assists, "stability", d.Stability, "assists");
			r.number(*assists, "steeringHelp", d.SteeringHelp, "assists");
		}
		if (const Json* steering = r.object(root, "steering", ""))
		{
			r.number(*steering, "speed", d.SteerSpeed, "steering");
			std::vector<std::pair<float, float>> limit;
			if (r.pairs(*steering, "limit", limit, "steering"))
			{
				d.SteerLimit.clear();
				for (const auto& [speed, fraction] : limit)
				{
					d.SteerLimit.push_back({ speed, fraction });
				}
			}
		}
		if (!r.ok())
		{
			error = r.Error;
			return false;
		}
		std::string why;
		if (!vehicle_settings_valid(d, &why))
		{
			error = why;
			return false;
		}
		out = std::move(d);
		return true;
	}

	nlohmann::json vehicle_to_json(const VehicleSettingsData& d)
	{
		Json curve = Json::array();
		for (const TorquePoint& p : d.TorqueCurve)
		{
			curve.push_back(Json::array({ p.Rpm, p.Torque }));
		}
		Json diffs = Json::array();
		for (const VehicleDifferentialData& diff : d.Differentials)
		{
			diffs.push_back(Json{ { "axle", diff.Axle }, { "ratio", diff.Ratio }, { "limitedSlipRatio", diff.LimitedSlipRatio }, { "engineTorqueRatio", diff.EngineTorqueRatio } });
		}
		Json limit = Json::array();
		for (const SteerLimitPoint& p : d.SteerLimit)
		{
			limit.push_back(Json::array({ p.Speed, p.Fraction }));
		}

		Json root = Json::object();
		root["version"] = 1;
		root["engine"] = Json{ { "torqueCurve", curve }, { "minRpm", d.MinRpm }, { "maxRpm", d.MaxRpm }, { "inertia", d.EngineInertia }, { "damping", d.EngineDamping } };
		root["transmission"] = Json{ { "automatic", d.AutomaticGearbox }, { "gears", d.Gears }, { "reverseGears", d.ReverseGears }, { "shiftUpRpm", d.ShiftUpRpm },
			{ "shiftDownRpm", d.ShiftDownRpm }, { "shiftTime", d.ShiftTime }, { "clutchStrength", d.ClutchStrength } };
		root["differentials"] = diffs;
		root["antiRoll"] = d.AntiRollStiffness;
		root["frontTire"] = tire_json(d.FrontTire);
		root["rearTire"] = tire_json(d.RearTire);
		root["aero"] = Json{ { "dragArea", d.DragArea }, { "downforceArea", d.DownforceArea }, { "downforceFront", d.DownforceFront } };
		root["brakes"] = Json{ { "maxTorque", d.MaxBrakeTorque }, { "handbrakeTorque", d.HandbrakeTorque }, { "biasFront", d.BrakeBiasFront } };
		root["assists"] = Json{ { "tractionControl", d.TractionControl }, { "abs", d.Abs }, { "stability", d.Stability }, { "steeringHelp", d.SteeringHelp } };
		root["steering"] = Json{ { "speed", d.SteerSpeed }, { "limit", limit } };
		return root;
	}
}
