#pragma once

#include <atomic>
#include <memory>

#include <core/common.h>
#include "asset_guid.h"
#include "asset_types.h"

namespace wiwa
{
	// Shared state of one loaded (or loading) asset. Owned by the AssetManager cache and by every Handle to it.
	struct AssetSlot
	{
		AssetGuid Guid{};
		AssetType Type = AssetType::None;
		std::atomic<LoadState> State{ LoadState::Loading };
		Ref<void> Data{}; // written once, before State becomes Ready
	};

	// What ECS components store: the asset reference plus a shared view of its load state.
	// Cheap to copy; the asset stays loaded while any handle to it exists.
	template<typename T>
	class Handle
	{
	public:
		Handle() = default;
		Handle(AssetRef ref, Ref<AssetSlot> slot) : m_Ref(ref), m_Slot(std::move(slot)) {}

		const AssetRef& ref() const { return m_Ref; }

		LoadState state() const { return m_Slot ? m_Slot->State.load(std::memory_order_acquire) : LoadState::Failed; }
		bool ready() const { return state() == LoadState::Ready; }

		// Null until the asset is Ready.
		const T* get() const
		{
			return ready() ? static_cast<const T*>(m_Slot->Data.get()) : nullptr;
		}

		explicit operator bool() const { return m_Slot != nullptr; }

	private:
		AssetRef m_Ref{};
		Ref<AssetSlot> m_Slot{};
	};
}
