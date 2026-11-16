#include <pch.h>

#include <assets/wasset_io.h>

namespace wiwa
{
	namespace
	{
		// Reads a count with a ceiling, and refuses one that claims more elements than the bytes left could hold, before
		// anything is allocated.
		template<typename T, typename F>
		void bounded_list(BinaryArchive& ar, std::vector<T>& v, u64 maxCount, u64 minElementBytes, F fn)
		{
			u64 count = v.size();
			ar.value(count);
			if (ar.reading())
			{
				if (!ar.ok() || count > maxCount || count > ar.remaining() / minElementBytes)
				{
					ar.fail();
					return;
				}
				v.resize(static_cast<size_t>(count));
			}

			for (T& element : v)
			{
				if (!ar.ok())
				{
					return;
				}
				fn(element);
			}
		}

		template<typename T>
		void archive_alternative(BinaryArchive& ar, T& value)
		{
			if constexpr (std::is_same_v<T, std::string>)
			{
				ar.string(value);
			}
			else if constexpr (std::is_same_v<T, std::vector<AssetRef>>)
			{
				ar.pod_vector(value);
			}
			else
			{
				ar.value(value);
			}
		}

		template<size_t I>
		bool read_alternative(BinaryArchive& ar, FieldValue& value, u32 index)
		{
			if constexpr (I >= std::variant_size_v<FieldValue>)
			{
				return false;
			}
			else
			{
				if (index != I)
				{
					return read_alternative<I + 1>(ar, value, index);
				}

				std::variant_alternative_t<I, FieldValue> alternative{};
				archive_alternative(ar, alternative);
				value.emplace<I>(std::move(alternative));
				return true;
			}
		}

		void archive_value(BinaryArchive& ar, FieldValue& value)
		{
			u32 tag = static_cast<u32>(value.index());
			ar.value(tag);
			if (ar.reading())
			{
				if (!ar.ok() || !read_alternative<0>(ar, value, tag))
				{
					ar.fail();
				}
				return;
			}
			std::visit([&](auto& alternative) { archive_alternative(ar, alternative); }, value);
		}

		void archive_component(BinaryArchive& ar, ComponentRecord& component)
		{
			ar.string(component.Name);
			bounded_list(ar, component.Fields, SCENE_MAX_FIELDS_PER_COMPONENT, 13, [&](FieldRecord& field)
				{
					ar.string(field.Name);
					archive_value(ar, field.Value);
				});
		}

		void archive_entity(BinaryArchive& ar, EntityRecord& entity, u32 depth);

		void archive_override(BinaryArchive& ar, PrefabOverride& override, u32 depth)
		{
			u32 kind = static_cast<u32>(override.Kind);
			ar.value(kind);
			if (ar.reading())
			{
				if (kind > static_cast<u32>(OverrideKind::RemoveNode))
				{
					ar.fail();
					return;
				}
				override.Kind = static_cast<OverrideKind>(kind);
			}

			ar.value(override.NodeId);
			ar.string(override.Component);
			ar.string(override.Field);
			archive_value(ar, override.Value);
			archive_component(ar, override.Added);
			archive_entity(ar, override.Child, depth + 1);
		}

		void archive_entity(BinaryArchive& ar, EntityRecord& entity, u32 depth)
		{
			if (depth > SCENE_MAX_CHILD_DEPTH)
			{
				ar.fail();
				return;
			}

			ar.value(entity.Id);
			ar.value(entity.Parent);
			bounded_list(ar, entity.Components, SCENE_MAX_COMPONENTS_PER_ENTITY, 16, [&](ComponentRecord& component) { archive_component(ar, component); });
			bounded_list(ar, entity.Overrides, SCENE_MAX_OVERRIDES, 8, [&](PrefabOverride& override) { archive_override(ar, override, depth); });
		}
	}

	namespace
	{
		// The file order of the settings is the order of the list in scene_settings.h.
		struct SettingsArchiver
		{
			BinaryArchive& Ar;

			template<typename E, size_t N>
			void choice(const char*, E& value, const E&, const std::array<const char*, N>&)
			{
				u32 raw = static_cast<u32>(value);
				Ar.value(raw);
				if (Ar.reading())
				{
					if (raw >= N)
					{
						Ar.fail();
						return;
					}
					value = static_cast<E>(raw);
				}
			}

			void asset(const char*, AssetRef& value) { Ar.value(value); }
			void angle(const char*, float& value, const float&) { Ar.value(value); }
			void number(const char*, float& value, const float&, float, float) { Ar.value(value); }
			void vec3(const char*, glm::vec3& value, const glm::vec3&, float, float) { Ar.value(value); }
			void flag(const char*, bool& value, const bool&) { Ar.value(value); }
			void count(const char*, u32& value, const u32&, u32, u32) { Ar.value(value); }
			void scripts(const char*, std::vector<AssetRef>& list) { Ar.pod_vector(list); }

			template<typename F>
			void group(const char*, F fn) { fn(); }
		};

		void archive_settings(BinaryArchive& ar, SceneSettings& s)
		{
			visit_scene_settings(s, SceneSettings{}, SettingsArchiver{ ar });
		}
	}

	void archive_component_list(BinaryArchive& ar, std::vector<ComponentRecord>& list)
	{
		bounded_list(ar, list, SCENE_MAX_COMPONENTS_PER_ENTITY, 16, [&](ComponentRecord& component) { archive_component(ar, component); });
	}

	void archive_override_list(BinaryArchive& ar, std::vector<PrefabOverride>& list)
	{
		bounded_list(ar, list, SCENE_MAX_OVERRIDES, 8, [&](PrefabOverride& override) { archive_override(ar, override, 0); });
	}

	void archive(BinaryArchive& ar, SceneData& v)
	{
		ar.string(v.Name);
		// the smallest entity: two guids (32 bytes) and the counts of its components and overrides (16 bytes)
		bounded_list(ar, v.Entities, SCENE_MAX_ENTITIES, 48, [&](EntityRecord& entity) { archive_entity(ar, entity, 0); });
		if (ar.ok())
		{
			archive_settings(ar, v.Settings);
		}
		if (ar.reading() && ar.ok())
		{
			v.Settings = sanitized(v.Settings);
		}
	}
}
