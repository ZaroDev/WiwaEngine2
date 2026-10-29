#pragma once

#include <memory>
#include <type_traits>

#include "component_info.h"

// Describes a C++ component to the reflection layer:
//
//   WI_COMPONENT(Light,
//       WI_FIELD(Kind),
//       WI_FIELD_META(Intensity, FieldMeta::range(0.0f, 100.0f)))
//
// expands to a ComponentInfo built by reflect_component<Light>(...). A member whose type has no FieldTypeOf is a compile error.
namespace wiwa
{
	template<typename T>
	struct FieldTypeOf; // no definition: an unsupported member type does not compile

	template<> struct FieldTypeOf<bool> { static constexpr FieldType Value = FieldType::Bool; };
	template<> struct FieldTypeOf<i32> { static constexpr FieldType Value = FieldType::Int; };
	template<> struct FieldTypeOf<float> { static constexpr FieldType Value = FieldType::Float; };
	template<> struct FieldTypeOf<glm::vec2> { static constexpr FieldType Value = FieldType::Vec2; };
	template<> struct FieldTypeOf<glm::vec3> { static constexpr FieldType Value = FieldType::Vec3; };
	template<> struct FieldTypeOf<glm::vec4> { static constexpr FieldType Value = FieldType::Vec4; };
	template<> struct FieldTypeOf<glm::quat> { static constexpr FieldType Value = FieldType::Quat; };
	template<> struct FieldTypeOf<std::string> { static constexpr FieldType Value = FieldType::String; };
	template<> struct FieldTypeOf<AssetRef> { static constexpr FieldType Value = FieldType::Asset; };
	template<> struct FieldTypeOf<std::vector<AssetRef>> { static constexpr FieldType Value = FieldType::AssetList; };
	template<> struct FieldTypeOf<EntityGuid> { static constexpr FieldType Value = FieldType::Entity; };

	template<typename T>
		requires std::is_enum_v<T>
	struct FieldTypeOf<T> { static constexpr FieldType Value = FieldType::Enum; };

	template<typename Accessor>
	struct FieldDef
	{
		const char* Name;
		Accessor Access;
		FieldMeta Meta;
	};

	template<typename Accessor>
	FieldDef<Accessor> make_field(const char* name, Accessor access, FieldMeta meta = {})
	{
		return FieldDef<Accessor>{ name, access, std::move(meta) };
	}

	namespace detail
	{
		template<typename M>
		FieldValue to_field_value(const M& member)
		{
			if constexpr (std::is_enum_v<M>)
			{
				return FieldValue(std::in_place_type<i32>, static_cast<i32>(member));
			}
			else
			{
				return FieldValue(std::in_place_type<M>, member);
			}
		}

		template<typename M>
		void from_field_value(M& member, const FieldValue& value)
		{
			if constexpr (std::is_enum_v<M>)
			{
				member = static_cast<M>(std::get<i32>(value));
			}
			else
			{
				member = std::get<M>(value);
			}
		}
	}

	template<typename C, typename... Defs>
	ComponentInfo reflect_component(const char* name, Defs... defs)
	{
		using Getter = std::function<FieldValue(C&)>;
		using Setter = std::function<bool(C&, const FieldValue&)>;

		ComponentInfo info;
		info.Name = name;
		auto getters = std::make_shared<std::vector<Getter>>();
		auto setters = std::make_shared<std::vector<Setter>>();
		C defaults{};

		auto add_field = [&](auto def)
		{
			using M = std::remove_cvref_t<std::invoke_result_t<decltype(def.Access), C&>>;

			FieldDescriptor field;
			field.Name = def.Name;
			field.Type = FieldTypeOf<M>::Value;
			if (field.Type == FieldType::Vec4 && def.Meta.IsColor)
			{
				field.Type = FieldType::Color;
			}
			field.Meta = std::move(def.Meta);
			field.Default = detail::to_field_value<M>(def.Access(defaults));

			auto access = def.Access;
			getters->push_back([access](C& component) { return detail::to_field_value<M>(access(component)); });
			setters->push_back([access, field](C& component, const FieldValue& value)
			{
				if (!is_valid_field_value(field, value))
				{
					return false;
				}
				detail::from_field_value<M>(access(component), value);
				return true;
			});
			info.Fields.push_back(std::move(field));
		};
		(add_field(defs), ...);

		info.Has = [](entt::registry& registry, entt::entity entity) { return registry.all_of<C>(entity); };
		info.Add = [](entt::registry& registry, entt::entity entity)
		{
			if (!registry.all_of<C>(entity))
			{
				registry.emplace<C>(entity);
			}
		};
		info.Remove = [](entt::registry& registry, entt::entity entity) { registry.remove<C>(entity); };

		auto defaultValues = std::make_shared<std::vector<FieldValue>>();
		for (const FieldDescriptor& field : info.Fields)
		{
			defaultValues->push_back(field.Default);
		}
		info.Get = [getters, defaultValues](entt::registry& registry, entt::entity entity, size_t index) -> FieldValue
		{
			if (index >= getters->size())
			{
				return FieldValue{};
			}

			C* component = registry.try_get<C>(entity);
			return component == nullptr ? (*defaultValues)[index] : (*getters)[index](*component);
		};
		info.Set = [setters](entt::registry& registry, entt::entity entity, size_t index, const FieldValue& value)
		{
			C* component = registry.try_get<C>(entity);
			if (component == nullptr || index >= setters->size())
			{
				return false;
			}
			return (*setters)[index](*component, value);
		};
		return info;
	}
}

#define WI_FIELD(Member) ::wiwa::make_field(#Member, [](auto& component) -> auto& { return component.Member; })
#define WI_FIELD_META(Member, Meta) ::wiwa::make_field(#Member, [](auto& component) -> auto& { return component.Member; }, Meta)
#define WI_COMPONENT(Type, ...) ::wiwa::reflect_component<Type>(#Type, __VA_ARGS__)
