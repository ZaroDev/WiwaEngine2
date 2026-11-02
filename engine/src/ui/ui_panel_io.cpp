#include <pch.h>
#include "ui_panel_io.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace wiwa
{
	namespace
	{
		constexpr u32 PANEL_FORMAT_VERSION = 1;

		UiJson value_to_json(const UiField& field, const FieldValue& value)
		{
			struct Visitor
			{
				const UiField& Field;

				UiJson operator()(bool v) const { return v; }
				UiJson operator()(i32 v) const
				{
					if (Field.Type == FieldType::Enum && v >= 0 && static_cast<size_t>(v) < Field.Meta.EnumNames.size())
					{
						return Field.Meta.EnumNames[static_cast<size_t>(v)];
					}
					return v;
				}
				UiJson operator()(float v) const { return v; }
				UiJson operator()(const glm::vec2& v) const { return UiJson::array({ v.x, v.y }); }
				UiJson operator()(const glm::vec3& v) const { return UiJson::array({ v.x, v.y, v.z }); }
				UiJson operator()(const glm::vec4& v) const { return UiJson::array({ v.x, v.y, v.z, v.w }); }
				UiJson operator()(const glm::quat& v) const { return UiJson::array({ v.w, v.x, v.y, v.z }); }
				UiJson operator()(const std::string& v) const { return v; }
				UiJson operator()(const AssetRef& v) const
				{
					if (!v.valid())
					{
						return nullptr;
					}
					UiJson json = UiJson::object();
					json["guid"] = v.Guid.to_string();
					if (v.LocalId != 0)
					{
						json["local"] = v.LocalId;
					}
					return json;
				}
				UiJson operator()(const std::vector<AssetRef>& v) const
				{
					UiJson list = UiJson::array();
					for (const AssetRef& ref : v)
					{
						list.push_back((*this)(ref));
					}
					return list;
				}
				UiJson operator()(const EntityGuid& v) const
				{
					return v.valid() ? UiJson(v.to_string()) : UiJson(nullptr);
				}
			};
			return std::visit(Visitor{ field }, value);
		}

		bool number_array(const UiJson& json, size_t count, float* out)
		{
			if (!json.is_array() || json.size() != count)
			{
				return false;
			}
			for (size_t i = 0; i < count; i++)
			{
				if (!json[i].is_number())
				{
					return false;
				}
				out[i] = json[i].get<float>();
			}
			return true;
		}

		// Returns the reason a value does not fit the field, empty if it does (and out holds it).
		std::string value_from_json(const UiField& field, const UiJson& json, FieldValue& out)
		{
			switch (field.Type)
			{
			case FieldType::Bool:
				if (!json.is_boolean())
				{
					return "not a boolean";
				}
				out = json.get<bool>();
				return {};
			case FieldType::Int:
				if (!json.is_number_integer())
				{
					return "not an integer";
				}
				out = json.get<i32>();
				return {};
			case FieldType::Float:
				if (!json.is_number())
				{
					return "not a number";
				}
				out = json.get<float>();
				return {};
			case FieldType::Vec2:
			{
				float v[2];
				if (!number_array(json, 2, v))
				{
					return "not an array of 2 numbers";
				}
				out = glm::vec2(v[0], v[1]);
				return {};
			}
			case FieldType::Vec3:
			{
				float v[3];
				if (!number_array(json, 3, v))
				{
					return "not an array of 3 numbers";
				}
				out = glm::vec3(v[0], v[1], v[2]);
				return {};
			}
			case FieldType::Vec4:
			case FieldType::Color:
			{
				float v[4];
				if (!number_array(json, 4, v))
				{
					return "not an array of 4 numbers";
				}
				out = glm::vec4(v[0], v[1], v[2], v[3]);
				return {};
			}
			case FieldType::String:
				if (!json.is_string())
				{
					return "not a string";
				}
				out = json.get<std::string>();
				return {};
			case FieldType::Enum:
			{
				if (json.is_string())
				{
					const std::string& name = json.get_ref<const std::string&>();
					for (size_t i = 0; i < field.Meta.EnumNames.size(); i++)
					{
						if (field.Meta.EnumNames[i] == name)
						{
							out = static_cast<i32>(i);
							return {};
						}
					}
					return "not one of the names of the enum";
				}
				if (json.is_number_integer() && json.get<i32>() >= 0 && static_cast<size_t>(json.get<i32>()) < field.Meta.EnumNames.size())
				{
					out = json.get<i32>();
					return {};
				}
				return "not a name of the enum";
			}
			case FieldType::Asset:
			{
				if (json.is_null())
				{
					out = AssetRef{};
					return {};
				}
				if (!json.is_object() || !json.contains("guid") || !json["guid"].is_string())
				{
					return "not an AssetRef";
				}
				const std::optional<AssetGuid> guid = AssetGuid::from_string(json["guid"].get_ref<const std::string&>());
				if (!guid)
				{
					return "not an AssetRef (bad guid)";
				}
				u64 local = 0;
				if (json.contains("local"))
				{
					if (!json["local"].is_number_unsigned())
					{
						return "not an AssetRef (bad local id)";
					}
					local = json["local"].get<u64>();
				}
				out = AssetRef{ *guid, local };
				return {};
			}
			case FieldType::Entity:
			{
				if (json.is_null())
				{
					out = EntityGuid{};
					return {};
				}
				if (!json.is_string())
				{
					return "not an entity guid";
				}
				const std::optional<EntityGuid> guid = EntityGuid::from_string(json.get_ref<const std::string&>());
				if (!guid)
				{
					return "not an entity guid";
				}
				out = *guid;
				return {};
			}
			default:
				return "a field type that panels do not hold";
			}
		}

		std::string where(size_t index, const std::string& key)
		{
			return "widgets[" + std::to_string(index) + "]" + (key.empty() ? "" : "." + key);
		}

		// Validates the reading of one widget; returns the reason, empty if it is fine.
		std::string widget_from_json(const UiJson& json, size_t index, UiWidget& widget)
		{
			if (!json.is_object())
			{
				return where(index, "") + ": not an object";
			}

			if (!json.contains("id") || !json["id"].is_number_unsigned() || json["id"].get<u64>() == 0 || json["id"].get<u64>() > 0xFFFFFFFFull)
			{
				return where(index, "id") + ": missing, or not a number above 0";
			}
			widget.Id = json["id"].get<UiId>();

			if (json.contains("parent"))
			{
				if (!json["parent"].is_number_unsigned() || json["parent"].get<u64>() > 0xFFFFFFFFull)
				{
					return where(index, "parent") + ": not a widget id";
				}
				widget.Parent = json["parent"].get<UiId>();
			}

			if (!json.contains("kind") || !json["kind"].is_string())
			{
				return where(index, "kind") + ": missing, or not a string";
			}
			const std::optional<WidgetKind> kind = widget_kind_from_name(json["kind"].get_ref<const std::string&>());
			if (!kind)
			{
				return where(index, "kind") + ": unknown widget kind \"" + json["kind"].get_ref<const std::string&>() + "\"";
			}
			widget.Kind = *kind;

			for (const UiField& field : ui_fields())
			{
				if (!json.contains(field.Name))
				{
					continue;
				}

				FieldValue value;
				const std::string problem = value_from_json(field, json[field.Name], value);
				if (!problem.empty() || !field.Set(widget, value))
				{
					return where(index, field.Name) + ": " + (problem.empty() ? "a value the field does not accept" : problem);
				}
			}
			return {};
		}
	}

	UiJson ui_panel_to_json(const UiPanelData& panel)
	{
		UiJson json = UiJson::object();
		json["version"] = PANEL_FORMAT_VERSION;
		json["referenceResolution"] = UiJson::array({ panel.ReferenceResolution.x, panel.ReferenceResolution.y });
		json["match"] = panel.MatchWidthOrHeight;
		json["nextId"] = panel.NextId;

		UiJson widgets = UiJson::array();
		for (const UiWidget& widget : panel.Widgets)
		{
			UiJson entry = UiJson::object();
			entry["id"] = widget.Id;
			if (widget.Parent != 0)
			{
				entry["parent"] = widget.Parent;
			}
			entry["kind"] = widget_kind_name(widget.Kind);

			for (const UiField& field : ui_fields())
			{
				const FieldValue value = field.Get(widget);
				if (value != field.Default)
				{
					entry[field.Name] = value_to_json(field, value);
				}
			}
			widgets.push_back(std::move(entry));
		}
		json["widgets"] = std::move(widgets);
		return json;
	}

	std::optional<UiPanelData> ui_panel_from_json(const UiJson& json, std::string& error)
	{
		error.clear();
		if (!json.is_object())
		{
			error = "the panel is not an object";
			return std::nullopt;
		}

		UiPanelData panel;
		if (json.contains("referenceResolution"))
		{
			float v[2];
			if (!number_array(json["referenceResolution"], 2, v) || !(v[0] > 0.0f) || !(v[1] > 0.0f) || !std::isfinite(v[0]) || !std::isfinite(v[1]))
			{
				error = "referenceResolution: not an array of 2 numbers above 0";
				return std::nullopt;
			}
			panel.ReferenceResolution = { v[0], v[1] };
		}
		if (json.contains("match"))
		{
			if (!json["match"].is_number())
			{
				error = "match: not a number";
				return std::nullopt;
			}
			panel.MatchWidthOrHeight = std::clamp(json["match"].get<float>(), 0.0f, 1.0f);
		}

		if (json.contains("widgets"))
		{
			const UiJson& widgets = json["widgets"];
			if (!widgets.is_array())
			{
				error = "widgets: not an array";
				return std::nullopt;
			}
			if (widgets.size() > UI_MAX_WIDGETS)
			{
				error = "widgets: more than " + std::to_string(UI_MAX_WIDGETS);
				return std::nullopt;
			}

			panel.Widgets.reserve(widgets.size());
			for (size_t i = 0; i < widgets.size(); i++)
			{
				UiWidget widget;
				error = widget_from_json(widgets[i], i, widget);
				if (!error.empty())
				{
					return std::nullopt;
				}
				panel.Widgets.push_back(std::move(widget));
			}
		}

		std::unordered_map<UiId, UiId> parentOf;
		UiId highest = 0;
		for (size_t i = 0; i < panel.Widgets.size(); i++)
		{
			const UiWidget& widget = panel.Widgets[i];
			if (!parentOf.emplace(widget.Id, widget.Parent).second)
			{
				error = where(i, "id") + ": id " + std::to_string(widget.Id) + " is used twice";
				return std::nullopt;
			}
			highest = std::max(highest, widget.Id);
		}

		for (size_t i = 0; i < panel.Widgets.size(); i++)
		{
			const UiWidget& widget = panel.Widgets[i];
			if (widget.Parent != 0 && !parentOf.contains(widget.Parent))
			{
				error = where(i, "parent") + ": there is no widget " + std::to_string(widget.Parent);
				return std::nullopt;
			}

			// Walking up must reach a root in at most as many steps as there are widgets.
			UiId current = widget.Id;
			size_t steps = 0;
			while (current != 0 && steps <= panel.Widgets.size())
			{
				current = parentOf[current];
				steps++;
			}
			if (current != 0)
			{
				error = where(i, "parent") + ": the parents form a loop";
				return std::nullopt;
			}
		}

		panel.NextId = highest + 1;
		if (json.contains("nextId"))
		{
			if (!json["nextId"].is_number_unsigned() || json["nextId"].get<u64>() > 0xFFFFFFFFull)
			{
				error = "nextId: not a number";
				return std::nullopt;
			}
			panel.NextId = std::max(panel.NextId, json["nextId"].get<UiId>());
		}
		return panel;
	}

	std::optional<UiPanelData> ui_panel_from_text(const std::string& text, std::string& error)
	{
		const UiJson json = UiJson::parse(text, nullptr, false);
		if (json.is_discarded())
		{
			error = "not valid JSON";
			return std::nullopt;
		}
		return ui_panel_from_json(json, error);
	}
}
