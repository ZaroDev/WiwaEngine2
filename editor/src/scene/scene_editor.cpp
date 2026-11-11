#include <editor_common.h>
#include "scene_editor.h"

#include <core/engine.h>
#include <scene/prefab_loading.h>
#include <scene/reflection/component_registry.h>

#include "../asset_pipeline/asset_templates.h"
#include "../asset_pipeline/prefab_file.h"
#include "scene_json.h"

#define WI_EDITOR_LOG(level, ...) WI_LOG(level, "SceneEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Components every entity has: they cannot be removed.
		bool is_mandatory(const std::string& component)
		{
			return component == "Id" || component == "Name" || component == "Transform";
		}

		ComponentRecord default_record(const ComponentInfo& info)
		{
			ComponentRecord record;
			record.Name = info.Name;
			for (const FieldDescriptor& field : info.Fields)
			{
				record.Fields.push_back(FieldRecord{ field.Name, field.Default });
			}
			return record;
		}

		void set_record_field(ComponentRecord& component, const std::string& name, const FieldValue& value)
		{
			for (FieldRecord& field : component.Fields)
			{
				if (field.Name == name)
				{
					field.Value = value;
					return;
				}
			}
			component.Fields.push_back(FieldRecord{ name, value });
		}

		ComponentRecord& record_for(std::vector<ComponentRecord>& components, const std::string& name)
		{
			for (ComponentRecord& component : components)
			{
				if (component.Name == name)
				{
					return component;
				}
			}
			components.push_back(ComponentRecord{ name, {} });
			return components.back();
		}

		// The id the live entity of an AddChild override has.
		EntityGuid added_child_id(EntityGuid instanceId, const EntityRecord& child)
		{
			return EntityGuid::derive(instanceId, child.Id.Hi ^ child.Id.Lo);
		}
	}

	SceneEditor::SceneEditor(AssetManager& assets, SceneModule& scenes) : m_Assets(assets), m_Scenes(scenes)
	{
	}

	void SceneEditor::set_scene(Ref<Scene> scene, std::filesystem::path path)
	{
		m_Scene = std::move(scene);
		m_Path = std::move(path);
		m_Dirty = false;
		m_Selected = entt::null;
		m_Pending.clear();
		m_PendingPrefabs.clear();
		m_Changed = false;
		if (!m_Playing)
		{
			// play mode shows a copy: the history of the scene being edited waits for it
			m_History.clear();
			m_HasBaseline = false;
			m_HistoryScene = nullptr;
			m_Scopes = 0;
			m_SavedState.clear();
		}
	}

	EditorDocument SceneEditor::capture_document() const
	{
		EditorDocument document;
		document.Path = m_Path;
		document.Dirty = m_Dirty;
		if (m_Scene != nullptr && m_Selected != entt::null && m_Scene->registry().valid(m_Selected))
		{
			document.Selected = m_Scene->registry().get<Id>(m_Selected).Guid;
		}
		return document;
	}

	void SceneEditor::restore_document(Ref<Scene> scene, const EditorDocument& document)
	{
		m_Scene = std::move(scene);
		m_Path = document.Path;
		m_Dirty = document.Dirty;
		m_Pending.clear();
		m_PendingPrefabs.clear();
		m_Selected = (m_Scene != nullptr && document.Selected.valid()) ? m_Scene->find(document.Selected) : entt::entity{ entt::null };
		m_Changed = false;
		m_Scopes = 0;
		if (m_Scene.get() != m_HistoryScene || !m_HasBaseline)
		{
			m_History.clear();
			m_HasBaseline = false;
			m_HistoryScene = nullptr;
			m_SavedState.clear();
		}
	}

	void SceneEditor::select(entt::entity entity)
	{
		m_Selected = (m_Scene != nullptr && m_Scene->registry().valid(entity)) ? entity : entt::entity{ entt::null };
	}

	void SceneEditor::update()
	{
		if (m_Opening != nullptr && m_Opening->finished())
		{
			Ref<JobProgress> opened = std::move(m_Opening);
			m_Opening.reset();
			if (opened->succeeded())
			{
				set_scene(m_Scenes.get_active(), m_OpenPath);
			}
			else
			{
				WI_EDITOR_LOG(Error, "Could not open {0}: {1}", m_OpenPath, opened->stage());
			}
		}
		else if (m_Follow && m_Opening == nullptr && m_Scenes.get_active() != m_Scene)
		{
			set_scene(m_Scenes.get_active());
		}

		if (m_Scene == nullptr)
		{
			return;
		}

		for (size_t i = 0; i < m_Pending.size();)
		{
			PendingInstance& pending = m_Pending[i];
			const entt::entity root = m_Scene->find(pending.Root);
			const LoadState state = pending.Prefab.state();
			if (root == entt::null || state == LoadState::Failed)
			{
				if (root != entt::null)
				{
					WI_EDITOR_LOG(Error, "The prefab of an instance could not be loaded");
				}
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else if (state == LoadState::Ready)
			{
				if (nested_state(*pending.Prefab.get()) == LoadState::Loading)
				{
					i++; // the prefabs it instantiates are still loading
					continue;
				}
				expand_instance(root, *pending.Prefab.get());
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}

		for (size_t i = 0; i < m_PendingPrefabs.size();)
		{
			const PendingPrefab pending = m_PendingPrefabs[i];
			const entt::entity original = m_Scene->find(pending.Entity);
			bool done = true;
			if (original == entt::null)
			{
				// the entity was deleted while the file was being imported: nothing to replace
			}
			else if (m_Playing)
			{
				m_Message = "\"" + m_Scene->registry().get<Name>(original).Value + "\" was not made a prefab: the scene started playing";
				WI_EDITOR_LOG(Warning, "{0}", m_Message);
			}
			else if (std::chrono::steady_clock::now() - pending.Started > m_PrefabTimeout)
			{
				m_Message = "\"" + m_Scene->registry().get<Name>(original).Value + "\" was not made a prefab: " + pending.File.filename().string() + " was not imported in time (see the console)";
				WI_EDITOR_LOG(Warning, "{0}", m_Message);
			}
			else if (const std::optional<AssetRef> prefab = m_Assets.find_asset(pending.File.string()))
			{
				finish_prefab(original, *prefab);
			}
			else
			{
				done = false;
			}

			if (done)
			{
				m_PendingPrefabs.erase(m_PendingPrefabs.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}

		if (m_Selected != entt::null && !m_Scene->registry().valid(m_Selected))
		{
			m_Selected = entt::null;
		}

		settle_history();
	}

	// ---- undo and redo ----

	std::string SceneEditor::snapshot()
	{
		return scene_to_json(to_scene_data()).dump();
	}

	// Takes the baseline once the scene is quiet, and turns an edit that has settled into an undo step.
	void SceneEditor::settle_history()
	{
		if (!can_edit_history() || m_Scopes > 0)
		{
			return;
		}
		if (!m_HasBaseline)
		{
			m_Baseline = snapshot();
			m_HasBaseline = true;
			m_HistoryScene = m_Scene.get();
			m_Changed = false;
			if (m_SavedState.empty() && !m_Dirty)
			{
				m_SavedState = m_Baseline;
			}
			return;
		}
		if (!m_Changed)
		{
			return;
		}
		std::string current = snapshot();
		if (current != m_Baseline)
		{
			m_History.push(m_ChangeLabel, std::move(m_Baseline));
			m_Baseline = std::move(current);
		}
		m_Changed = false;
	}

	bool SceneEditor::restore_snapshot(const std::string& state)
	{
		SceneJson json = SceneJson::parse(state, nullptr, false);
		if (json.is_discarded())
		{
			return false;
		}
		SceneJsonResult parsed = scene_from_json(json);
		if (!parsed.Scene)
		{
			WI_EDITOR_LOG(Error, "Undo could not read the snapshot: {0}", parsed.Error);
			return false;
		}

		EntityGuid selected{};
		if (m_Selected != entt::null && m_Scene->registry().valid(m_Selected))
		{
			selected = m_Scene->registry().get<Id>(m_Selected).Guid;
		}

		const std::vector<entt::entity> roots = m_Scene->roots();
		for (const entt::entity root : roots)
		{
			m_Scene->destroy(root);
		}
		apply_scene_data(*m_Scene, *parsed.Scene);

		// the instances made their nodes again from the prefab; they appear in update() once the prefabs are there
		m_Pending.clear();
		entt::registry& registry = m_Scene->registry();
		for (const entt::entity entity : registry.view<PrefabInstance>())
		{
			queue_expansion(entity);
		}
		m_Scene->update_world_transforms();
		m_Selected = selected.valid() ? m_Scene->find(selected) : entt::entity{ entt::null };
		return true;
	}

	bool SceneEditor::undo()
	{
		settle_history(); // an edit made this frame counts first
		if (!can_undo())
		{
			return false;
		}
		const std::string current = m_Baseline;
		const std::optional<std::string> state = m_History.undo(current);
		if (!state || !restore_snapshot(*state))
		{
			return false;
		}
		m_Baseline = *state;
		m_Changed = false;
		m_Dirty = m_Baseline != m_SavedState;
		return true;
	}

	bool SceneEditor::redo()
	{
		settle_history();
		if (!can_redo())
		{
			return false;
		}
		const std::string current = m_Baseline;
		const std::optional<std::string> state = m_History.redo(current);
		if (!state || !restore_snapshot(*state))
		{
			return false;
		}
		m_Baseline = *state;
		m_Changed = false;
		m_Dirty = m_Baseline != m_SavedState;
		return true;
	}

	bool SceneEditor::jump_history(size_t target)
	{
		if (target > m_History.undo_labels().size() + m_History.redo_labels().size())
		{
			return false;
		}
		bool moved = false;
		m_IgnorePending = true; // each restore queues the prefab instances again; the next step does not wait for them
		while (m_History.undo_labels().size() > target && undo()) { moved = true; }
		while (m_History.undo_labels().size() < target && redo()) { moved = true; }
		m_IgnorePending = false;
		return moved;
	}

	void SceneEditor::new_scene()
	{
		if (m_Playing)
		{
			WI_EDITOR_LOG(Warning, "A new scene cannot be made while the scene is playing");
			return;
		}

		Ref<Scene> scene = CreateRef<Scene>();
		// a new scene starts at the rate the project chose (Settings, Performance); 0 keeps the built-in default
		if (const u32 rate = Engine::get().project().FixedRate; rate != 0)
		{
			SceneSettings settings = scene->settings();
			settings.Physics.FixedRateHz = rate;
			scene->set_settings(settings);
		}
		m_Scenes.set_active(scene);
		set_scene(m_Scenes.get_active());
	}

	void SceneEditor::open(const std::string& sourcePath)
	{
		if (m_Playing)
		{
			WI_EDITOR_LOG(Warning, "A scene cannot be opened while the scene is playing");
			return;
		}

		m_OpenPath = sourcePath;
		m_Opening = m_Scenes.load_async(std::string_view(sourcePath));
	}

	void SceneEditor::edit_settings(const SceneSettings& settings)
	{
		const u64 revision = m_Scene->settings_revision();
		m_Scene->set_settings(settings);
		if (m_Scene->settings_revision() != revision)
		{
			mark_dirty("Edit scene settings");
		}
	}

	SceneData SceneEditor::to_scene_data()
	{
		const std::string name = m_Path.empty() ? "untitled" : m_Path.stem().string();
		return data_from_scene(*m_Scene, name, true);
	}

	bool SceneEditor::save()
	{
		if (m_Playing)
		{
			WI_EDITOR_LOG(Warning, "The scene cannot be saved while it is playing");
			return false;
		}
		return !m_Path.empty() && save_as(m_Path);
	}

	bool SceneEditor::save_as(const std::filesystem::path& file)
	{
		if (m_Playing)
		{
			WI_EDITOR_LOG(Warning, "The scene cannot be saved while it is playing");
			return false;
		}

		const std::filesystem::path previous = m_Path;
		m_Path = file; // the name in the file is the file's
		const std::string text = scene_to_json(to_scene_data()).dump(2);

		std::error_code error;
		if (file.has_parent_path())
		{
			std::filesystem::create_directories(file.parent_path(), error);
		}

		std::filesystem::path temp = file;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			out << text;
			if (!out)
			{
				m_Path = previous;
				WI_EDITOR_LOG(Error, "Could not write {0}", temp.string());
				return false;
			}
		}

		std::filesystem::rename(temp, file, error);
		if (error)
		{
			m_Path = previous;
			WI_EDITOR_LOG(Error, "Could not replace {0}: {1}", file.string(), error.message());
			return false;
		}

		m_Dirty = false;
		m_SavedState = snapshot(); // what was written, even if an edit has not settled into a step yet
		return true;
	}

	// ---- structure ----

	std::optional<SceneEditor::Generated> SceneEditor::generated_of(entt::entity entity) const
	{
		const PrefabChild* tag = m_Scene->registry().try_get<PrefabChild>(entity);
		if (tag == nullptr)
		{
			return std::nullopt;
		}

		const entt::entity root = m_Scene->find(tag->Instance);
		if (root == entt::null || !m_Scene->registry().all_of<PrefabInstance>(root))
		{
			return std::nullopt;
		}
		return Generated{ root, tag->NodeId };
	}

	SceneEditor::Kind SceneEditor::kind_of(entt::entity entity) const
	{
		const PrefabChild* tag = m_Scene->registry().try_get<PrefabChild>(entity);
		if (tag == nullptr)
		{
			return Kind::Authored;
		}
		return tag->NodeId == ADDED_CHILD_NODE ? Kind::AddedChild : Kind::PrefabNode;
	}

	PrefabOverride* SceneEditor::find_added_override(PrefabInstance& instance, EntityGuid rootId, EntityGuid childId)
	{
		for (PrefabOverride& override : instance.Overrides)
		{
			if (override.Kind == OverrideKind::AddChild && added_child_id(rootId, override.Child) == childId)
			{
				return &override;
			}
		}
		return nullptr;
	}

	entt::entity SceneEditor::create_entity(entt::entity parent)
	{
		entt::registry& registry = m_Scene->registry();
		if (parent == entt::null || kind_of(parent) == Kind::Authored)
		{
			const entt::entity entity = m_Scene->create("Entity", parent);
			mark_dirty("Create entity");
			return entity;
		}

		const std::optional<Generated> generated = generated_of(parent);
		if (!generated || generated->NodeId == ADDED_CHILD_NODE)
		{
			return entt::null; // nothing can go under an added child
		}

		const EntityGuid rootId = registry.get<Id>(generated->Root).Guid;
		PrefabOverride add;
		add.Kind = OverrideKind::AddChild;
		add.NodeId = generated->NodeId;
		add.Child.Id = EntityGuid::generate();
		add.Child.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Entity")) } } });

		const entt::entity entity = m_Scene->create(added_child_id(rootId, add.Child), "Entity", parent);
		registry.emplace<PrefabChild>(entity, PrefabChild{ rootId, ADDED_CHILD_NODE });
		registry.get<PrefabInstance>(generated->Root).Overrides.push_back(std::move(add));
		mark_dirty("Create entity");
		return entity;
	}

	bool SceneEditor::delete_entity(entt::entity entity)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity))
		{
			return false;
		}

		entt::registry& registry = m_Scene->registry();
		if (kind_of(entity) != Kind::Authored)
		{
			const std::optional<Generated> generated = generated_of(entity);
			if (!generated)
			{
				return false;
			}

			PrefabInstance& instance = registry.get<PrefabInstance>(generated->Root);
			if (generated->NodeId == ADDED_CHILD_NODE)
			{
				const EntityGuid rootId = registry.get<Id>(generated->Root).Guid;
				PrefabOverride* added = find_added_override(instance, rootId, registry.get<Id>(entity).Guid);
				if (added != nullptr)
				{
					instance.Overrides.erase(instance.Overrides.begin() + (added - instance.Overrides.data()));
				}
			}
			else
			{
				PrefabOverride remove;
				remove.Kind = OverrideKind::RemoveNode;
				remove.NodeId = generated->NodeId;
				instance.Overrides.push_back(std::move(remove));
			}
		}

		m_Scene->destroy(entity);
		if (m_Selected != entt::null && !registry.valid(m_Selected))
		{
			m_Selected = entt::null;
		}
		mark_dirty("Delete entity");
		return true;
	}

	entt::entity SceneEditor::duplicate_entity(entt::entity entity)
	{
		if (m_Playing || m_Scene == nullptr || !m_Scene->registry().valid(entity) || kind_of(entity) != Kind::Authored)
		{
			return entt::null;
		}

		entt::registry& registry = m_Scene->registry();

		// the entity and the authored entities below it, parents first: the nodes a prefab instance generated are not copied, the copy of
		// the instance makes its own
		std::vector<entt::entity> subtree;
		std::vector<entt::entity> stack{ entity };
		while (!stack.empty())
		{
			const entt::entity current = stack.back();
			stack.pop_back();
			subtree.push_back(current);

			std::vector<entt::entity> children;
			for (entt::entity child = registry.get<Hierarchy>(current).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
			{
				if (!registry.all_of<PrefabChild>(child))
				{
					children.push_back(child);
				}
			}
			stack.insert(stack.end(), children.rbegin(), children.rend());
		}

		std::unordered_map<EntityGuid, EntityGuid> fresh;
		std::vector<entt::entity> copies;
		for (const entt::entity current : subtree)
		{
			EntityRecord record = record_from_entity(*m_Scene, current);
			const EntityGuid id = EntityGuid::generate();
			fresh.emplace(record.Id, id);
			record.Id = id;
			if (current != entity)
			{
				record.Parent = fresh.at(record.Parent); // the top copy stays under the parent of the original
			}

			const entt::entity created = apply_record(*m_Scene, record);
			if (created == entt::null)
			{
				break;
			}
			copies.push_back(created);
			if (registry.all_of<PrefabInstance>(created))
			{
				queue_expansion(created);
			}
		}
		if (copies.empty())
		{
			return entt::null;
		}

		registry.get<Name>(copies.front()).Value += " (copy)";
		m_Scene->move_after(copies.front(), entity);
		mark_dirty("Duplicate entity");
		select(copies.front());
		return copies.front();
	}

	std::vector<entt::entity> SceneEditor::authored_subtree(entt::entity entity) const
	{
		const entt::registry& registry = m_Scene->registry();
		std::vector<entt::entity> subtree;
		std::vector<entt::entity> stack{ entity };
		while (!stack.empty())
		{
			const entt::entity current = stack.back();
			stack.pop_back();
			subtree.push_back(current);

			std::vector<entt::entity> children;
			for (entt::entity child = registry.get<Hierarchy>(current).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
			{
				if (!registry.all_of<PrefabChild>(child))
				{
					children.push_back(child);
				}
			}
			stack.insert(stack.end(), children.rbegin(), children.rend());
		}
		return subtree;
	}

	bool SceneEditor::copy_entity(entt::entity entity)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity) || kind_of(entity) != Kind::Authored)
		{
			return false;
		}

		m_Clipboard.clear();
		for (const entt::entity current : authored_subtree(entity))
		{
			m_Clipboard.push_back(record_from_entity(*m_Scene, current));
		}
		return true;
	}

	entt::entity SceneEditor::paste_entity(entt::entity parent)
	{
		if (m_Playing || m_Scene == nullptr || m_Clipboard.empty())
		{
			return entt::null;
		}
		if (parent != entt::null && (!m_Scene->registry().valid(parent) || kind_of(parent) != Kind::Authored))
		{
			return entt::null;
		}

		const EntityGuid parentId = parent == entt::null ? EntityGuid{} : m_Scene->registry().get<Id>(parent).Guid;
		std::unordered_map<EntityGuid, EntityGuid> fresh;
		entt::entity top = entt::null;
		for (const EntityRecord& original : m_Clipboard)
		{
			EntityRecord record = original;
			const EntityGuid id = EntityGuid::generate();
			fresh.emplace(record.Id, id);
			record.Id = id;
			record.Parent = top == entt::null ? parentId : fresh.at(original.Parent);

			const entt::entity created = apply_record(*m_Scene, record);
			if (created == entt::null)
			{
				break;
			}
			if (top == entt::null)
			{
				top = created;
			}
			if (m_Scene->registry().all_of<PrefabInstance>(created))
			{
				queue_expansion(created);
			}
		}
		if (top == entt::null)
		{
			return entt::null;
		}

		mark_dirty("Paste entity");
		select(top);
		return top;
	}

	bool SceneEditor::rename_entity(entt::entity entity, const std::string& name)
	{
		if (m_Playing || m_Scene == nullptr || !m_Scene->registry().valid(entity) || kind_of(entity) != Kind::Authored)
		{
			return false;
		}

		Name& current = m_Scene->registry().get<Name>(entity);
		if (current.Value != name)
		{
			current.Value = name;
			mark_dirty("Rename entity");
		}
		return true;
	}

	bool SceneEditor::reparent(entt::entity child, entt::entity parent)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(child) || kind_of(child) != Kind::Authored)
		{
			return false;
		}
		if (parent != entt::null && (!m_Scene->registry().valid(parent) || kind_of(parent) != Kind::Authored))
		{
			return false;
		}

		if (!m_Scene->set_parent(child, parent))
		{
			return false;
		}
		mark_dirty("Reparent entity");
		return true;
	}

	// ---- prefab instances ----

	Handle<PrefabData> SceneEditor::prefab_handle(const AssetRef& prefab)
	{
		const AssetGuid key = prefab.resolved();
		auto found = m_Prefabs.find(key);
		if (found == m_Prefabs.end())
		{
			found = m_Prefabs.emplace(key, m_Assets.load<PrefabData>(prefab)).first;
		}
		return found->second;
	}

	const PrefabData* SceneEditor::prefab_of(entt::entity root)
	{
		const PrefabInstance* instance = m_Scene->registry().try_get<PrefabInstance>(root);
		if (instance == nullptr || !instance->Prefab.valid())
		{
			return nullptr;
		}
		return prefab_handle(instance->Prefab).get();
	}

	LoadState SceneEditor::nested_state(const PrefabData& prefab)
	{
		return nested_prefabs_state(prefab, [this](const AssetRef& ref) { return prefab_handle(ref); });
	}

	PrefabResolver SceneEditor::resolver()
	{
		return [this](const AssetRef& ref) -> const PrefabData* { return prefab_handle(ref).get(); };
	}

	void SceneEditor::expand_instance(entt::entity root, const PrefabData& prefab)
	{
		const EntityRecord rootRecord = record_from_entity(*m_Scene, root);
		const ExpandResult expanded = expand_prefab(rootRecord, prefab, resolver());
		for (const std::string& warning : expanded.Warnings)
		{
			WI_EDITOR_LOG(Warning, "{0}", warning);
		}

		for (size_t i = 1; i < expanded.Entities.size(); i++)
		{
			const entt::entity entity = apply_record(*m_Scene, expanded.Entities[i]);
			if (entity != entt::null)
			{
				m_Scene->registry().emplace<PrefabChild>(entity, PrefabChild{ rootRecord.Id, expanded.NodeIds[i], expanded.NestedRoots[i] != 0 });
			}
		}
		m_Scene->update_world_transforms();
	}

	void SceneEditor::queue_expansion(entt::entity root)
	{
		const PrefabInstance& instance = m_Scene->registry().get<PrefabInstance>(root);
		m_Pending.push_back(PendingInstance{ m_Scene->registry().get<Id>(root).Guid, prefab_handle(instance.Prefab) });
	}

	entt::entity SceneEditor::add_prefab(AssetRef prefab, entt::entity parent)
	{
		if (m_Scene == nullptr || !prefab.valid() || (parent != entt::null && (!m_Scene->registry().valid(parent) || kind_of(parent) != Kind::Authored)))
		{
			return entt::null;
		}

		const entt::entity root = m_Scene->create("Prefab instance", parent);
		m_Scene->registry().emplace<PrefabInstance>(root, PrefabInstance{ prefab, {} });
		queue_expansion(root);
		mark_dirty("Add prefab");
		return root;
	}

	std::string SceneEditor::make_prefab(entt::entity entity, const std::filesystem::path& folder)
	{
		if (m_Playing)
		{
			return "Stop the scene first";
		}
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity))
		{
			return "There is no entity to make a prefab of";
		}
		if (kind_of(entity) != Kind::Authored)
		{
			return "Only entities of the scene itself can become a prefab, not the ones a prefab made";
		}

		entt::registry& registry = m_Scene->registry();
		const EntityGuid guid = registry.get<Id>(entity).Guid;
		for (const PendingPrefab& pending : m_PendingPrefabs)
		{
			if (pending.Entity == guid)
			{
				return "That entity is already becoming a prefab";
			}
		}

		// the entity and everything below it, parents first
		std::vector<entt::entity> subtree;
		std::vector<entt::entity> stack{ entity };
		while (!stack.empty())
		{
			const entt::entity current = stack.back();
			stack.pop_back();
			if (registry.all_of<PrefabChild>(current))
			{
				return "Only entities of the scene itself can become a prefab, not the ones a prefab made";
			}
			subtree.push_back(current);
			if (registry.all_of<PrefabInstance>(current))
			{
				continue; // an instance goes into the prefab as the link and its overrides: its nodes are generated again from the prefab
			}

			std::vector<entt::entity> children;
			for (entt::entity child = registry.get<Hierarchy>(current).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
			{
				children.push_back(child);
			}
			stack.insert(stack.end(), children.rbegin(), children.rend());
		}

		std::vector<EntityRecord> records;
		for (const entt::entity member : subtree)
		{
			records.push_back(record_from_entity(*m_Scene, member));
		}
		// the instance takes the place and Transform of the entity, so the prefab's own top node sits at its origin
		records.front().Parent = EntityGuid{};
		for (ComponentRecord& component : records.front().Components)
		{
			if (component.Name == "Transform")
			{
				set_record_field(component, "Position", FieldValue(glm::vec3(0.0f)));
				set_record_field(component, "Rotation", FieldValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
				set_record_field(component, "Scale", FieldValue(glm::vec3(1.0f)));
			}
		}

		const std::string stem = sanitize_file_stem(registry.get<Name>(entity).Value);

		std::error_code error;
		std::filesystem::path file = folder / (stem + ".wprefab");
		for (u32 number = 2; std::filesystem::exists(file, error); number++)
		{
			file = folder / (stem + " " + std::to_string(number) + ".wprefab");
		}

		if (!write_prefab_file(file, records, stem))
		{
			return "Could not write " + file.string() + " (see the log)";
		}

		m_PendingPrefabs.push_back(PendingPrefab{ guid, file, std::chrono::steady_clock::now() });
		return {};
	}

	void SceneEditor::finish_prefab(entt::entity original, const AssetRef& prefab)
	{
		entt::registry& registry = m_Scene->registry();
		const std::string name = registry.get<Name>(original).Value;
		const Transform transform = registry.get<Transform>(original);
		const entt::entity parent = registry.get<Hierarchy>(original).Parent;

		bool selectInstance = false;
		for (entt::entity ancestor = m_Selected; ancestor != entt::null; ancestor = registry.get<Hierarchy>(ancestor).Parent)
		{
			selectInstance = selectInstance || ancestor == original;
		}

		const entt::entity instance = add_prefab(prefab, parent);
		if (instance == entt::null)
		{
			m_Message = "\"" + name + "\" was not made a prefab: the instance could not be added";
			WI_EDITOR_LOG(Warning, "{0}", m_Message);
			return;
		}

		registry.get<Name>(instance).Value = name;
		registry.get<Transform>(instance) = transform;
		m_Scene->move_after(instance, original);
		delete_entity(original);
		if (selectInstance)
		{
			select(instance);
		}
	}

	bool SceneEditor::unpack(entt::entity instance)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(instance) || !m_Scene->registry().all_of<PrefabInstance>(instance))
		{
			return false;
		}

		entt::registry& registry = m_Scene->registry();
		const EntityGuid rootId = registry.get<Id>(instance).Guid;
		std::vector<entt::entity> generated;
		for (const entt::entity entity : registry.view<PrefabChild>())
		{
			if (registry.get<PrefabChild>(entity).Instance == rootId)
			{
				generated.push_back(entity);
			}
		}
		for (const entt::entity entity : generated)
		{
			registry.remove<PrefabChild>(entity);
		}

		registry.remove<PrefabInstance>(instance);
		mark_dirty("Unpack prefab");
		return true;
	}

	bool SceneEditor::revert_instance(entt::entity instance)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(instance) || !m_Scene->registry().all_of<PrefabInstance>(instance))
		{
			return false;
		}

		entt::registry& registry = m_Scene->registry();
		const EntityGuid rootId = registry.get<Id>(instance).Guid;
		std::vector<entt::entity> generated;
		for (const entt::entity entity : registry.view<PrefabChild>())
		{
			if (registry.get<PrefabChild>(entity).Instance == rootId)
			{
				generated.push_back(entity);
			}
		}
		for (const entt::entity entity : generated)
		{
			if (registry.valid(entity))
			{
				m_Scene->destroy(entity);
			}
		}

		registry.get<PrefabInstance>(instance).Overrides.clear();
		queue_expansion(instance);
		if (m_Selected != entt::null && !registry.valid(m_Selected))
		{
			m_Selected = entt::null;
		}
		mark_dirty("Revert prefab instance");
		return true;
	}

	// ---- components ----

	bool SceneEditor::set_field(entt::entity entity, const std::string& component, const std::string& field, const FieldValue& value)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity) || component == "Id" || component == "PrefabInstance")
		{
			return false;
		}

		const ComponentInfo* info = ComponentRegistry::get().find(component);
		const int index = info != nullptr ? info->field_index(field) : -1;
		entt::registry& registry = m_Scene->registry();
		if (index < 0 || !info->Has(registry, entity) || !info->Set(registry, entity, static_cast<size_t>(index), value))
		{
			return false;
		}

		if (const std::optional<Generated> generated = generated_of(entity))
		{
			PrefabInstance& instance = registry.get<PrefabInstance>(generated->Root);
			if (generated->NodeId == ADDED_CHILD_NODE)
			{
				const EntityGuid rootId = registry.get<Id>(generated->Root).Guid;
				if (PrefabOverride* added = find_added_override(instance, rootId, registry.get<Id>(entity).Guid))
				{
					set_record_field(record_for(added->Child.Components, component), field, value);
				}
			}
			else
			{
				PrefabOverride* existing = nullptr;
				for (PrefabOverride& override : instance.Overrides)
				{
					if (override.Kind == OverrideKind::SetField && override.NodeId == generated->NodeId && override.Component == component && override.Field == field)
					{
						existing = &override;
					}
				}

				if (existing != nullptr)
				{
					existing->Value = value;
				}
				else
				{
					PrefabOverride set;
					set.Kind = OverrideKind::SetField;
					set.NodeId = generated->NodeId;
					set.Component = component;
					set.Field = field;
					set.Value = value;
					instance.Overrides.push_back(std::move(set));
				}
			}
		}

		mark_dirty("Edit " + component + "." + field);
		return true;
	}

	bool SceneEditor::add_component(entt::entity entity, const std::string& component)
	{
		const ComponentInfo* info = ComponentRegistry::get().find(component);
		if (m_Scene == nullptr || info == nullptr || !m_Scene->registry().valid(entity) || component == "Id" || component == "PrefabInstance")
		{
			return false;
		}

		entt::registry& registry = m_Scene->registry();
		if (info->Has(registry, entity))
		{
			return false;
		}
		info->Add(registry, entity);

		if (const std::optional<Generated> generated = generated_of(entity))
		{
			PrefabInstance& instance = registry.get<PrefabInstance>(generated->Root);
			if (generated->NodeId == ADDED_CHILD_NODE)
			{
				const EntityGuid rootId = registry.get<Id>(generated->Root).Guid;
				if (PrefabOverride* added = find_added_override(instance, rootId, registry.get<Id>(entity).Guid))
				{
					record_for(added->Child.Components, component) = default_record(*info);
				}
			}
			else
			{
				// taking a RemoveComponent back, or adding the component
				std::erase_if(instance.Overrides, [&](const PrefabOverride& override) { return override.Kind == OverrideKind::RemoveComponent && override.NodeId == generated->NodeId && override.Component == component; });
				PrefabOverride add;
				add.Kind = OverrideKind::AddComponent;
				add.NodeId = generated->NodeId;
				add.Added = default_record(*info);
				instance.Overrides.push_back(std::move(add));
			}
		}

		mark_dirty("Add component " + component);
		return true;
	}

	bool SceneEditor::remove_component(entt::entity entity, const std::string& component)
	{
		const ComponentInfo* info = ComponentRegistry::get().find(component);
		if (m_Scene == nullptr || info == nullptr || !m_Scene->registry().valid(entity) || is_mandatory(component) || component == "PrefabInstance")
		{
			return false;
		}

		entt::registry& registry = m_Scene->registry();
		if (!info->Has(registry, entity))
		{
			return false;
		}
		info->Remove(registry, entity);

		if (const std::optional<Generated> generated = generated_of(entity))
		{
			PrefabInstance& instance = registry.get<PrefabInstance>(generated->Root);
			if (generated->NodeId == ADDED_CHILD_NODE)
			{
				const EntityGuid rootId = registry.get<Id>(generated->Root).Guid;
				if (PrefabOverride* added = find_added_override(instance, rootId, registry.get<Id>(entity).Guid))
				{
					std::erase_if(added->Child.Components, [&](const ComponentRecord& record) { return record.Name == component; });
				}
			}
			else
			{
				// what was added or set on this component earlier is moot now
				std::erase_if(instance.Overrides, [&](const PrefabOverride& override)
					{
						return override.NodeId == generated->NodeId
							&& ((override.Kind == OverrideKind::AddComponent && override.Added.Name == component)
								|| (override.Kind == OverrideKind::SetField && override.Component == component));
					});

				PrefabOverride remove;
				remove.Kind = OverrideKind::RemoveComponent;
				remove.NodeId = generated->NodeId;
				remove.Component = component;
				instance.Overrides.push_back(std::move(remove));
			}
		}

		mark_dirty("Remove component " + component);
		return true;
	}

	bool SceneEditor::is_overridden(entt::entity entity, const std::string& component, const std::string& field) const
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity))
		{
			return false;
		}

		const std::optional<Generated> generated = generated_of(entity);
		if (!generated || generated->NodeId == ADDED_CHILD_NODE)
		{
			return false;
		}

		for (const PrefabOverride& override : m_Scene->registry().get<PrefabInstance>(generated->Root).Overrides)
		{
			if (override.Kind == OverrideKind::SetField && override.NodeId == generated->NodeId && override.Component == component && override.Field == field)
			{
				return true;
			}
		}
		return false;
	}

	bool SceneEditor::revert_field(entt::entity entity, const std::string& component, const std::string& field)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(entity))
		{
			return false;
		}

		const std::optional<Generated> generated = generated_of(entity);
		if (!generated || generated->NodeId == ADDED_CHILD_NODE)
		{
			return false;
		}

		const PrefabData* prefab = prefab_of(generated->Root);
		if (prefab == nullptr)
		{
			return false; // the prefab is not loaded: its value is not known
		}

		entt::registry& registry = m_Scene->registry();
		EntityRecord holder; // revert_field works on the record of the root; the overrides live in the component
		holder.Overrides = std::move(registry.get<PrefabInstance>(generated->Root).Overrides);
		wiwa::revert_field(holder, generated->NodeId, component, field);
		registry.get<PrefabInstance>(generated->Root).Overrides = std::move(holder.Overrides);

		if (const std::optional<FieldValue> original = prefab_value(*prefab, generated->NodeId, component, field, resolver()))
		{
			const ComponentInfo* info = ComponentRegistry::get().find(component);
			const int index = info != nullptr ? info->field_index(field) : -1;
			if (index >= 0)
			{
				info->Set(registry, entity, static_cast<size_t>(index), *original);
			}
		}

		mark_dirty("Revert " + component + "." + field);
		return true;
	}
}
