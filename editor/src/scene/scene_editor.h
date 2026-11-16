#pragma once

#include <chrono>
#include <utility>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <scene/prefab_expander.h>
#include <scene/scene_module.h>
#include <scene/scene_records.h>

#include "undo_stack.h"

namespace wiwa
{
	// What the editor keeps of the document besides the scene: where it is saved, whether it changed, and what is selected (by guid, so it
	// can be found again in another copy of the scene). Play mode puts it back when it stops.
	struct EditorDocument
	{
		std::filesystem::path Path{};
		bool Dirty = false;
		EntityGuid Selected{};
	};

	// The scene being edited and everything that changes it: what the Hierarchy and Inspector panels call, without any ImGui in
	// here. It edits a Scene (the active one of the SceneModule, in the editor) and keeps track of the file it came from, whether
	// it changed, and the selection. Main thread.
	//
	// Entities of a linked prefab instance are not edited directly: a change to one is recorded as an override on the instance root
	// (the instance's record is what the file holds) and applied to the live entity as well, so nothing has to be expanded again.
	class SceneEditor
	{
	public:
		// What an entity is, as far as editing goes.
		enum class Kind
		{
			Authored,   // written in the scene file
			PrefabNode, // generated from a prefab node; edits become SetField/AddComponent/RemoveComponent/RemoveNode overrides
			AddedChild, // made by an AddChild override
		};

		SceneEditor(AssetManager& assets, SceneModule& scenes);

		// ---- the document ----
		// Adopts a scene (clears the selection and the dirty flag).
		void set_scene(Ref<Scene> scene, std::filesystem::path path = {});
		Ref<Scene> get_scene() const { return m_Scene; }
		Scene& scene() { return *m_Scene; }
		const std::filesystem::path& path() const { return m_Path; }
		bool dirty() const { return m_Dirty; }

		EditorDocument capture_document() const;
		// Adopts the scene with the path, dirty flag and selection captured earlier (the selected entity if the scene has it).
		void restore_document(Ref<Scene> scene, const EditorDocument& document);

		// While a scene is running (play mode) the document cannot be replaced or written: new_scene, open, save and save_as do nothing.
		void set_playing(bool playing) { m_Playing = playing; }
		bool playing() const { return m_Playing; }

		// When on, the editor adopts whatever scene the SceneModule makes active (a scene loaded with --scene, for instance).
		void set_follow_module(bool follow) { m_Follow = follow; }

		// Every frame: finishes an open, expands the prefab instances whose prefab has loaded.
		void update();
		bool has_pending() const { return !m_Pending.empty(); }

		void new_scene();
		// Loads the scene by the path of its source file in the asset catalog ("Assets/scenes/level.wscene") without stalling; update()
		// adopts it once it is there.
		void open(const std::string& sourcePath);
		bool is_opening() const { return m_Opening != nullptr; }
		Ref<JobProgress> get_open_progress() const { return m_Opening; }

		// Writes the .wscene (JSON) next to the temp file and renames it over the target. The file watcher imports it.
		bool save();
		bool save_as(const std::filesystem::path& file);
		// What save() writes: the authored entities, without the ones a prefab instance generates.
		SceneData to_scene_data();

		// ---- undo and redo ----
		// Every edit that settles (update() sees it; edits made while a scope is open count as one) becomes one step. The document is restored
		// from a snapshot, so every kind of edit is covered the same way. Not while playing, opening a scene or expanding a prefab instance.
		bool can_edit_history() const { return !m_Playing && m_Scene != nullptr && m_Opening == nullptr && (m_IgnorePending || (m_Pending.empty() && m_PendingPrefabs.empty())); }
		// not while a widget is being dragged: the edits in flight are not a step yet
		bool can_undo() const { return can_edit_history() && m_Scopes == 0 && m_History.can_undo(); }
		bool can_redo() const { return can_edit_history() && m_Scopes == 0 && m_History.can_redo(); }
		bool undo();
		bool redo();
		const UndoStack& history() const { return m_History; }
		// Steps the document to the state with `target` steps applied (0: as it was opened; the undo labels, then the redo labels).
		bool jump_history(size_t target);
		// While a scope is open (a widget being dragged or typed in) the edits count as one step, committed when the last scope closes.
		void begin_edit_scope() { m_Scopes++; }
		void end_edit_scope() { if (m_Scopes > 0) { m_Scopes--; } }

		// ---- the scene's look ----
		const SceneSettings& settings() const { return m_Scene->settings(); }
		// Applies new settings (sanitized by the scene); marks the scene changed when they differ from the current ones.
		void edit_settings(const SceneSettings& settings);

		// ---- selection ----
		entt::entity selected() const { return m_Selected; }
		void select(entt::entity entity);

		// ---- structure ----
		Kind kind_of(entt::entity entity) const;

