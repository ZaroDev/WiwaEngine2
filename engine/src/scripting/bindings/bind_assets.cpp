#include <pch.h>
#include "bindings.h"

#include <assets/asset_manager.h>
#include <scene/scene_module.h>

namespace wiwa
{
	void bind_assets(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaAsset>("Asset", sol::no_constructor,
			"is_valid", [](const LuaAsset& asset) { return asset.Ref.valid(); },
			"guid", sol::property([](const LuaAsset& asset) { return asset.Ref.Guid.to_string(); }),
			sol::meta_function::equal_to, [](const LuaAsset& a, const LuaAsset& b) { return a.Ref == b.Ref; });

		lua.new_usertype<LuaSceneLoad>("SceneLoad", sol::no_constructor,
			"progress", [](const LuaSceneLoad& load) { return load.Progress->fraction(); },
			"done", [](const LuaSceneLoad& load) { return load.Progress->finished(); },
			"failed", [](const LuaSceneLoad& load) { return load.Progress->finished() && !load.Progress->succeeded(); },
			"stage", [](const LuaSceneLoad& load) { return load.Progress->stage(); });

		// The asset a source path names ("assets/textures/rock.png"), to put in an asset property. Loading is left to whoever uses it.
		// With a second argument the sub-asset of the file with that local id (the number the file's .meta lists under subAssets, e.g. "mesh/0": 1, for a MeshRenderer.mesh).
		runtime.sandbox().set_function("load_asset", [&context](const std::string& path, sol::optional<double> localId)
			{
				if (context.Assets == nullptr)
				{
					throw sol::error("assets are not available");
				}
				const std::optional<AssetRef> asset = context.Assets->find_asset(path);
				if (!asset)
				{
					throw sol::error("there is no asset at '" + path + "'");
				}
				AssetRef ref = *asset;
				if (localId)
				{
					if (*localId < 0.0 || *localId != std::floor(*localId))
					{
						throw sol::error("the local id of load_asset is a whole number of 0 or more");
					}
					ref.LocalId = static_cast<u64>(*localId);
				}
				return LuaAsset{ ref };
			});

		// Starts loading a scene and returns at once: 'replace' (the default) swaps the active scene when it is complete, 'additive' adds to it.
		runtime.sandbox().set_function("load_scene", [&context](const std::string& path, sol::optional<std::string> mode)
			{
				if (context.Scenes == nullptr)
				{
					throw sol::error("scenes cannot be loaded here");
				}
				if (mode && *mode != "replace" && *mode != "additive")
				{
					throw sol::error("load_scene mode is 'replace' or 'additive', not '" + *mode + "'");
				}
				return LuaSceneLoad{ context.Scenes->load_async(path, mode && *mode == "additive" ? LoadMode::Additive : LoadMode::Replace) };
			});
	}
}
