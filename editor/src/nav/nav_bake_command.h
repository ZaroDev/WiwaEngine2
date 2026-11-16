#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <core/common.h>

namespace wiwa
{
	class SceneEditor;
	class NavEditModel;

	// --nav-bake <file>: once the scene of the command line has loaded and the assets are cooked, bakes the scene's first enabled NavMeshVolume to the file
	// asked for and quits the engine with the result. EditorModule calls update() once per frame while active().
	class NavBakeCommand
	{
	public:
		// Frames to let the command-line scene start opening before it is looked at.
		static constexpr u32 FIRST_FRAME = 10;
		// The scene of the command line may not have replaced the empty startup scene yet; after this many frames a scene without a volume is an error.
		static constexpr u32 GIVE_UP_FRAME = 1800;

		// The file after "--nav-bake" in `arguments` (the last one wins); the command is not active without one.
		explicit NavBakeCommand(const std::vector<std::string>& arguments);

		bool active() const { return !m_Out.empty(); }

		// `frame`: the editor's frame counter.
		void update(SceneEditor& scene, NavEditModel& model, u32 frame, bool assetScanFinished);

	private:
		std::filesystem::path m_Out{};
		bool m_Started = false;
	};
}
