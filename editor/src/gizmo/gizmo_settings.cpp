#include <editor_common.h>
#include "gizmo_settings.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace wiwa
{
	namespace
	{
		constexpr float MIN_WIDTH = 1.0f;
		constexpr float MAX_WIDTH = 8.0f;

		float clamped(float value, float fallback, float low, float high)
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}
	}

	GizmoSettings GizmoSettings::load(const std::filesystem::path& path)
	{
		GizmoSettings settings;
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return settings;
		}

		const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			return settings;
		}

		auto flag = [](const nlohmann::json& object, const char* key, bool& out)
		{
			if (object.contains(key) && object[key].is_boolean())
			{
				out = object[key].get<bool>();
			}
		};
		flag(json, "lights", settings.Lights);
		flag(json, "cameras", settings.Cameras);
		flag(json, "bounds", settings.Bounds);
		flag(json, "grid", settings.Grid);
		flag(json, "axes", settings.Axes);
		flag(json, "transform", settings.TransformHandles);
		flag(json, "startMaximized", settings.StartMaximized);

		if (json.contains("outline") && json["outline"].is_object())
		{
			const nlohmann::json& outline = json["outline"];
			const SelectionOutlineStyle defaults{};
			flag(outline, "enabled", settings.Outline.Enabled);
			if (outline.contains("width") && outline["width"].is_number())
			{
				settings.Outline.WidthPx = clamped(outline["width"].get<float>(), defaults.WidthPx, MIN_WIDTH, MAX_WIDTH);
			}
			if (outline.contains("occludedAlpha") && outline["occludedAlpha"].is_number())
			{
				settings.Outline.OccludedAlpha = clamped(outline["occludedAlpha"].get<float>(), defaults.OccludedAlpha, 0.0f, 1.0f);
			}
			if (outline.contains("color") && outline["color"].is_array() && outline["color"].size() == 3)
			{
				const nlohmann::json& color = outline["color"];
				if (color[0].is_number() && color[1].is_number() && color[2].is_number())
				{
					settings.Outline.Color = glm::vec3(clamped(color[0].get<float>(), defaults.Color.r, 0.0f, 1.0f), clamped(color[1].get<float>(), defaults.Color.g, 0.0f, 1.0f),
						clamped(color[2].get<float>(), defaults.Color.b, 0.0f, 1.0f));
				}
			}
		}
		return settings;
	}

	bool GizmoSettings::save(const std::filesystem::path& path) const
	{
		nlohmann::ordered_json json = nlohmann::ordered_json::object();
		json["lights"] = Lights;
		json["cameras"] = Cameras;
		json["bounds"] = Bounds;
		json["grid"] = Grid;
		json["axes"] = Axes;
		json["transform"] = TransformHandles;
		json["startMaximized"] = StartMaximized;
		nlohmann::ordered_json outline = nlohmann::ordered_json::object();
		outline["enabled"] = Outline.Enabled;
		outline["width"] = Outline.WidthPx;
		outline["occludedAlpha"] = Outline.OccludedAlpha;
		outline["color"] = nlohmann::ordered_json::array({ Outline.Color.r, Outline.Color.g, Outline.Color.b });
		json["outline"] = outline;

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}
		file << json.dump(2) << "\n";
		return static_cast<bool>(file);
	}
}
