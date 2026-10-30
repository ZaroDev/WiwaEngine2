#include <editor_common.h>
#include "scene_json.h"

#include <charconv>

#include <scene/reflection/component_registry.h>

namespace wiwa
{
	namespace
	{
		// ---- writing ----

		SceneJson ref_to_json(const AssetRef& ref)
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			SceneJson json = SceneJson::object();
			json["guid"] = ref.Guid.to_string();
			json["localId"] = ref.LocalId;
			return json;
		}

		SceneJson entity_ref_to_json(const EntityGuid& id)
		{
			return id.valid() ? SceneJson(id.to_string()) : SceneJson(nullptr);
		}

		const char* alternative_name(const FieldValue& value)
		{
			static const char* names[] = { "bool", "int", "float", "vec2", "vec3", "vec4", "quat", "string", "asset", "assetlist", "entity" };
			return names[value.index()];
		}

		// The JSON of a value from its alternative alone (the type is the one in the variant).
		SceneJson bare_value_to_json(const FieldValue& value)
		{
			struct Visitor
			{
				SceneJson operator()(bool v) const { return v; }
				SceneJson operator()(i32 v) const { return v; }
				SceneJson operator()(float v) const { return static_cast<double>(v); }
				SceneJson operator()(const glm::vec2& v) const { return SceneJson::array({ static_cast<double>(v.x), static_cast<double>(v.y) }); }
				SceneJson operator()(const glm::vec3& v) const { return SceneJson::array({ static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z) }); }
				SceneJson operator()(const glm::vec4& v) const { return SceneJson::array({ static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z), static_cast<double>(v.w) }); }
				SceneJson operator()(const glm::quat& v) const { return SceneJson::array({ static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z), static_cast<double>(v.w) }); }
				SceneJson operator()(const std::string& v) const { return v; }
				SceneJson operator()(const AssetRef& v) const { return ref_to_json(v); }
				SceneJson operator()(const std::vector<AssetRef>& v) const
				{
					SceneJson list = SceneJson::array();
					for (const AssetRef& ref : v)
					{
						list.push_back(ref_to_json(ref));
					}
					return list;
				}
				SceneJson operator()(const EntityGuid& v) const { return entity_ref_to_json(v); }
			};
			return std::visit(Visitor{}, value);
		}

		SceneJson tagged_to_json(const FieldValue& value)
		{
			SceneJson json = SceneJson::object();
			json["$type"] = alternative_name(value);
			json["value"] = bare_value_to_json(value);
			return json;
		}

		// A field the engine describes is written plainly (an enum by name); anything else is tagged.
		SceneJson value_to_json(const FieldValue& value, const FieldDescriptor* field)
		{
			if (field == nullptr || !value_matches_type(field->Type, value))
			{
				return tagged_to_json(value);
			}

			if (field->Type == FieldType::Enum && !field->Meta.EnumNames.empty())
			{
				const i32 index = std::get<i32>(value);
				if (index >= 0 && static_cast<size_t>(index) < field->Meta.EnumNames.size())
				{
					return field->Meta.EnumNames[static_cast<size_t>(index)];
				}
				return tagged_to_json(value);
			}
			return bare_value_to_json(value);
		}

		SceneJson component_fields_to_json(const ComponentRecord& component, const std::optional<ComponentSchema>& schema)
		{
			SceneJson fields = SceneJson::object();
			for (const FieldRecord& field : component.Fields)
			{
				fields[field.Name] = value_to_json(field.Value, schema ? schema->find_field(field.Name) : nullptr);
			}
			return fields;
		}

		std::string node_to_string(u64 node)
		{
			return fmt::format("{:016x}", node);
		}

		SceneJson entity_to_json(const EntityRecord& entity, bool withParent, bool withOverrides);

		SceneJson override_to_json(const PrefabOverride& override)
		{
			static const char* kinds[] = { "SetField", "AddComponent", "RemoveComponent", "AddChild", "RemoveNode" };

			SceneJson json = SceneJson::object();
			json["kind"] = kinds[static_cast<u32>(override.Kind)];
			json["node"] = node_to_string(override.NodeId);
			switch (override.Kind)
			{
			case OverrideKind::SetField:
			{
				json["component"] = override.Component;
				json["field"] = override.Field;
				const std::optional<ComponentSchema> schema = ComponentRegistry::get().schema(override.Component);
				json["value"] = value_to_json(override.Value, schema ? schema->find_field(override.Field) : nullptr);
				break;
			}
			case OverrideKind::AddComponent:
				json["component"] = override.Added.Name;
				json["fields"] = component_fields_to_json(override.Added, ComponentRegistry::get().schema(override.Added.Name));
				break;
			case OverrideKind::RemoveComponent:
				json["component"] = override.Component;
				break;
			case OverrideKind::AddChild:
				json["child"] = entity_to_json(override.Child, false, false);
				break;
			case OverrideKind::RemoveNode:
				break;
			}
			return json;
		}

		SceneJson entity_to_json(const EntityRecord& entity, bool withParent, bool withOverrides)
		{
			SceneJson json = SceneJson::object();
			json["id"] = entity.Id.to_string();
			if (withParent && entity.Parent.valid())
			{
				json["parent"] = entity.Parent.to_string();
			}

			SceneJson components = SceneJson::object();
			for (const ComponentRecord& component : entity.Components)
			{
				components[component.Name] = component_fields_to_json(component, ComponentRegistry::get().schema(component.Name));
			}
			json["components"] = std::move(components);

			if (withOverrides && !entity.Overrides.empty())
			{
				SceneJson overrides = SceneJson::array();
				for (const PrefabOverride& override : entity.Overrides)
				{
					overrides.push_back(override_to_json(override));
				}
				json["overrides"] = std::move(overrides);
			}
			return json;
		}

		// ---- reading ----

		struct ReadError
		{
			std::string Message;
		};

		bool is_hex_guid(const SceneJson& json)
		{
			return json.is_string() && json.get_ref<const std::string&>().size() == 32;
		}

		bool ref_from_json(const SceneJson& json, AssetRef& out)
		{
			if (json.is_null())
			{
				out = AssetRef{};
				return true;
			}
			if (!json.is_object() || !json.contains("guid") || !json["guid"].is_string())
			{
				return false;
			}

			const std::optional<AssetGuid> guid = AssetGuid::from_string(json["guid"].get_ref<const std::string&>());
			if (!guid)
			{
				return false;
			}

			u64 localId = 0;
			if (json.contains("localId"))
			{
				if (!json["localId"].is_number_unsigned())
				{
					return false;
				}
				localId = json["localId"].get<u64>();
			}
			out = AssetRef{ *guid, localId };
			return true;
		}

		bool numbers_from_json(const SceneJson& json, size_t count, float* out)
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

		// Reads the plain JSON of a value of the given type. On failure, says what was expected.
		bool value_from_json(FieldType type, const FieldDescriptor* field, const SceneJson& json, FieldValue& out, std::string& expected)
		{
			float n[4] = {};
			switch (type)
			{
			case FieldType::Bool:
				expected = "not a boolean";
				if (!json.is_boolean()) return false;
				out = json.get<bool>();
				return true;
			case FieldType::Int:
				expected = "not an integer";
				if (!json.is_number_integer() || json.get<i64>() < INT32_MIN || json.get<i64>() > INT32_MAX) return false;
				out = static_cast<i32>(json.get<i64>());
				return true;
			case FieldType::Enum:
			{
				if (json.is_string() && field != nullptr)
				{
					const std::vector<std::string>& names = field->Meta.EnumNames;
					const auto it = std::find(names.begin(), names.end(), json.get_ref<const std::string&>());
					if (it != names.end())
					{
						out = static_cast<i32>(it - names.begin());
						return true;
					}
				}
				else if (json.is_number_integer())
				{
					const i64 index = json.get<i64>();
					if (index >= 0 && (field == nullptr || field->Meta.EnumNames.empty() || static_cast<size_t>(index) < field->Meta.EnumNames.size()))
					{
						out = static_cast<i32>(index);
						return true;
					}
				}

				expected = "not one of the names";
				if (field != nullptr)
				{
					for (size_t i = 0; i < field->Meta.EnumNames.size(); i++)
					{
						expected += (i == 0 ? ": " : ", ") + field->Meta.EnumNames[i];
					}
				}
				return false;
			}
			case FieldType::Float:
				expected = "not a number";
				if (!json.is_number()) return false;
				out = json.get<float>();
				return true;
			case FieldType::Vec2:
				expected = "not an array of 2 numbers";
				if (!numbers_from_json(json, 2, n)) return false;
				out = glm::vec2(n[0], n[1]);
				return true;
			case FieldType::Vec3:
				expected = "not an array of 3 numbers";
				if (!numbers_from_json(json, 3, n)) return false;
				out = glm::vec3(n[0], n[1], n[2]);
				return true;
			case FieldType::Vec4:
			case FieldType::Color:
				expected = "not an array of 4 numbers";
				if (!numbers_from_json(json, 4, n)) return false;
				out = glm::vec4(n[0], n[1], n[2], n[3]);
				return true;
			case FieldType::Quat:
				expected = "not an array of 4 numbers [x, y, z, w]";
				if (!numbers_from_json(json, 4, n)) return false;
				out = glm::quat(n[3], n[0], n[1], n[2]);
				return true;
			case FieldType::String:
				expected = "not a string";
				if (!json.is_string()) return false;
				out = json.get<std::string>();
				return true;
			case FieldType::Asset:
			{
				expected = "not an AssetRef";
				AssetRef ref;
				if (!ref_from_json(json, ref)) return false;
				out = ref;
				return true;
			}
			case FieldType::AssetList:
			{
				expected = "not a list of AssetRefs";
				if (!json.is_array()) return false;
				std::vector<AssetRef> list;
				for (const SceneJson& item : json)
				{
					AssetRef ref;
					if (!ref_from_json(item, ref)) return false;
					list.push_back(ref);
				}
				out = std::move(list);
				return true;
			}
			case FieldType::Entity:
			{
				expected = "not an entity id";
				if (json.is_null())
				{
					out = EntityGuid{};
					return true;
				}
				if (!is_hex_guid(json)) return false;
				const std::optional<EntityGuid> id = EntityGuid::from_string(json.get_ref<const std::string&>());
				if (!id) return false;
				out = *id;
				return true;
			}
			}
			return false;
		}

		bool tagged_from_json(const SceneJson& json, FieldValue& out, std::string& expected)
		{
			static const std::pair<const char*, FieldType> types[] = {
				{ "bool", FieldType::Bool }, { "int", FieldType::Int }, { "float", FieldType::Float }, { "vec2", FieldType::Vec2 }, { "vec3", FieldType::Vec3 },
				{ "vec4", FieldType::Vec4 }, { "quat", FieldType::Quat }, { "string", FieldType::String }, { "asset", FieldType::Asset },
				{ "assetlist", FieldType::AssetList }, { "entity", FieldType::Entity } };

			if (!json.is_object() || !json.contains("$type") || !json["$type"].is_string() || !json.contains("value"))
			{
				return false;
			}

			for (const auto& [name, type] : types)
			{
				if (json["$type"].get_ref<const std::string&>() == name)
				{
					return value_from_json(type, nullptr, json["value"], out, expected);
				}
			}
			expected = "unknown $type";
			return false;
		}

		// A value that is not described and not tagged: guess from the JSON, as far as that is unambiguous.
		bool guess_from_json(const SceneJson& json, FieldValue& out)
		{
			std::string ignored;
			if (json.is_boolean()) return value_from_json(FieldType::Bool, nullptr, json, out, ignored);
			if (json.is_number_integer()) return value_from_json(FieldType::Int, nullptr, json, out, ignored);
			if (json.is_number()) return value_from_json(FieldType::Float, nullptr, json, out, ignored);
			if (json.is_string()) return value_from_json(FieldType::String, nullptr, json, out, ignored);
			if (json.is_array() && json.size() >= 2 && json.size() <= 4 && std::all_of(json.begin(), json.end(), [](const SceneJson& item) { return item.is_number(); }))
			{
				const FieldType type = json.size() == 2 ? FieldType::Vec2 : json.size() == 3 ? FieldType::Vec3 : FieldType::Vec4;
				return value_from_json(type, nullptr, json, out, ignored);
			}
			if (json.is_object() && json.contains("guid")) return value_from_json(FieldType::Asset, nullptr, json, out, ignored);
			return false;
		}

		struct Reader
		{
			std::vector<std::string>& Warnings;

			// Reads one value of component.field. A described field must have the right type; anything else is tagged or guessed.
			bool read_value(const std::optional<ComponentSchema>& schema, const std::string& component, const std::string& field, const SceneJson& json, FieldValue& out, const std::string& path, std::string& error)
			{
				const FieldDescriptor* descriptor = schema ? schema->find_field(field) : nullptr;
				if (descriptor != nullptr)
				{
					std::string expected;
					if (!value_from_json(descriptor->Type, descriptor, json, out, expected))
					{
						error = path + ": " + expected;
						return false;
					}
					return true;
				}

				std::string expected;
				if (json.is_object() && json.contains("$type"))
				{
					if (!tagged_from_json(json, out, expected))
					{
						error = path + ": " + (expected.empty() ? "bad $type value" : expected);
						return false;
					}
					return true;
				}

				if (!guess_from_json(json, out))
				{
					error = path + ": " + component + "." + field + " is not known to this engine and this value cannot be told apart; write it as {\"$type\": ..., \"value\": ...}";
					return false;
				}
				return true;
			}

			bool read_fields(const std::string& component, const SceneJson& json, ComponentRecord& out, const std::string& path, std::string& error)
			{
				if (!json.is_object())
				{
					error = path + ": not an object of fields";
					return false;
				}

				const std::optional<ComponentSchema> schema = ComponentRegistry::get().schema(component);
				out.Name = component;
				for (auto it = json.begin(); it != json.end(); ++it)
				{
					FieldRecord field;
					field.Name = it.key();
					if (!read_value(schema, component, field.Name, it.value(), field.Value, path + "." + field.Name, error))
					{
						return false;
					}
					out.Fields.push_back(std::move(field));
				}
				return true;
			}

			bool read_node(const SceneJson& json, u64& out)
			{
				if (json.is_number_unsigned())
				{
					out = json.get<u64>();
					return true;
				}
				if (json.is_string())
				{
					const std::string& text = json.get_ref<const std::string&>();
					const auto result = std::from_chars(text.data(), text.data() + text.size(), out, 16);
					return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
				}
				return false;
			}

			bool read_override(const SceneJson& json, PrefabOverride& out, const std::string& path, std::string& error);

			bool read_entity(const SceneJson& json, EntityRecord& out, const std::string& path, std::string& error, bool allowParent, bool allowOverrides)
			{
				if (!json.is_object())
				{
					error = path + ": not an object";
					return false;
				}

				if (!json.contains("id") || !is_hex_guid(json["id"]))
				{
					error = path + ".id: not a 32 character hex guid";
					return false;
				}
				const std::optional<EntityGuid> id = EntityGuid::from_string(json["id"].get_ref<const std::string&>());
				if (!id || !id->valid())
				{
					error = path + ".id: not a valid hex guid";
					return false;
				}
				out.Id = *id;

				if (allowParent && json.contains("parent"))
				{
					const std::optional<EntityGuid> parent = is_hex_guid(json["parent"]) ? EntityGuid::from_string(json["parent"].get_ref<const std::string&>()) : std::nullopt;
					if (!parent)
					{
						error = path + ".parent: not a 32 character hex guid";
						return false;
					}
					out.Parent = *parent;
				}

				if (json.contains("components"))
				{
					if (!json["components"].is_object())
					{
						error = path + ".components: not an object";
						return false;
					}

					for (auto it = json["components"].begin(); it != json["components"].end(); ++it)
					{
						if (it.key() == "Id")
						{
							Warnings.push_back(path + ": a component called Id is ignored, the id is the entity's id");
							continue;
						}

						ComponentRecord component;
						if (!read_fields(it.key(), it.value(), component, path + "." + it.key(), error))
						{
							return false;
						}
						out.Components.push_back(std::move(component));
					}
				}

				if (allowOverrides && json.contains("overrides"))
				{
					if (!json["overrides"].is_array())
					{
						error = path + ".overrides: not an array";
						return false;
					}

					for (size_t i = 0; i < json["overrides"].size(); i++)
					{
						PrefabOverride override;
						if (!read_override(json["overrides"][i], override, path + ".overrides[" + std::to_string(i) + "]", error))
						{
							return false;
						}
						out.Overrides.push_back(std::move(override));
					}
				}
				return true;
			}
		};

		bool Reader::read_override(const SceneJson& json, PrefabOverride& out, const std::string& path, std::string& error)
		{
			if (!json.is_object())
			{
				error = path + ": not an object";
				return false;
			}

			static const char* kinds[] = { "SetField", "AddComponent", "RemoveComponent", "AddChild", "RemoveNode" };
			if (!json.contains("kind") || !json["kind"].is_string())
			{
				error = path + ".kind: missing";
				return false;
			}

			const std::string& kind = json["kind"].get_ref<const std::string&>();
			bool known = false;
			for (u32 i = 0; i < 5; i++)
			{
				if (kind == kinds[i])
				{
					out.Kind = static_cast<OverrideKind>(i);
					known = true;
				}
			}
			if (!known)
			{
				error = path + ".kind: unknown override kind \"" + kind + "\"";
				return false;
			}

			if (!json.contains("node") || !read_node(json["node"], out.NodeId))
			{
				error = path + ".node: not a node id (16 hex digits)";
				return false;
			}

			auto read_string = [&](const char* key, std::string& value)
			{
				if (!json.contains(key) || !json[key].is_string())
				{
					error = path + "." + key + ": missing or not a string";
					return false;
				}
				value = json[key].get<std::string>();
				return true;
			};

			switch (out.Kind)
			{
			case OverrideKind::SetField:
			{
				if (!read_string("component", out.Component) || !read_string("field", out.Field))
				{
					return false;
				}
				if (!json.contains("value"))
				{
					error = path + ".value: missing";
					return false;
				}
				return read_value(ComponentRegistry::get().schema(out.Component), out.Component, out.Field, json["value"], out.Value, path + ".value", error);
			}
			case OverrideKind::AddComponent:
			{
				std::string name;
				if (!read_string("component", name))
				{
					return false;
				}
				return read_fields(name, json.contains("fields") ? json["fields"] : SceneJson::object(), out.Added, path + ".fields", error);
			}
			case OverrideKind::RemoveComponent:
				return read_string("component", out.Component);
			case OverrideKind::AddChild:
				if (!json.contains("child"))
				{
					error = path + ".child: missing";
					return false;
				}
				return read_entity(json["child"], out.Child, path + ".child", error, false, false);
			case OverrideKind::RemoveNode:
				return true;
			}
			return false;
		}
	}

	namespace
	{
		// ---- the settings block ----

		SceneJson vec3_json(const glm::vec3& v)
		{
			return SceneJson::array({ static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z) });
		}

		void put_float_if_different(SceneJson& json, const char* key, float value, float defaultValue)
		{
			if (value != defaultValue)
			{
				json[key] = static_cast<double>(value);
			}
		}

		void put_vec3_if_different(SceneJson& json, const char* key, const glm::vec3& value, const glm::vec3& defaultValue)
		{
			if (value != defaultValue)
			{
				json[key] = vec3_json(value);
			}
		}

		SceneJson settings_to_json(const SceneSettings& s)
		{
			const SceneSettings d;
			SceneJson json = SceneJson::object();
			if (s.Source != d.Source)
			{
				json["source"] = s.Source == EnvironmentSource::File ? "file" : (s.Source == EnvironmentSource::Atmosphere ? "atmosphere" : "none");
			}
			if (s.EnvironmentAsset.valid())
			{
				json["environment"] = ref_to_json(s.EnvironmentAsset);
			}
			put_float_if_different(json, "rotation", s.RotationDegrees, d.RotationDegrees);
			put_float_if_different(json, "brightness", s.Brightness, d.Brightness);
			put_float_if_different(json, "ambient", s.AmbientIntensity, d.AmbientIntensity);
			put_vec3_if_different(json, "hemisphereSky", s.HemisphereSky, d.HemisphereSky);
			put_vec3_if_different(json, "hemisphereGround", s.HemisphereGround, d.HemisphereGround);

			SceneJson atmosphere = SceneJson::object();
			const AtmosphereParameters& a = s.Atmosphere;
			const AtmosphereParameters& da = d.Atmosphere;
			put_float_if_different(atmosphere, "rayleigh", a.RayleighScale, da.RayleighScale);
			put_float_if_different(atmosphere, "mie", a.MieScale, da.MieScale);
			put_float_if_different(atmosphere, "ozone", a.OzoneScale, da.OzoneScale);
			put_float_if_different(atmosphere, "mieAnisotropy", a.MieAnisotropy, da.MieAnisotropy);
			put_float_if_different(atmosphere, "multipleScattering", a.MultipleScattering, da.MultipleScattering);
			put_float_if_different(atmosphere, "sunIlluminance", a.SunIlluminance, da.SunIlluminance);
			put_float_if_different(atmosphere, "skyBrightness", a.SkyBrightness, da.SkyBrightness);
			put_float_if_different(atmosphere, "sunDiscBoost", a.SunDiscBoost, da.SunDiscBoost);
			put_vec3_if_different(atmosphere, "groundAlbedo", a.GroundAlbedo, da.GroundAlbedo);
			if (!atmosphere.empty())
			{
				json["atmosphere"] = std::move(atmosphere);
			}

			if (s.Background != d.Background)
			{
				json["background"] = s.Background == BackgroundMode::SolidColor ? "color" : "sky";
			}
			put_vec3_if_different(json, "backgroundColor", s.BackgroundColor, d.BackgroundColor);
			put_float_if_different(json, "exposureEv", s.ExposureEv, d.ExposureEv);

			auto put_bool = [](SceneJson& object, const char* key, bool value, bool defaultValue)
			{
				if (value != defaultValue)
				{
					object[key] = value;
				}
			};
			auto put_group = [&](const char* key, SceneJson& group)
			{
				if (!group.empty())
				{
					json[key] = std::move(group);
				}
			};

			SceneJson exposure = SceneJson::object();
			put_bool(exposure, "auto", s.Exposure.Auto, d.Exposure.Auto);
			put_float_if_different(exposure, "compensation", s.Exposure.CompensationEv, d.Exposure.CompensationEv);
			put_float_if_different(exposure, "key", s.Exposure.Key, d.Exposure.Key);
			put_float_if_different(exposure, "minEv", s.Exposure.MinEv, d.Exposure.MinEv);
			put_float_if_different(exposure, "maxEv", s.Exposure.MaxEv, d.Exposure.MaxEv);
			put_float_if_different(exposure, "adaptSpeed", s.Exposure.AdaptSpeed, d.Exposure.AdaptSpeed);
			put_float_if_different(exposure, "lowPercent", s.Exposure.LowPercent, d.Exposure.LowPercent);
			put_float_if_different(exposure, "highPercent", s.Exposure.HighPercent, d.Exposure.HighPercent);
			put_group("exposure", exposure);

			SceneJson bloom = SceneJson::object();
			put_bool(bloom, "enabled", s.Bloom.Enabled, d.Bloom.Enabled);
			put_float_if_different(bloom, "threshold", s.Bloom.Threshold, d.Bloom.Threshold);
			put_float_if_different(bloom, "knee", s.Bloom.Knee, d.Bloom.Knee);
			put_float_if_different(bloom, "intensity", s.Bloom.Intensity, d.Bloom.Intensity);
			put_group("bloom", bloom);

			SceneJson tonemap = SceneJson::object();
			if (s.Tonemap.Operator != d.Tonemap.Operator)
			{
				static const char* const names[] = { "aces", "reinhard", "neutral", "none" };
				tonemap["operator"] = names[std::min<u32>(static_cast<u32>(s.Tonemap.Operator), 3)];
			}
			put_float_if_different(tonemap, "contrast", s.Tonemap.Contrast, d.Tonemap.Contrast);
			put_float_if_different(tonemap, "saturation", s.Tonemap.Saturation, d.Tonemap.Saturation);
			put_float_if_different(tonemap, "temperature", s.Tonemap.Temperature, d.Tonemap.Temperature);
			put_float_if_different(tonemap, "tint", s.Tonemap.Tint, d.Tonemap.Tint);
			put_float_if_different(tonemap, "vignette", s.Tonemap.Vignette, d.Tonemap.Vignette);
			put_group("tonemap", tonemap);

			SceneJson ao = SceneJson::object();
			put_bool(ao, "enabled", s.Ao.Enabled, d.Ao.Enabled);
			put_float_if_different(ao, "radius", s.Ao.Radius, d.Ao.Radius);
			put_float_if_different(ao, "power", s.Ao.Power, d.Ao.Power);
			put_group("ao", ao);

			SceneJson taa = SceneJson::object();
			put_float_if_different(taa, "feedback", s.Taa.Feedback, d.Taa.Feedback);
			put_group("taa", taa);

			SceneJson motionBlur = SceneJson::object();
			put_bool(motionBlur, "enabled", s.MotionBlur.Enabled, d.MotionBlur.Enabled);
			put_float_if_different(motionBlur, "exposureTime", s.MotionBlur.ExposureTime, d.MotionBlur.ExposureTime);
			put_float_if_different(motionBlur, "strength", s.MotionBlur.Strength, d.MotionBlur.Strength);
			put_float_if_different(motionBlur, "maxBlurPixels", s.MotionBlur.MaxBlurPixels, d.MotionBlur.MaxBlurPixels);
			put_group("motionBlur", motionBlur);
			return json;
		}

		bool read_float(const SceneJson& json, const char* key, const std::string& path, float& out, std::string& error)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_number())
			{
				error = path + "." + key + ": not a number";
				return false;
			}
			out = json[key].get<float>();
			return true;
		}

		bool read_bool(const SceneJson& json, const char* key, const std::string& path, bool& out, std::string& error)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_boolean())
			{
				error = path + "." + key + ": not a boolean";
				return false;
			}
			out = json[key].get<bool>();
			return true;
		}

		// A group (an object) that may be missing; calls read with the object when it is there.
		template<typename F>
		bool read_group(const SceneJson& json, const char* key, const std::string& path, std::string& error, F read)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_object())
			{
				error = path + "." + key + ": not an object";
				return false;
			}
			return read(json[key], path + "." + key);
		}

		bool read_vec3(const SceneJson& json, const char* key, const std::string& path, glm::vec3& out, std::string& error)
		{
			if (!json.contains(key))
			{
				return true;
			}
			const SceneJson& value = json[key];
			if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
			{
				error = path + "." + key + ": not an array of 3 numbers";
				return false;
			}
			out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return true;
		}

		bool read_choice(const SceneJson& json, const char* key, const std::string& path, std::initializer_list<const char*> names, u32& out, std::string& error)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_string())
			{
				error = path + "." + key + ": not a string";
				return false;
			}
			const std::string& text = json[key].get_ref<const std::string&>();
			u32 index = 0;
			for (const char* name : names)
			{
				if (text == name)
				{
					out = index;
					return true;
				}
				index++;
			}
			error = path + "." + key + ": unknown value \"" + text + "\"";
			return false;
		}

		bool settings_from_json(const SceneJson& json, SceneSettings& s, std::string& error)
		{
			const std::string path = "settings";
			if (!json.is_object())
			{
				error = "settings: not an object";
				return false;
			}

			u32 source = static_cast<u32>(s.Source);
			u32 background = static_cast<u32>(s.Background);
			if (!read_choice(json, "source", path, { "none", "file", "atmosphere" }, source, error)
				|| !read_choice(json, "background", path, { "sky", "color" }, background, error)
				|| !read_float(json, "rotation", path, s.RotationDegrees, error) || !read_float(json, "brightness", path, s.Brightness, error)
				|| !read_float(json, "ambient", path, s.AmbientIntensity, error) || !read_float(json, "exposureEv", path, s.ExposureEv, error)
				|| !read_vec3(json, "hemisphereSky", path, s.HemisphereSky, error) || !read_vec3(json, "hemisphereGround", path, s.HemisphereGround, error)
				|| !read_vec3(json, "backgroundColor", path, s.BackgroundColor, error))
			{
				return false;
			}
			s.Source = static_cast<EnvironmentSource>(source);
			s.Background = static_cast<BackgroundMode>(background);

			if (json.contains("environment") && !ref_from_json(json["environment"], s.EnvironmentAsset))
			{
				error = path + ".environment: not an AssetRef";
				return false;
			}

			if (json.contains("atmosphere"))
			{
				const SceneJson& atmosphere = json["atmosphere"];
				const std::string atmospherePath = path + ".atmosphere";
				if (!atmosphere.is_object())
				{
					error = atmospherePath + ": not an object";
					return false;
				}
				AtmosphereParameters& a = s.Atmosphere;
				if (!read_float(atmosphere, "rayleigh", atmospherePath, a.RayleighScale, error) || !read_float(atmosphere, "mie", atmospherePath, a.MieScale, error)
					|| !read_float(atmosphere, "ozone", atmospherePath, a.OzoneScale, error) || !read_float(atmosphere, "mieAnisotropy", atmospherePath, a.MieAnisotropy, error)
					|| !read_float(atmosphere, "multipleScattering", atmospherePath, a.MultipleScattering, error)
					|| !read_float(atmosphere, "sunIlluminance", atmospherePath, a.SunIlluminance, error)
					|| !read_float(atmosphere, "skyBrightness", atmospherePath, a.SkyBrightness, error) || !read_float(atmosphere, "sunDiscBoost", atmospherePath, a.SunDiscBoost, error)
					|| !read_vec3(atmosphere, "groundAlbedo", atmospherePath, a.GroundAlbedo, error))
				{
					return false;
				}
			}

			ExposureSettings& e = s.Exposure;
			BloomSettings& b = s.Bloom;
			TonemapSettings& t = s.Tonemap;
			AoSettings& ao = s.Ao;
			u32 tonemapOperator = static_cast<u32>(t.Operator);
			if (!read_group(json, "exposure", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_bool(g, "auto", p, e.Auto, error) && read_float(g, "compensation", p, e.CompensationEv, error) && read_float(g, "key", p, e.Key, error)
							&& read_float(g, "minEv", p, e.MinEv, error) && read_float(g, "maxEv", p, e.MaxEv, error) && read_float(g, "adaptSpeed", p, e.AdaptSpeed, error)
							&& read_float(g, "lowPercent", p, e.LowPercent, error) && read_float(g, "highPercent", p, e.HighPercent, error);
					})
				|| !read_group(json, "bloom", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_bool(g, "enabled", p, b.Enabled, error) && read_float(g, "threshold", p, b.Threshold, error) && read_float(g, "knee", p, b.Knee, error)
							&& read_float(g, "intensity", p, b.Intensity, error);
					})
				|| !read_group(json, "tonemap", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_choice(g, "operator", p, { "aces", "reinhard", "neutral", "none" }, tonemapOperator, error) && read_float(g, "contrast", p, t.Contrast, error)
							&& read_float(g, "saturation", p, t.Saturation, error) && read_float(g, "temperature", p, t.Temperature, error) && read_float(g, "tint", p, t.Tint, error)
							&& read_float(g, "vignette", p, t.Vignette, error);
					})
				|| !read_group(json, "ao", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_bool(g, "enabled", p, ao.Enabled, error) && read_float(g, "radius", p, ao.Radius, error) && read_float(g, "power", p, ao.Power, error);
					})
				|| !read_group(json, "taa", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_float(g, "feedback", p, s.Taa.Feedback, error);
					})
				|| !read_group(json, "motionBlur", path, error, [&](const SceneJson& g, const std::string& p)
					{
						return read_bool(g, "enabled", p, s.MotionBlur.Enabled, error) && read_float(g, "exposureTime", p, s.MotionBlur.ExposureTime, error)
							&& read_float(g, "strength", p, s.MotionBlur.Strength, error) && read_float(g, "maxBlurPixels", p, s.MotionBlur.MaxBlurPixels, error);
					}))
			{
				return false;
			}
			t.Operator = static_cast<TonemapOperator>(tonemapOperator);
			return true;
		}
	}

	SceneJsonResult scene_from_json(const SceneJson& json)
	{
		SceneJsonResult result;
		auto fail = [&](std::string message)
		{
			result.Error = std::move(message);
			result.Scene.reset();
			return std::move(result);
		};

		if (!json.is_object() || !json.contains("entities") || !json["entities"].is_array())
		{
			return fail("entities: missing or not an array");
		}

		SceneData data;
		if (json.contains("name") && json["name"].is_string())
		{
			data.Name = json["name"].get<std::string>();
		}

		Reader reader{ result.Warnings };
		const SceneJson& list = json["entities"];
		std::vector<EntityRecord> entities(list.size());
		std::unordered_map<EntityGuid, size_t> index;
		for (size_t i = 0; i < list.size(); i++)
		{
			std::string error;
			const std::string path = "entities[" + std::to_string(i) + "]";
			if (!reader.read_entity(list[i], entities[i], path, error, true, true))
			{
				return fail(error);
			}

			const auto [it, inserted] = index.emplace(entities[i].Id, i);
			if (!inserted)
			{
				return fail(path + ".id: duplicate of entities[" + std::to_string(it->second) + "]");
			}
		}

		std::vector<std::vector<size_t>> children(entities.size());
		std::vector<size_t> roots;
		for (size_t i = 0; i < entities.size(); i++)
		{
			if (!entities[i].Parent.valid())
			{
				roots.push_back(i);
				continue;
			}

			const auto parent = index.find(entities[i].Parent);
			if (parent == index.end())
			{
				return fail("entities[" + std::to_string(i) + "].parent: no entity has this id");
			}
			children[parent->second].push_back(i);
		}

		// parents first; an entity that is not reached sits in a loop
		std::vector<size_t> stack(roots.rbegin(), roots.rend());
		while (!stack.empty())
		{
			const size_t i = stack.back();
			stack.pop_back();
			data.Entities.push_back(std::move(entities[i]));
			stack.insert(stack.end(), children[i].rbegin(), children[i].rend());
		}
		if (data.Entities.size() != entities.size())
		{
			for (size_t i = 0; i < entities.size(); i++)
			{
				if (entities[i].Id.valid() && std::none_of(data.Entities.begin(), data.Entities.end(), [&](const EntityRecord& e) { return e.Id == entities[i].Id; }))
				{
					return fail("entities[" + std::to_string(i) + "].parent: parent loop");
				}
			}
		}

		if (json.contains("settings"))
		{
			std::string error;
			if (!settings_from_json(json["settings"], data.Settings, error))
			{
				return fail(error);
			}
			data.Settings = sanitized(data.Settings);
		}

		result.Scene = std::move(data);
		return result;
	}

	SceneJson scene_to_json(const SceneData& data)
	{
		SceneJson json = SceneJson::object();
		json["name"] = data.Name;
		SceneJson entities = SceneJson::array();
		for (const EntityRecord& entity : data.Entities)
		{
			entities.push_back(entity_to_json(entity, true, true));
		}
		json["entities"] = std::move(entities);
		SceneJson settings = settings_to_json(data.Settings);
		if (!settings.empty())
		{
			json["settings"] = std::move(settings);
		}
		return json;
	}
}
