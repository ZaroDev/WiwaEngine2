#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <functional>

#include <math/math.h>

namespace wiwa
{
	// 128-bit identifier of an asset, stable across moves, renames and re-imports (it lives in the .meta file).
	struct AssetGuid
	{
		u64 Hi = 0;
		u64 Lo = 0;

		static AssetGuid generate();
		static std::optional<AssetGuid> from_string(std::string_view text);

		// 32 lowercase hex characters
		std::string to_string() const;

		bool valid() const { return Hi != 0 || Lo != 0; }

		bool operator==(const AssetGuid& other) const { return Hi == other.Hi && Lo == other.Lo; }
		bool operator!=(const AssetGuid& other) const { return !(*this == other); }
		bool operator<(const AssetGuid& other) const { return Hi != other.Hi ? Hi < other.Hi : Lo < other.Lo; }
	};

	// A whole asset (LocalId 0) or one sub-asset of a source file (a mesh of a model, ...).
	struct AssetRef
	{
		AssetGuid Guid{};
		u64 LocalId = 0;

		bool valid() const { return Guid.valid(); }
		bool operator==(const AssetRef& other) const { return Guid == other.Guid && LocalId == other.LocalId; }
	};
}

template<>
struct std::hash<wiwa::AssetGuid>
{
	size_t operator()(const wiwa::AssetGuid& guid) const noexcept
	{
		return static_cast<size_t>(guid.Hi ^ (guid.Lo * 0x9E3779B97F4A7C15ull));
	}
};
