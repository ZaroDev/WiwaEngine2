#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <core/module.h>

#include "asset_source.h"
#include "handle.h"
#include "job_system.h"
#include "wasset_io.h"

#if !WI_SHIPPING
#include "editor/asset_database.h"
#endif

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

		LoadState get_state(AssetGuid resolvedGuid) const;

#if !WI_SHIPPING
		// Editor only: the database of source files, null when the assets come from a pack.
		AssetDatabase* get_database() const { return m_Database.get(); }
#endif

		// Replaces the source; assets already loaded stay.
		void set_source(Scope<IAssetSource> source);
		IAssetSource* get_source() const { return m_Source.get(); }

		// Drops every cached asset that no handle refers to.
		void unload_unused();

		// Blocks until no load is pending. For tools and tests; the game polls the handles.
		void wait_idle() const;

	private:
		Ref<AssetSlot> acquire(AssetRef ref, AssetType expected);
		Ref<AssetSlot> acquire_resolved(AssetGuid guid, AssetType expected, bool& created);
		void run_load(Ref<AssetSlot> slot);
		void fail(const Ref<AssetSlot>& slot, const char* why);

		Scope<IAssetSource> m_Source{};
		Scope<JobSystem> m_Jobs{};
#if !WI_SHIPPING
		Scope<AssetDatabase> m_Database{};
#endif

		mutable std::mutex m_Mutex{};
		std::unordered_map<AssetGuid, Ref<AssetSlot>> m_Cache{};
		std::atomic<u32> m_Pending{ 0 };
	};
}
