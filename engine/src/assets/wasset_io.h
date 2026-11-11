#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <scene/scene_data.h>
#include <physics/cooked_shape.h>
#include <input/action_types.h>
#include <physics/physics_material_asset.h>
#include <nav/nav_mesh_asset.h>
#include <ui/ui_widget.h>
#include "asset_data.h"
#include "asset_types.h"
#include "particle_effect.h"

// The .wasset file: a header (magic, format version, type, guid, dependencies, payload size) followed by a typed
// binary payload. Every read is bounds-checked: a truncated or corrupt blob gives std::nullopt, never a crash.
namespace wiwa
{
	constexpr u32 WASSET_MAGIC = 0x53534157; // "WASS"
	constexpr u32 WASSET_VERSION = 5; // 5: animation graphs. 4: skeletons carry sockets. 3: clips drive joints by name; clips and skinned meshes name their skeleton; skeletons have no node indices. 2: skeletons carry names, parents and a rest pose; clips carry events

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
	// The components of one entity or prefab node, at most SCENE_MAX_COMPONENTS_PER_ENTITY of them.
	void archive_component_list(BinaryArchive& ar, std::vector<ComponentRecord>& list);
	// The overrides of a prefab instance, at most SCENE_MAX_OVERRIDES of them.
	void archive_override_list(BinaryArchive& ar, std::vector<PrefabOverride>& list);
	void archive(BinaryArchive& ar, ScriptData& v);
	void archive(BinaryArchive& ar, SurfaceShaderData& v);
	void archive(BinaryArchive& ar, FontData& v);
	// A panel is kept as its JSON text, so that ui_panel_io is the one description of the format.
	void archive(BinaryArchive& ar, UiPanelData& v);
	// A version, then the values; refuses an unknown version and values physics_material_valid refuses (physics_material_asset.cpp).
	void archive(BinaryArchive& ar, PhysicsMaterialData& v);
	// A version, the shape kind, then the Jolt binary state of the shape (physics/cooked_shape.cpp).
	void archive(BinaryArchive& ar, CookedShapeData& v);
	// A version, then the JSON text (input/input_map_io.h is the one description of the format); refuses another version, trailing bytes and text input_map_from_text refuses.
	void archive(BinaryArchive& ar, InputMapData& v);
	// A version, then the .wgraph JSON text (native_anim_files.h is the one description of it).
	void archive(BinaryArchive& ar, AnimationGraphData& v);
	// A version, then the .wfx JSON text (particle_effect.h is the one description of it); reading bakes the curves again.
	void archive(BinaryArchive& ar, ParticleEffectData& v);
	// A version, the bank name, its bytes, then the metadata (when HasInfo). Refuses another version, an empty name or empty bytes and trailing bytes.
	void archive(BinaryArchive& ar, SoundBankData& v);
	// A version, the clip name, then the encoded bytes. Refuses another version, an empty name or empty bytes and trailing bytes.
	void archive(BinaryArchive& ar, AudioClipData& v);
	// Magic, a version, then the Detour tiles (nav/nav_mesh_asset.h is the one description of the format).
	void archive(BinaryArchive& ar, NavMeshData& v);

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
	template<> constexpr AssetType asset_type_of<ScriptData>() { return AssetType::Script; }
	template<> constexpr AssetType asset_type_of<SurfaceShaderData>() { return AssetType::SurfaceShader; }
	template<> constexpr AssetType asset_type_of<FontData>() { return AssetType::Font; }
	template<> constexpr AssetType asset_type_of<UiPanelData>() { return AssetType::UiPanel; }
	template<> constexpr AssetType asset_type_of<PhysicsMaterialData>() { return AssetType::PhysicsMaterial; }
	template<> constexpr AssetType asset_type_of<CookedShapeData>() { return AssetType::PhysicsShape; }
	template<> constexpr AssetType asset_type_of<InputMapData>() { return AssetType::InputMap; }
	template<> constexpr AssetType asset_type_of<AnimationGraphData>() { return AssetType::AnimationGraph; }
	template<> constexpr AssetType asset_type_of<ParticleEffectData>() { return AssetType::ParticleEffect; }
	template<> constexpr AssetType asset_type_of<SoundBankData>() { return AssetType::SoundBank; }
	template<> constexpr AssetType asset_type_of<AudioClipData>() { return AssetType::AudioClip; }
	template<> constexpr AssetType asset_type_of<NavMeshData>() { return AssetType::NavMesh; }

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
