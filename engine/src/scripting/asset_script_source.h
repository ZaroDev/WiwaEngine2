#pragma once

#include <map>

#include <assets/asset_manager.h>
#include "script_source.h"

namespace wiwa
{
	// Script sources from the AssetManager: the editor's Library or a pak, the same way. Scripts are small, so a script that is not
	// loaded yet is waited for (on the main thread, at most a moment); the ones a scene needs are preloaded with it anyway.
	class AssetScriptSource final : public IScriptSource
	{
	public:
		explicit AssetScriptSource(AssetManager& assets) : m_Assets(assets) {}

		virtual std::optional<LoadedScript> find_by_path(std::string_view path) override;
		virtual std::optional<LoadedScript> find_by_asset(AssetRef ref) override;

		// Drops the loaded copy of a script, so that the next read reads the file again (it was cooked again).
		void forget(AssetRef ref) { m_Handles.erase(ref.resolved()); }

	private:
		AssetManager& m_Assets;
		std::map<AssetGuid, Handle<ScriptData>> m_Handles{}; // keeps the scripts loaded
	};
}
