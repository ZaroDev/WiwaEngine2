#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <scene/scene_data.h>
#include "asset_data.h"
#include "asset_types.h"

// The .wasset file: a header (magic, format version, type, guid, dependencies, payload size) followed by a typed
// binary payload. Every read is bounds-checked: a truncated or corrupt blob gives std::nullopt, never a crash.
namespace wiwa
{
	constexpr u32 WASSET_MAGIC = 0x53534157; // "WASS"
	constexpr u32 WASSET_VERSION = 1;

	struct WAssetHeader
	{
		AssetType Type = AssetType::None;
		AssetGuid Guid{};
		std::vector<AssetGuid> Dependencies{};
		u64 PayloadSize = 0;
	};

	// Whole file bytes: header + payload.
	std::vector<std::byte> make_wasset_blob(const WAssetHeader& header, std::span<const std::byte> payload);

	// Writes to a temp file next to the target and renames it over it, so a reader never sees a half-written asset.
	bool write_wasset(const std::filesystem::path& path, const WAssetHeader& header, std::span<const std::byte> payload);

	// Validates magic, version and sizes. payload receives the bytes after the header.
	std::optional<WAssetHeader> parse_wasset_header(std::span<const std::byte> blob, std::span<const std::byte>& payload);

	// Reads and writes bytes in either direction so one archive() function per type serves both.
	class BinaryArchive
	{
	public:
		static BinaryArchive writer() { return BinaryArchive(true, {}); }
		static BinaryArchive reader(std::span<const std::byte> bytes) { return BinaryArchive(false, bytes); }

		bool reading() const { return !m_Writing; }
		bool ok() const { return m_Ok; }
		void fail() { m_Ok = false; }
		// Bytes not read yet (reading only): a count that claims more elements than could fit is corrupt.
		size_t remaining() const { return m_Bytes.size() - m_Cursor; }

		void raw(void* data, size_t size);

		template<typename T>
		void value(T& v)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			raw(&v, sizeof(T));
		}

		template<typename T>
		void pod_vector(std::vector<T>& v)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			u64 count = v.size();
			value(count);
			if (reading())
			{
				if (!m_Ok || count > remaining() / sizeof(T))
				{
					fail();
					return;
				}
				v.resize(static_cast<size_t>(count));
			}
			raw(v.data(), static_cast<size_t>(count) * sizeof(T));
		}

		// For vectors of non-trivial elements; each element is archived by fn.
		template<typename T, typename F>
		void list(std::vector<T>& v, F fn)
		{
			u64 count = v.size();
			value(count);
			if (reading())
			{
				if (!m_Ok || count > remaining()) // every element takes at least one byte
				{
					fail();
					return;
				}
				v.resize(static_cast<size_t>(count));
			}
			for (T& element : v)
			{
				if (!m_Ok)
				{
					return;
				}
				fn(element);
			}
		}

		void string(std::string& s);

		std::vector<std::byte> take() { return std::move(m_Bytes); }

	private:
		BinaryArchive(bool writing, std::span<const std::byte> bytes);

		bool m_Writing;
		bool m_Ok = true;
		std::vector<std::byte> m_Bytes{};
		size_t m_Cursor = 0;
	};

	void archive(BinaryArchive& ar, TextureData& v);
	void archive(BinaryArchive& ar, EnvironmentData& v);
	void archive(BinaryArchive& ar, LutData& v);
	void archive(BinaryArchive& ar, ShaderData& v);
	void archive(BinaryArchive& ar, MeshData& v);
	void archive(BinaryArchive& ar, MaterialData& v);
	void archive(BinaryArchive& ar, SkeletonData& v);
	void archive(BinaryArchive& ar, AnimationClipData& v);
	void archive(BinaryArchive& ar, PrefabData& v);
	void archive(BinaryArchive& ar, SceneData& v);

	template<typename T>
	constexpr AssetType asset_type_of();
	template<> constexpr AssetType asset_type_of<TextureData>() { return AssetType::Texture; }
	template<> constexpr AssetType asset_type_of<EnvironmentData>() { return AssetType::Environment; }
	template<> constexpr AssetType asset_type_of<LutData>() { return AssetType::Lut; }
	template<> constexpr AssetType asset_type_of<ShaderData>() { return AssetType::Shader; }
	template<> constexpr AssetType asset_type_of<MeshData>() { return AssetType::Mesh; }
	template<> constexpr AssetType asset_type_of<MaterialData>() { return AssetType::Material; }
	template<> constexpr AssetType asset_type_of<SkeletonData>() { return AssetType::Skeleton; }
	template<> constexpr AssetType asset_type_of<AnimationClipData>() { return AssetType::AnimationClip; }
	template<> constexpr AssetType asset_type_of<PrefabData>() { return AssetType::Prefab; }
	template<> constexpr AssetType asset_type_of<SceneData>() { return AssetType::Scene; }

	template<typename T>
	std::vector<std::byte> serialize(const T& data)
	{
		BinaryArchive ar = BinaryArchive::writer();
		archive(ar, const_cast<T&>(data)); // writing never modifies the data
		return ar.take();
	}

	template<typename T>
	std::optional<T> deserialize(std::span<const std::byte> payload)
	{
		BinaryArchive ar = BinaryArchive::reader(payload);
		T data{};
		archive(ar, data);
		if (!ar.ok())
		{
			return std::nullopt;
		}
		return data;
	}
}
