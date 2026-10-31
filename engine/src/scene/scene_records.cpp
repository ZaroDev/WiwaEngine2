#include <pch.h>
#include "scene_records.h"

#include <mutex>

#include <algorithm>

#include <scripting/script_names.h>
#include "reflection/component_registry.h"

#define WI_SCENE_LOG(level, ...) WI_LOG(level, "Scene", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		void warn(ApplyReport* report, std::string message)
		{
			if (report != nullptr)
			{
				report->Warnings.push_back(std::move(message));
			}
		}

		// An unknown name is worth one log line, not one per entity.
		void log_unknown_once(const std::string& what)
		{
			static std::mutex mutex;
			static std::unordered_set<std::string> seen;

			std::lock_guard<std::mutex> lock(mutex);
			if (seen.insert(what).second)
			{
				WI_SCENE_LOG(Warning, "{0} is not known to this engine; it is kept in the scene but has no effect", what);
			}
		}

		ComponentRecord* find_record(std::vector<ComponentRecord>& records, const std::string& name)
		{
			for (ComponentRecord& record : records)
			{
				if (record.Name == name)
				{
					return &record;
				}
			}
			return nullptr;
		}

		void collect_value(const FieldValue& value, std::unordered_set<AssetGuid>& seen, std::vector<AssetRef>& out)
		{
			auto add = [&](const AssetRef& ref)
			{
				if (ref.valid() && seen.insert(ref.resolved()).second)
				{
					out.push_back(ref);
				}
			};

			if (const AssetRef* ref = std::get_if<AssetRef>(&value))
			{
				add(*ref);
			}
			else if (const std::vector<AssetRef>* list = std::get_if<std::vector<AssetRef>>(&value))
			{
				for (const AssetRef& item : *list)
				{
					add(item);
				}
			}
		}

		void collect_entity(const EntityRecord& entity, std::unordered_set<AssetGuid>& seen, std::vector<AssetRef>& out)
		{
			auto collect_component = [&](const ComponentRecord& component)
			{
				if (const std::optional<AssetGuid> script = script_guid_from_component(component.Name))
				{
					const AssetRef ref{ *script, 0 };
					if (seen.insert(ref.resolved()).second)
					{
						out.push_back(ref);
					}
				}
				for (const FieldRecord& field : component.Fields)
				{
					collect_value(field.Value, seen, out);
				}
			};

			for (const ComponentRecord& component : entity.Components)
			{
				collect_component(component);
			}

			for (const PrefabOverride& override : entity.Overrides)
			{
				collect_value(override.Value, seen, out);
				collect_component(override.Added);
				collect_entity(override.Child, seen, out);
			}
		}
	}

	EntityRecord record_from_entity(Scene& scene, entt::entity entity)
	{
		entt::registry& registry = scene.registry();

		EntityRecord record;
		record.Id = registry.get<Id>(entity).Guid;
		const entt::entity parent = registry.get<Hierarchy>(entity).Parent;
		if (parent != entt::null)
		{
			record.Parent = registry.get<Id>(parent).Guid;
		}

		for (const ComponentInfo* info : ComponentRegistry::get().all())
		{
			if (info->Name == "Id" || !info->Has(registry, entity))
			{
				continue;
			}

			ComponentRecord component;
			component.Name = info->Name;
			for (size_t i = 0; i < info->Fields.size(); i++)
			{
				component.Fields.push_back(FieldRecord{ info->Fields[i].Name, info->Get(registry, entity, i) });
			}
			record.Components.push_back(std::move(component));
		}

		if (const UnknownComponents* unknown = registry.try_get<UnknownComponents>(entity))
		{
			for (const ComponentRecord& kept : unknown->Records)
			{
				if (ComponentRecord* known = find_record(record.Components, kept.Name))
				{
					known->Fields.insert(known->Fields.end(), kept.Fields.begin(), kept.Fields.end());
				}
				else
				{
					record.Components.push_back(kept);
				}
			}
		}

		if (const PrefabInstance* instance = registry.try_get<PrefabInstance>(entity))
		{
			record.Overrides = instance->Overrides;
		}
		return record;
	}

	entt::entity apply_record(Scene& scene, const EntityRecord& record, ApplyReport* report)
	{
		if (!record.Id.valid() || scene.find(record.Id) != entt::null)
		{
			warn(report, "entity " + record.Id.to_string() + ": the guid is invalid or already in the scene");
			return entt::null;
		}

		entt::entity parent = entt::null;
		if (record.Parent.valid())
		{
			parent = scene.find(record.Parent);
			if (parent == entt::null)
			{
				warn(report, "entity " + record.Id.to_string() + ": parent " + record.Parent.to_string() + " is not in the scene, put at the top level");
			}
		}

		entt::registry& registry = scene.registry();
		const entt::entity entity = scene.create(record.Id, "", parent);
		std::vector<ComponentRecord> unknown;

		for (const ComponentRecord& component : record.Components)
		{
			if (component.Name == "Id")
			{
				continue;
			}

			const ComponentInfo* info = ComponentRegistry::get().find(component.Name);
			if (info == nullptr)
			{
				unknown.push_back(component);
				if (report != nullptr)
				{
					report->UnknownComponents++;
				}
				log_unknown_once(component.Name);
				continue;
			}

			info->Add(registry, entity);
			ComponentRecord unknownFields;
			unknownFields.Name = component.Name;
			for (const FieldRecord& field : component.Fields)
			{
				const int index = info->field_index(field.Name);
				if (index < 0)
				{
					unknownFields.Fields.push_back(field);
					if (report != nullptr)
					{
						report->UnknownFields++;
					}
					log_unknown_once(component.Name + "." + field.Name);
				}
				else if (!info->Set(registry, entity, static_cast<size_t>(index), field.Value))
				{
					warn(report, "entity " + record.Id.to_string() + ": " + component.Name + "." + field.Name + " got a value it cannot hold, not applied");
				}
			}

			if (!unknownFields.Fields.empty())
			{
				unknown.push_back(std::move(unknownFields));
			}
		}

		if (!unknown.empty())
		{
			registry.emplace<UnknownComponents>(entity, UnknownComponents{ std::move(unknown) });
		}

		if (!record.Overrides.empty())
		{
			registry.get_or_emplace<PrefabInstance>(entity).Overrides = record.Overrides;
		}
		return entity;
	}

	SceneData data_from_scene(Scene& scene, std::string name, bool skipGenerated)
	{
		entt::registry& registry = scene.registry();

		SceneData data;
		data.Name = std::move(name);
		data.Settings = scene.settings();

		std::vector<entt::entity> order;
		std::vector<entt::entity> stack;
		for (const entt::entity root : scene.roots())
		{
			stack.push_back(root);
			while (!stack.empty())
			{
				const entt::entity entity = stack.back();
				stack.pop_back();
				if (skipGenerated && registry.all_of<PrefabChild>(entity))
				{
					continue; // and everything below it is generated too
				}
				order.push_back(entity);

				// children are pushed in reverse so that they come out in sibling order
				const size_t first = stack.size();
				for (entt::entity child = registry.get<Hierarchy>(entity).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
				{
					stack.push_back(child);
				}
				std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(first), stack.end());
			}
		}

		for (const entt::entity entity : order)
		{
			data.Entities.push_back(record_from_entity(scene, entity));
		}
		return data;
	}

	ApplyReport apply_scene_data(Scene& scene, const SceneData& data)
	{
		ApplyReport report;
		scene.set_settings(data.Settings);
		for (const EntityRecord& record : data.Entities)
		{
			apply_record(scene, record, &report);
		}
		return report;
	}

	std::vector<AssetRef> collect_references(const std::vector<EntityRecord>& entities)
	{
		std::unordered_set<AssetGuid> seen;
		std::vector<AssetRef> references;
		for (const EntityRecord& entity : entities)
		{
			collect_entity(entity, seen, references);
		}
		return references;
	}

	std::vector<AssetRef> collect_references(const SceneData& data)
	{
		std::vector<AssetRef> references = collect_references(data.Entities);
		for (const AssetRef& script : data.Settings.Scripts)
		{
			if (script.valid() && std::find(references.begin(), references.end(), script) == references.end())
			{
				references.push_back(script);
			}
		}
		return references;
	}
}
