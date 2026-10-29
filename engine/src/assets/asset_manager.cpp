#include <pch.h>
#include "asset_manager.h"

#include "library_source.h"
#include "pak_source.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		template<typename T>
		Ref<void> make_asset(std::span<const std::byte> payload)
		{
			std::optional<T> data = deserialize<T>(payload);
			if (!data)
			{
				return nullptr;
			}
			return std::make_shared<T>(std::move(*data));
		}

		Ref<void> deserialize_any(AssetType type, std::span<const std::byte> payload)
		{
			switch (type)
			{
			case AssetType::Texture: return make_asset<TextureData>(payload);
			case AssetType::Mesh: return make_asset<MeshData>(payload);
			case AssetType::Material: return make_asset<MaterialData>(payload);
			case AssetType::Skeleton: return make_asset<SkeletonData>(payload);
			case AssetType::AnimationClip: return make_asset<AnimationClipData>(payload);
			case AssetType::Prefab: return make_asset<PrefabData>(payload);
			case AssetType::Environment: return make_asset<EnvironmentData>(payload);
			case AssetType::Lut: return make_asset<LutData>(payload);
			case AssetType::Shader: return make_asset<ShaderData>(payload);
			default: return nullptr;
			}
		}
	}

	void AssetManager::init()
	{
		m_Jobs = CreateScope<JobSystem>();

		const std::filesystem::path pack = "assets.wpak";
		std::error_code error;
		if (s_PreferPack && std::filesystem::exists(pack, error))
		{
			auto source = CreateScope<PakSource>(pack);
			if (source->valid())
			{
				m_Source = std::move(source);
				WI_ASSET_LOG(Info, "Loading assets from {0}", pack.string());
			}
		}

		if (!m_Source)
		{
			// No pack next to the game: read the cooked files an editor wrote to Library/.
			m_Source = CreateScope<LibrarySource>("Library");
			WI_ASSET_LOG(Info, "Loading assets from Library/ with {0} workers", m_Jobs->get_worker_count());
		}
	}

	void AssetManager::shutdown()
	{
		m_Jobs.reset(); // joins the workers; queued loads are dropped

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (auto& [guid, slot] : m_Cache)
			{
				LoadState expected = LoadState::Loading;
				slot->State.compare_exchange_strong(expected, LoadState::Failed);
			}
			m_Cache.clear();

			for (const Ref<LoadBatch>& batch : m_Batches)
			{
				batch->Progress->finish(false); // nobody is left to finish them
			}
			m_Batches.clear();
		}

		m_Source.reset();
		WI_ASSET_LOG(Info, "Shutdown");
	}

	void AssetManager::set_source(Scope<IAssetSource> source)
	{
		m_Source = std::move(source);
	}

	LoadState AssetManager::get_state(AssetGuid resolvedGuid) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto found = m_Cache.find(resolvedGuid);
		return found == m_Cache.end() ? LoadState::Failed : found->second->State.load(std::memory_order_acquire);
	}

	void AssetManager::unload_unused()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto it = m_Cache.begin(); it != m_Cache.end();)
		{
			// only the cache refers to it, and no load job holds it
			if (it->second.use_count() == 1 && it->second->State.load() != LoadState::Loading)
			{
				it = m_Cache.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	Ref<JobProgress> AssetManager::preload(std::span<const AssetRef> refs)
	{
		auto batch = CreateRef<LoadBatch>();
		batch->Progress = CreateRef<JobProgress>();
		batch->Progress->begin("Loading assets", 0);

		std::vector<Ref<AssetSlot>> slots;
		for (const AssetRef& ref : refs)
		{
			if (ref.valid())
			{
				bool created = false;
				slots.push_back(acquire_resolved(ref.resolved(), AssetType::None, created));
			}
		}

		// Registered under the lock, after the loads were started: an asset that finished in between is counted here, one
		// that finishes later is counted by on_slot_finished.
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const Ref<AssetSlot>& slot : slots)
		{
			if (batch->Seen.insert(slot->Guid).second)
			{
				batch->Progress->add_total(1);
				if (slot->State.load() == LoadState::Loading)
				{
					batch->Remaining.insert(slot->Guid);
				}
				else
				{
					settle(*batch, *slot);
				}
			}
		}

		if (batch->Remaining.empty())
		{
			batch->Progress->finish(!batch->Failed);
		}
		else
		{
			m_Batches.push_back(batch);
		}

		return batch->Progress;
	}

	// Counts a finished slot as done and starts following its dependencies. Called with m_Mutex held.
	void AssetManager::settle(LoadBatch& batch, AssetSlot& slot)
	{
		std::vector<AssetSlot*> work{ &slot };
		while (!work.empty())
		{
			AssetSlot* current = work.back();
			work.pop_back();

			batch.Progress->advance(std::string(asset_type_name(current->Type.load())) + " " + current->Guid.to_string().substr(0, 8));

			if (current->State.load() == LoadState::Failed)
			{
				batch.Failed = true;
				continue;
			}

			for (AssetGuid dependency : current->Dependencies)
			{
				if (!batch.Seen.insert(dependency).second)
				{
					continue;
				}

				batch.Progress->add_total(1);
				auto found = m_Cache.find(dependency);
				if (found == m_Cache.end())
				{
					batch.Progress->advance(); // dropped from the cache already, so it had finished
				}
				else if (found->second->State.load() == LoadState::Loading)
				{
					batch.Remaining.insert(dependency);
				}
				else
				{
					work.push_back(found->second.get());
				}
			}
		}
	}

	void AssetManager::on_slot_finished(const Ref<AssetSlot>& slot)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto it = m_Batches.begin(); it != m_Batches.end();)
		{
			LoadBatch& batch = **it;
			if (batch.Remaining.erase(slot->Guid) > 0)
			{
				settle(batch, *slot);
			}

			if (batch.Remaining.empty())
			{
				batch.Progress->finish(!batch.Failed);
				it = m_Batches.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	u32 AssetManager::add_reload_listener(std::function<void(AssetGuid)> listener)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const u32 id = m_NextListenerId++;
		m_ReloadListeners.emplace_back(id, std::move(listener));
		return id;
	}

	void AssetManager::remove_reload_listener(u32 id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::erase_if(m_ReloadListeners, [id](const auto& entry) { return entry.first == id; });
	}

	void AssetManager::notify_reimported(AssetGuid resolvedGuid)
	{
		std::vector<std::function<void(AssetGuid)>> listeners;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Cache.erase(resolvedGuid);
			for (const auto& entry : m_ReloadListeners)
			{
				listeners.push_back(entry.second);
			}
		}

		for (auto& listener : listeners)
		{
			listener(resolvedGuid);
		}
	}

	void AssetManager::wait_idle() const
	{
		while (m_Pending.load() > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void AssetManager::fail(const Ref<AssetSlot>& slot, const char* why)
	{
		WI_ASSET_LOG(Error, "Cannot load asset {0}: {1}", slot->Guid.to_string(), why);
		slot->State.store(LoadState::Failed, std::memory_order_release);
	}

	Ref<AssetSlot> AssetManager::acquire(AssetRef ref, AssetType expected)
	{
		if (!ref.valid())
		{
			auto slot = CreateRef<AssetSlot>();
			slot->State.store(LoadState::Failed);
			return slot;
		}

		bool created = false;
		Ref<AssetSlot> slot = acquire_resolved(ref.resolved(), expected, created);

		// An existing slot may have been created by a dependency preload that did not know the type yet.
		AssetType current = slot->Type.load();
		if (current == AssetType::None)
		{
			slot->Type.compare_exchange_strong(current, expected);
		}
		if (current != AssetType::None && current != expected)
		{
			WI_ASSET_LOG(Error, "Asset {0} is a {1}, not a {2}", slot->Guid.to_string(), asset_type_name(current), asset_type_name(expected));
			auto mismatch = CreateRef<AssetSlot>();
			mismatch->Guid = slot->Guid;
			mismatch->State.store(LoadState::Failed);
			return mismatch;
		}

		return slot;
	}

	Ref<AssetSlot> AssetManager::acquire_resolved(AssetGuid guid, AssetType expected, bool& created)
	{
		Ref<AssetSlot> slot;
		created = false;

		{
			std::lock_guard<std::mutex> lock(m_Mutex);

			auto found = m_Cache.find(guid);
			if (found != m_Cache.end())
			{
				return found->second;
			}

			slot = CreateRef<AssetSlot>();
			slot->Guid = guid;
			slot->Type.store(expected);
			m_Cache.emplace(guid, slot);
			created = true;

			if (!m_Jobs || !m_Source)
			{
				slot->State.store(LoadState::Failed);
				return slot;
			}

			m_Pending.fetch_add(1);
		}

		m_Jobs->submit([this, slot]() { run_load(slot); });
		return slot;
	}

	void AssetManager::run_load(Ref<AssetSlot> slot)
	{
		struct PendingGuard
		{
			std::atomic<u32>& Counter;
			~PendingGuard() { Counter.fetch_sub(1); }
		} guard{ m_Pending };

		struct FinishGuard
		{
			AssetManager& Manager;
			const Ref<AssetSlot>& Slot;
			~FinishGuard() { Manager.on_slot_finished(Slot); }
		} finished{ *this, slot };

		try
		{
			std::optional<std::vector<std::byte>> blob = m_Source->read(slot->Guid);
			if (!blob)
			{
				fail(slot, "missing or unreadable");
				return;
			}

			std::span<const std::byte> payload;
			std::optional<WAssetHeader> header = parse_wasset_header(*blob, payload);
			if (!header)
			{
				fail(slot, "bad or truncated .wasset header");
				return;
			}

			if (header->Guid != slot->Guid)
			{
				fail(slot, "file guid does not match");
				return;
			}

			AssetType type = AssetType::None;
			if (!slot->Type.compare_exchange_strong(type, header->Type) && type != header->Type)
			{
				fail(slot, "asset type does not match the requested one");
				return;
			}

			Ref<void> data = deserialize_any(header->Type, payload);
			if (!data)
			{
				fail(slot, "payload is damaged");
				return;
			}

			// Dependencies start loading now; a cycle just finds the slot already in the cache.
			for (AssetGuid dependency : header->Dependencies)
			{
				bool created = false;
				acquire_resolved(dependency, AssetType::None, created);
			}

			slot->Data = std::move(data);
			slot->Dependencies = header->Dependencies;
			slot->State.store(LoadState::Ready, std::memory_order_release);
		}
		catch (const std::exception& exception)
		{
			fail(slot, exception.what());
		}
	}
}
