#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <core/job_progress.h>
#include <core/module.h>

#include "asset_catalog.h"
#include "asset_source.h"
#include "handle.h"
#include "job_system.h"
#include "wasset_io.h"

namespace wiwa
{
	// Loads assets by guid on worker threads and keeps them in a refcounted cache.
	// The data it hands out is CPU-side only; the renderer builds GPU resources from it on its own thread.
	class AssetManager : public IModule
	{
	public:
		AssetManager() = default;
		virtual ~AssetManager() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "AssetManager"; }
		virtual int get_priority() const override { return -100; }

		// Starts loading (if it is not loaded or loading already) and returns at once. The handle goes Ready or Failed
		// later; a type that does not match the file is Failed.
		template<typename T>
		Handle<T> load(AssetRef ref)
		{
			return Handle<T>(ref, acquire(ref, asset_type_of<T>()));
		}

		// Starts loading all of them, plus everything they depend on, and returns a progress object for a loading bar: it
		// finishes when every one has either loaded or failed (Succeeded only if none failed). The total grows as dependencies
		// are found. The assets stay cached until unload_unused(); load<T>() them afterwards to get at the data.
		Ref<JobProgress> preload(std::span<const AssetRef> refs);

		// The asset a source path names ("engine/shaders/imgui.hlsl", "Assets/models/torus_knot.glb"), from the catalog of the
		// current source. nullopt if there is no such asset (yet: the editor writes the catalog when its import has finished,
		// so a caller that needs an asset early asks again later; a miss reads the catalog again at most every 250 ms).
		// Thread-safe.
		std::optional<AssetRef> find_asset(std::string_view path);

		// Every whole asset the catalog lists (a copy), reading the catalog first if it was not read yet. Thread-safe.
		std::vector<AssetCatalog::Entry> catalog_entries();

		LoadState get_state(AssetGuid resolvedGuid) const;

		// Whether init() reads an assets.wpak next to the program when there is one (a game: yes, the default). A tool that
		// has a Library of its own and imports live, like the editor, turns it off before the engine initializes, so a
		// leftover pack never shadows the Library.
		static void set_prefer_pack(bool prefer) { s_PreferPack = prefer; }

		// Replaces the source; assets already loaded stay.
		void set_source(Scope<IAssetSource> source);
		IAssetSource* get_source() const { return m_Source.get(); }

		// Drops every cached asset that no handle refers to.
		void unload_unused();

		// Called when the editor re-cooked an asset: forgets the cached copy so the next load() reads the new file (handles that
		// already exist keep the old data) and tells the listeners, which can load it again. May be called from any thread;
		// the listeners run on the calling thread.
		void notify_reimported(AssetGuid resolvedGuid);
		// Returns an id for remove_reload_listener; the owner of a listener removes it before it is destroyed.
		u32 add_reload_listener(std::function<void(AssetGuid)> listener);
		void remove_reload_listener(u32 id);

		// Blocks until no load is pending. For tools and tests; the game polls the handles.
		void wait_idle() const;

	private:
		inline static bool s_PreferPack = true;

		// A preload() in progress: the assets it waits for, and every asset it has counted so far.
		struct LoadBatch
		{
			Ref<JobProgress> Progress{};
			std::unordered_set<AssetGuid> Remaining{};
			std::unordered_set<AssetGuid> Seen{};
			bool Failed = false;
		};

		void settle(LoadBatch& batch, AssetSlot& slot);
		void on_slot_finished(const Ref<AssetSlot>& slot);

		Ref<AssetSlot> acquire(AssetRef ref, AssetType expected);
		Ref<AssetSlot> acquire_resolved(AssetGuid guid, AssetType expected, bool& created);
		void run_load(Ref<AssetSlot> slot);
		void fail(const Ref<AssetSlot>& slot, const char* why);

		Scope<IAssetSource> m_Source{};
		AssetCatalog m_Catalog{};
		std::mutex m_CatalogMutex{};
		std::chrono::steady_clock::time_point m_CatalogLoadedAt{};
		bool m_CatalogLoaded = false;
		Scope<JobSystem> m_Jobs{};

		mutable std::mutex m_Mutex{};
		std::unordered_map<AssetGuid, Ref<AssetSlot>> m_Cache{};
		std::atomic<u32> m_Pending{ 0 };
		std::vector<std::pair<u32, std::function<void(AssetGuid)>>> m_ReloadListeners{};
		u32 m_NextListenerId = 1;
		std::vector<Ref<LoadBatch>> m_Batches{};
	};
}