		// A new entity under parent (entt::null for the top level). Under a prefab node it is an AddChild override; under an added
		// child, null (not supported).
		entt::entity create_entity(entt::entity parent);
		bool delete_entity(entt::entity entity);
		// A copy of an authored entity and the authored entities below it, with guids of their own, named "<name> (copy)", right after the
		// original, and selected. A copied prefab instance generates its own nodes (they appear in update()). entt::null for anything
		// else, and while the scene plays.
		entt::entity duplicate_entity(entt::entity entity);
		// An editor-wide clipboard of an authored entity and the authored entities below it (it outlives the scene, so it can be pasted into
		// another one). copy_entity is false for anything else.
		bool copy_entity(entt::entity entity);
		bool has_clipboard() const { return !m_Clipboard.empty(); }
		// A copy of the clipboard with guids of its own under parent (entt::null: top level), selected. entt::null when the clipboard is
		// empty, parent is not an authored entity, or the scene plays.
		entt::entity paste_entity(entt::entity parent);
		// Authored entities only; false for anything else, and while the scene plays.
		bool rename_entity(entt::entity entity, const std::string& name);
		// Only between authored entities; false if it would make a loop.
		bool reparent(entt::entity child, entt::entity parent);

		// The instance root comes back at once; its children appear in update() when the prefab has loaded.
		entt::entity add_prefab(AssetRef prefab, entt::entity parent);
		// Saves the entity and everything below it as a prefab file (<folder>/<name>.wprefab) and, once the file has been imported (update()
		// notices it, and gives up after a few seconds), replaces them by an instance of the prefab: same name, place and Transform, and the
		// entity's own components and children are the prefab's. Returns why it cannot be done, or an empty string when it has started.
		std::string make_prefab(entt::entity entity, const std::filesystem::path& folder);
		bool has_pending_prefab() const { return !m_PendingPrefabs.empty(); }
		// How long update() waits for the prefab file to be imported before it gives up (5 seconds).
		void set_prefab_timeout(std::chrono::milliseconds timeout) { m_PrefabTimeout = timeout; }
		// What went wrong in the background since the last call (a conversion that was given up), or an empty string. The editor shows it.
		std::string take_message() { return std::exchange(m_Message, {}); }

		// Turns the instance and everything it generated into ordinary entities.
		bool unpack(entt::entity instance);
		// Drops all overrides and generates the instance again from the prefab.
		bool revert_instance(entt::entity instance);

		// ---- components ----
		// false if the entity lacks the component, the field does not exist or the value is not valid for it
		bool set_field(entt::entity entity, const std::string& component, const std::string& field, const FieldValue& value);
		bool add_component(entt::entity entity, const std::string& component);
		bool remove_component(entt::entity entity, const std::string& component);
		// Whether an override of the instance changes this field of this prefab node.
		bool is_overridden(entt::entity entity, const std::string& component, const std::string& field) const;
		// Takes the override off and puts the prefab's value back.
		bool revert_field(entt::entity entity, const std::string& component, const std::string& field);

	private:
		struct Generated
		{
			entt::entity Root{ entt::null };
			u64 NodeId = 0;
		};

		struct PendingInstance
		{
			EntityGuid Root{};
			Handle<PrefabData> Prefab{};
		};

		struct PendingPrefab
		{
			EntityGuid Entity{};
			std::filesystem::path File{};
			std::chrono::steady_clock::time_point Started{};
		};

		void finish_prefab(entt::entity original, const AssetRef& prefab);

		std::optional<Generated> generated_of(entt::entity entity) const;
		PrefabOverride* find_added_override(PrefabInstance& instance, EntityGuid rootId, EntityGuid childId);
		Handle<PrefabData> prefab_handle(const AssetRef& prefab);
		const PrefabData* prefab_of(entt::entity root);
		void expand_instance(entt::entity root, const PrefabData& prefab);
		// Whether the prefabs that this one instantiates have loaded (a failed one counts: its instances are left out).
		LoadState nested_state(const PrefabData& prefab);
		PrefabResolver resolver();
		void queue_expansion(entt::entity root);
		// Every edit ends here, so the WorldTransforms the renderer and the gizmos read never go stale.
		void mark_dirty(const std::string& label = "Edit")
		{
			m_Dirty = true;
			if (!m_Changed)
			{
				m_ChangeLabel = label;
			}
			m_Changed = true;
			if (m_Scene != nullptr)
			{
				m_Scene->update_world_transforms();
			}
		}

		std::string snapshot();
		bool restore_snapshot(const std::string& state);
		void settle_history();

		AssetManager& m_Assets;
		SceneModule& m_Scenes;
		Ref<Scene> m_Scene{};
		std::filesystem::path m_Path{};
		bool m_Dirty = false;
		bool m_Follow = false;
		bool m_Playing = false;
		entt::entity m_Selected{ entt::null };

		Ref<JobProgress> m_Opening{};
		std::string m_OpenPath{};
		std::vector<PendingInstance> m_Pending{};
		std::vector<PendingPrefab> m_PendingPrefabs{};
		std::chrono::milliseconds m_PrefabTimeout{ 5000 };
		std::string m_Message{};
		std::unordered_map<AssetGuid, Handle<PrefabData>> m_Prefabs{};

		std::vector<EntityRecord> m_Clipboard{}; // parents first; the first one is the top of the copy
		std::vector<entt::entity> authored_subtree(entt::entity entity) const;

		UndoStack m_History{};
		std::string m_Baseline{};    // the document as of the last settled edit
		bool m_HasBaseline = false;
		bool m_Changed = false;      // an edit happened since the baseline was taken
		std::string m_ChangeLabel{};
		int m_Scopes = 0;
		bool m_IgnorePending = false; // jump_history steps through restores whose prefab instances have not expanded yet
		const Scene* m_HistoryScene = nullptr; // the scene the history belongs to: play mode shows a copy and gives this one back
		std::string m_SavedState{};  // the document as it was last saved (empty: never saved), to tell when undo got back to it
	};
}
