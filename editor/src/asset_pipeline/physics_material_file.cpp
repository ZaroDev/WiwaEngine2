#include <editor_common.h>
#include "physics_material_file.h"

#include <cmath>

namespace wiwa
{
	namespace
	{
		using Json = nlohmann::json;

		bool read_number(const Json& root, const char* key, float& value, std::string& error)
		{
			const auto found = root.find(key);
			if (found == root.end())
			{
				return true;
			}
			if (!found->is_number())
			{
				error = std::string("\"") + key + "\" must be a number";
				return false;
			}
			const double number = found->get<double>();
			const float narrowed = static_cast<float>(number);
			if (!std::isfinite(number) || !std::isfinite(narrowed) || number < 0.0)
			{
				error = std::string("\"") + key + "\" must be a number from 0 to 3.4e38";
				return false;
			}
			value = narrowed;
			return true;
		}

		bool read_mode(const Json& root, const char* key, CombineMode& mode, std::string& error)
		{
			const auto found = root.find(key);
			if (found == root.end())
			{
				return true;
			}
			if (found->is_string())
			{
				const std::string text = found->get<std::string>();
				for (const CombineMode candidate : { CombineMode::Average, CombineMode::Min, CombineMode::Max, CombineMode::Multiply })
				{
					if (text == combine_mode_name(candidate))
					{
						mode = candidate;
						return true;
					}
				}
			}
			error = std::string("\"") + key + "\" must be \"Average\", \"Min\", \"Max\" or \"Multiply\"";
			return false;
		}
	}

	const char* combine_mode_name(CombineMode mode)
	{
		switch (mode)
		{
		case CombineMode::Min: return "Min";
		case CombineMode::Max: return "Max";
		case CombineMode::Multiply: return "Multiply";
		default: return "Average";
		}
	}

	bool physics_material_from_text(const std::string& text, PhysicsMaterialData& out, std::string& error)
	{
		Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		PhysicsMaterialData data = default_physics_material();
		if (!read_number(root, "friction", data.Friction, error) || !read_number(root, "restitution", data.Restitution, error) ||
			!read_number(root, "density", data.Density, error) || !read_mode(root, "frictionCombine", data.FrictionCombine, error) ||
			!read_mode(root, "restitutionCombine", data.RestitutionCombine, error))
		{
			return false;
		}
		out = data;
		return true;
	}

	Json physics_material_to_json(const PhysicsMaterialData& data)
	{
		Json root = Json::object();
		root["friction"] = data.Friction;
		root["restitution"] = data.Restitution;
		root["density"] = data.Density;
		root["frictionCombine"] = combine_mode_name(data.FrictionCombine);
		root["restitutionCombine"] = combine_mode_name(data.RestitutionCombine);
		return root;
	}
}
