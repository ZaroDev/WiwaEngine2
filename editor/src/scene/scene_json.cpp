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
						if (it.key() == ID_COMPONENT)
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

		// Writes the fields the list in scene_settings.h visits that differ from the default, into objects it opens for its groups.
		struct SettingsJsonWriter
		{
			SceneJson* Out = nullptr;

			template<typename E, size_t N>
			void choice(const char* key, E value, const E& fallback, const std::array<const char*, N>& names)
			{
				if (value != fallback)
				{
					(*Out)[key] = names[std::min<size_t>(static_cast<size_t>(value), N - 1)];
				}
			}

			void asset(const char* key, const AssetRef& value)
			{
				if (value.valid())
				{
					(*Out)[key] = ref_to_json(value);
				}
			}

			void angle(const char* key, float value, const float& fallback) { number(key, value, fallback, 0.0f, 0.0f); }

			void number(const char* key, float value, const float& fallback, float, float)
			{
				if (value != fallback)
				{
					(*Out)[key] = static_cast<double>(value);
				}
			}

			void vec3(const char* key, const glm::vec3& value, const glm::vec3& fallback, float, float)
			{
				if (value != fallback)
				{
					(*Out)[key] = vec3_json(value);
				}
			}

			void flag(const char* key, bool value, const bool& fallback)
			{
				if (value != fallback)
				{
					(*Out)[key] = value;
				}
			}

			void count(const char* key, u32 value, const u32& fallback, u32, u32)
			{
				if (value != fallback)
				{
					(*Out)[key] = value;
				}
			}

			// the file has the scripts before the groups: settings_to_json puts them there
			void scripts(const char*, const std::vector<AssetRef>&) {}

			template<typename F>
			void group(const char* key, F fn)
			{
				SceneJson* parent = Out;
				SceneJson object = SceneJson::object();
				Out = &object;
				fn();
				Out = parent;
				if (!object.empty())
				{
					(*parent)[key] = std::move(object);
				}
			}
		};

		SceneJson settings_to_json(const SceneSettings& s)
		{
			SceneJson json = SceneJson::object();
			SettingsJsonWriter writer;
			writer.Out = &json;
			visit_scene_settings(s, SceneSettings{}, writer);

			if (!s.Scripts.empty())
			{
				SceneJson scripts = SceneJson::array();
				for (const AssetRef& script : s.Scripts)
				{
					scripts.push_back(ref_to_json(script));
				}

				// before the first group that follows exposureEv in the list, else last
				static constexpr const char* GROUPS_AFTER[] = { "exposure", "bloom", "tonemap", "ao", "taa", "motionBlur", "physics" };
				SceneJson ordered = SceneJson::object();
				bool placed = false;
				for (auto& item : json.items())
				{
					if (!placed && std::find(std::begin(GROUPS_AFTER), std::end(GROUPS_AFTER), item.key()) != std::end(GROUPS_AFTER))
					{
						ordered["scripts"] = std::move(scripts);
						placed = true;
					}
					ordered[item.key()] = std::move(item.value());
				}
				if (!placed)
				{
					ordered["scripts"] = std::move(scripts);
				}
				json = std::move(ordered);
			}
			return json;
		}

		// Reads the fields the list in scene_settings.h visits from the JSON object it is on, into the group objects it opens. The
		// first error stops the reading: the rest of the fields are left alone.
		struct SettingsJsonReader
		{
			const SceneJson* Json = nullptr;
			std::string Path;
			std::string* Error = nullptr;
			bool Ok = true;

			bool has(const char* key) const { return Ok && Json->contains(key); }

			void fail(const char* key, const std::string& what)
			{
				*Error = Path + "." + key + ": " + what;
				Ok = false;
			}

			template<typename E, size_t N>
			void choice(const char* key, E& value, const E&, const std::array<const char*, N>& names)
			{
				if (!has(key))
				{
					return;
				}
				const SceneJson& json = (*Json)[key];
				if (!json.is_string())
				{
					fail(key, "not a string");
					return;
				}
				const std::string& text = json.get_ref<const std::string&>();
				for (size_t i = 0; i < N; i++)
				{
					if (text == names[i])
					{
						value = static_cast<E>(i);
						return;
					}
				}
				fail(key, "unknown value \"" + text + "\"");
			}

			void asset(const char* key, AssetRef& value)
			{
				if (has(key) && !ref_from_json((*Json)[key], value))
				{
					fail(key, "not an AssetRef");
				}
			}

			void angle(const char* key, float& value, const float&) { number(key, value, 0.0f, 0.0f, 0.0f); }

			void number(const char* key, float& value, const float&, float, float)
			{
				if (!has(key))
				{
					return;
				}
				if (!(*Json)[key].is_number())
				{
					fail(key, "not a number");
					return;
				}
				value = (*Json)[key].get<float>();
			}

			void vec3(const char* key, glm::vec3& value, const glm::vec3&, float, float)
			{
				if (!has(key))
				{
					return;
				}
				const SceneJson& json = (*Json)[key];
				if (!json.is_array() || json.size() != 3 || !json[0].is_number() || !json[1].is_number() || !json[2].is_number())
				{
					fail(key, "not an array of 3 numbers");
					return;
				}
				value = glm::vec3(json[0].get<float>(), json[1].get<float>(), json[2].get<float>());
			}

			void flag(const char* key, bool& value, const bool&)
			{
				if (!has(key))
				{
					return;
				}
				if (!(*Json)[key].is_boolean())
				{
					fail(key, "not a boolean");
					return;
				}
				value = (*Json)[key].get<bool>();
			}

			void count(const char* key, u32& value, const u32&, u32, u32)
			{
				if (!has(key))
				{
					return;
				}
				if (!(*Json)[key].is_number_unsigned())
				{
					fail(key, "not a non-negative integer");
					return;
				}
				value = (*Json)[key].get<u32>();
			}

			void scripts(const char* key, std::vector<AssetRef>& list)
			{
				if (!has(key))
				{
					return;
				}
				const SceneJson& json = (*Json)[key];
				if (!json.is_array())
				{
					fail(key, "not an array");
					return;
				}
				for (size_t i = 0; i < json.size(); i++)
				{
					AssetRef script;
					if (!ref_from_json(json[i], script))
					{
						*Error = Path + "." + key + "[" + std::to_string(i) + "]: not an AssetRef";
						Ok = false;
						return;
					}
					list.push_back(script);
				}
			}

			template<typename F>
			void group(const char* key, F fn)
			{
				if (!has(key))
				{
					return;
				}
				const SceneJson& json = (*Json)[key];
				if (!json.is_object())
				{
					fail(key, "not an object");
					return;
				}
				const SceneJson* parent = Json;
				const std::string parentPath = Path;
				Json = &json;
				Path = Path + "." + key;
				fn();
				Json = parent;
				Path = parentPath;
			}
		};

		bool settings_from_json(const SceneJson& json, SceneSettings& s, std::string& error)
		{
			if (!json.is_object())
			{
				error = "settings: not an object";
				return false;
			}

			SettingsJsonReader reader;
			reader.Json = &json;
			reader.Path = "settings";
			reader.Error = &error;
			visit_scene_settings(s, SceneSettings{}, reader);
			return reader.Ok;
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
