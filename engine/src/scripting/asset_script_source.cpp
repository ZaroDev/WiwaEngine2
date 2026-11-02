#include <pch.h>
#include "asset_script_source.h"

#include <chrono>
#include <thread>

namespace wiwa
{
	std::optional<LoadedScript> AssetScriptSource::find_by_path(std::string_view path)
	{
		const std::optional<AssetRef> asset = m_Assets.find_asset(path);
		return asset ? find_by_asset(*asset) : std::nullopt;
	}

	std::optional<LoadedScript> AssetScriptSource::find_by_asset(AssetRef ref)
	{
		auto found = m_Handles.find(ref.resolved());
		if (found == m_Handles.end())
		{
			found = m_Handles.emplace(ref.resolved(), m_Assets.load<ScriptData>(ref)).first;
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (found->second.state() == LoadState::Loading && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		const ScriptData* data = found->second.get();
		if (data == nullptr)
		{
			m_Handles.erase(found); // a later try loads it again
			return std::nullopt;
		}
		return LoadedScript{ ref, data->SourcePath, data->Source };
	}
}
