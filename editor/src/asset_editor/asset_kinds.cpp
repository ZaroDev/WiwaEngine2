#include <editor_common.h>
#include "asset_kinds.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>

namespace wiwa
{
	namespace
	{
		using E = AssetEditorKind;

		// Extension, type, icon, editor, IsModel, AnimationFile, Summary.
		const AssetKindInfo KINDS[] = {
			{ ".gltf",     AssetType::Prefab,          ICON_FK_CUBE,            E::None,         true,  false, false },
			{ ".fbx",      AssetType::Prefab,          ICON_FK_CUBE,            E::None,         true,  false, false },
			{ ".glb",      AssetType::Prefab,          ICON_FK_CUBE,            E::None,         true,  false, false },
			{ ".lua",      AssetType::Script,          ICON_FK_CODE,            E::None,         false, false, false },
			{ ".wprefab",  AssetType::Prefab,          ICON_FK_CUBES,           E::None,         false, false, false },
			{ ".wshader",  AssetType::SurfaceShader,   ICON_FK_SUN,             E::None,         false, false, false },
			{ ".winput",   AssetType::InputMap,        ICON_FK_GAMEPAD,         E::None,         false, false, false },
			{ ".hdr",      AssetType::Environment,     ICON_FK_SKYATLAS,        E::None,         false, false, false },
			{ ".cube",     AssetType::Lut,             ICON_FK_FILE_O,          E::None,         false, false, false },
			{ ".ttf",      AssetType::Font,            ICON_FK_FONT,            E::None,         false, false, false },
			{ ".otf",      AssetType::Font,            ICON_FK_FONT,            E::None,         false, false, false },
			{ ".wmat",     AssetType::Material,        ICON_FK_LIGHTBULB_O,     E::None,         false, false, false },
			{ ".wuipanel", AssetType::UiPanel,         ICON_FK_WINDOW_MAXIMIZE, E::None,         false, false, false },
			{ ".wscene",   AssetType::Scene,           ICON_FK_GLOBE,           E::None,         false, false, false },
			{ ".wskel",    AssetType::Skeleton,        ICON_FK_SITEMAP,         E::Skeleton,     false, true,  true  },
			{ ".wanim",    AssetType::AnimationClip,   ICON_FK_FILM,            E::Clip,         false, true,  true  },
			{ ".wgraph",   AssetType::AnimationGraph,  ICON_FK_SHARE_ALT,       E::Graph,        false, false, true  },
			{ ".wmesh",    AssetType::Mesh,            ICON_FK_CUBE,            E::Mesh,         false, true,  true  },
			{ ".wfx",      AssetType::ParticleEffect,  ICON_FK_FIRE,            E::Effect,       false, false, false },
			{ ".wbt",      AssetType::BehaviorTree,    ICON_FK_TREE,            E::BehaviorTree, false, false, false },
			{ ".wnav",     AssetType::NavMesh,         ICON_FK_MAP,             E::None,         false, false, false },
			{ ".wphysmat", AssetType::PhysicsMaterial, ICON_FK_HUBZILLA,        E::None,         false, false, false },
		};
	}

	const AssetKindInfo* find_asset_kind(const std::filesystem::path& file)
	{
		std::string extension = file.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const AssetKindInfo& kind : KINDS)
		{
			if (extension == kind.Extension)
			{
				return &kind;
			}
		}
		return nullptr;
	}

	const AssetKindInfo* find_editable_asset_kind(AssetType type)
	{
		for (const AssetKindInfo& kind : KINDS)
		{
			if (kind.Type == type && kind.Editor != AssetEditorKind::None)
			{
				return &kind;
			}
		}
		return nullptr;
	}

	bool opens_in_asset_editor(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr && kind->Editor != AssetEditorKind::None;
	}

	bool opens_in_asset_editor(AssetType type)
	{
		return find_editable_asset_kind(type) != nullptr;
	}

	bool is_model_file(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr && kind->IsModel;
	}

	bool is_prefab_source(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr && kind->Type == AssetType::Prefab;
	}

	bool is_animation_file(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr && kind->AnimationFile;
	}

	bool has_animation_summary(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr && kind->Summary;
	}

	const char* asset_icon(const std::filesystem::path& file)
	{
		const AssetKindInfo* kind = find_asset_kind(file);
		return kind != nullptr ? kind->Icon : ICON_FK_FILE_O;
	}
}
