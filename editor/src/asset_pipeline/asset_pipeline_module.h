#pragma once

#include <core/module.h>

#include "asset_database.h"
#include "file_watcher.h"

namespace wiwa
{
	// Part of the editor project, not of the engine: owns the AssetDatabase that imports Assets/ into Library/.
	// A game built on the engine has no such module and just reads the cooked files (or the pack) through the AssetManager.
	class AssetPipelineModule : public IModule
	{
	public:
		AssetPipelineModule() = default;
		virtual ~AssetPipelineModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::NONE; }
		virtual std::string get_name() const override { return "AssetPipeline"; }
		virtual int get_priority() const override { return -90; }

		AssetDatabase& get_database() { return *m_Database; }

	private:
		Scope<AssetDatabase> m_Database{};
		Scope<FileWatcher> m_Watcher{};
	};
}
