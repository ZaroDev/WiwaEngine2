#include <editor_common.h>
#include "starter_scene.h"

#include <asset_pipeline/build_settings.h>
#include <core/engine.h>
#include <core/project_dir.h>
#include <scene/entity_templates.h>

#include <glm/gtc/quaternion.hpp>

namespace wiwa
{
	namespace
	{
		std::string text(const std::filesystem::path& path)
		{
			const std::u8string utf8 = path.u8string();
			return std::string(utf8.begin(), utf8.end());
		}

		entt::entity create(SceneEditor& editor, const std::vector<EntityTemplate>& templates, const std::string& name)
		{
			for (const EntityTemplate& entry : templates)
			{
				if (entry.Name == name)
				{
					return entry.Create(editor, entt::null);
				}
			}
			return entt::null;
		}
	}

	StarterSceneStep build_starter_scene(const std::filesystem::path& projectDir, bool scanFinished, AssetManager& assets, SceneModule& scenes,
		SceneEditor& editor, const std::function<void(const std::filesystem::path&)>& import, std::string& error)
	{
		std::error_code ec;
		const std::filesystem::path marker = projectDir / NEW_PROJECT_MARKER;
		if (!std::filesystem::exists(marker, ec))
		{
			return StarterSceneStep::NotNeeded;
		}
		if (!scanFinished)
		{
			return StarterSceneStep::NotYet;
		}

		// ---- the scene, once (a start that failed after saving it only finishes the build settings)
		const std::filesystem::path scenePath = projectDir / STARTER_SCENE;
		if (!std::filesystem::exists(scenePath, ec))
		{
			// the catalog is read again at most every 250 ms: right after the scan the shapes model can still be unknown for a moment
			const std::vector<EntityTemplate> templates = entity_templates(assets);
			if (std::none_of(templates.begin(), templates.end(), [](const EntityTemplate& entry) { return entry.Name == "Plane"; }))
			{
				return StarterSceneStep::NotYet;
			}
			// a document of its own: whatever the user does in the editor meanwhile (a long first scan cooks every shader) is left alone
			SceneEditor builder(assets, scenes);
			Ref<Scene> starter = CreateRef<Scene>();
			if (const u32 rate = Engine::get().project().FixedRate; rate != 0) // as SceneEditor::new_scene
			{
				SceneSettings settings = starter->settings();
				settings.Physics.FixedRateHz = rate;
				starter->set_settings(settings);
			}
			builder.set_scene(starter);
			const entt::entity plane = create(builder, templates, "Plane");
			const entt::entity light = create(builder, templates, "Directional light");
			const entt::entity camera = create(builder, templates, "Camera");
			if (plane == entt::null || light == entt::null || camera == entt::null)
			{
				error = "a template entity could not be created";
				return StarterSceneStep::Failed;
			}
			builder.set_field(plane, "Transform", "Scale", FieldValue(glm::vec3(10.0f, 1.0f, 10.0f)));
			// above and behind the origin, looking down at it (the camera looks down -Z)
			builder.set_field(camera, "Transform", "Position", FieldValue(glm::vec3(0.0f, 4.0f, 8.0f)));
			builder.set_field(camera, "Transform", "Rotation", FieldValue(glm::quat(glm::vec3(-std::atan2(4.0f, 8.0f), 0.0f, 0.0f))));

			std::filesystem::create_directories(scenePath.parent_path(), ec);
			if (!builder.save_as(scenePath))
			{
				error = "could not write " + text(scenePath);
				return StarterSceneStep::Failed;
			}
			import(scenePath);
		}

		// ---- the build settings, once the scene has a guid
		const std::optional<AssetRef> scene = assets.find_asset(STARTER_SCENE_ASSET);
		if (!scene)
		{
			return StarterSceneStep::NotYet;
		}
		const std::filesystem::path buildPath = projectDir / BUILD_SETTINGS_FILE;
		std::string loadError;
		BuildSettings build = BuildSettings::load(buildPath, loadError).value_or(BuildSettings{});
		build.DefaultScene = scene->Guid;
		build.Scenes = { scene->Guid };
		if (!build.save(buildPath))
		{
			error = "could not write " + text(buildPath);
			return StarterSceneStep::Failed;
		}

		std::filesystem::remove(marker, ec);
		if (ec)
		{
			error = "could not delete " + text(marker) + ": " + ec.message();
			return StarterSceneStep::Failed;
		}
		// shown only while the editor holds an untouched untitled scene: never in place of the user's work
		if (!editor.playing() && editor.path().empty() && !editor.dirty())
		{
			editor.open(text(scenePath));
		}
		return StarterSceneStep::Done;
	}
}
