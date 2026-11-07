#include <pch.h>
#include "preview_scene.h"

#include "components.h"
#include "prefab_expander.h"
#include "scene_records.h"

#define WI_PREVIEW_LOG(level, ...) WI_LOG(level, "Scene", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// A light that shines from `from` towards the origin: the entity's forward axis (-Z) points along the light.
		void add_light(Scene& scene, const char* name, LightKind kind, const glm::vec3& from, const glm::vec3& color, float intensity, bool shadows)
		{
			const entt::entity entity = scene.create(name);
			entt::registry& registry = scene.registry();
			Transform& transform = registry.get<Transform>(entity);
			transform.Position = from;
			transform.Rotation = glm::quatLookAt(glm::normalize(-from), glm::vec3(0.0f, 1.0f, 0.0f));
			Light light;
			light.Kind = kind;
			light.Color = glm::vec4(color, 1.0f);
			light.Intensity = intensity;
			light.CastsShadows = shadows;
			registry.emplace<Light>(entity, light);
		}
	}

	Ref<Scene> make_preview_scene(const PreviewSceneOptions& options)
	{
		Ref<Scene> scene = CreateRef<Scene>();

		add_light(*scene, "Key light", LightKind::Directional, glm::vec3(2.5f, 4.0f, 3.0f), glm::vec3(1.0f, 0.96f, 0.9f), 3.0f, true);
		add_light(*scene, "Fill light", LightKind::Directional, glm::vec3(-3.0f, 1.5f, 2.0f), glm::vec3(0.7f, 0.8f, 1.0f), 1.0f, false);
		add_light(*scene, "Rim light", LightKind::Directional, glm::vec3(0.5f, 2.5f, -4.0f), glm::vec3(1.0f, 1.0f, 1.0f), 1.5f, false);

		SceneSettings settings;
		settings.Source = EnvironmentSource::None;
		settings.Background = BackgroundMode::SolidColor;
		settings.BackgroundColor = options.Background;
		settings.HemisphereSky = glm::vec3(0.55f, 0.6f, 0.7f);
		settings.HemisphereGround = glm::vec3(0.25f, 0.23f, 0.2f);
		settings.AmbientIntensity = 0.6f;
		settings.ExposureEv = options.ExposureEv;
		settings.Exposure.Auto = false;
		settings.Bloom.Enabled = false;
		settings.Ao.Enabled = false;
		scene->set_settings(settings);
		scene->update_world_transforms();
		return scene;
	}

	PreviewModel::~PreviewModel()
	{
		clear();
	}

	void PreviewModel::clear()
	{
		if (m_Root != entt::null && m_Scene != nullptr && m_Scene->registry().valid(m_Root))
		{
			m_Scene->destroy(m_Root);
		}
		m_Root = entt::null;
	}

	void PreviewModel::set_prefab(const AssetRef& prefab)
	{
		if (prefab == m_Prefab)
		{
			return;
		}
		clear();
		m_Prefab = prefab;
		m_Handle = prefab.valid() ? m_Assets.load<PrefabData>(prefab) : Handle<PrefabData>{};
	}

	void PreviewModel::update()
	{
		if (m_Root != entt::null || !m_Prefab.valid() || m_Scene == nullptr)
		{
			return;
		}
		const PrefabData* prefab = m_Handle.get();
		if (prefab == nullptr)
		{
			return; // still loading, or it failed (failed())
		}

		const entt::entity root = m_Scene->create("Model");
		m_Scene->registry().emplace<PrefabInstance>(root, PrefabInstance{ m_Prefab, {} });
		const EntityRecord rootRecord = record_from_entity(*m_Scene, root);
		const ExpandResult expanded = expand_prefab(rootRecord, *prefab);
		for (const std::string& warning : expanded.Warnings)
		{
			WI_PREVIEW_LOG(Warning, "{0}", warning);
		}
		for (size_t i = 1; i < expanded.Entities.size(); i++)
		{
			const entt::entity entity = apply_record(*m_Scene, expanded.Entities[i]);
			if (entity != entt::null)
			{
				m_Scene->registry().emplace<PrefabChild>(entity, PrefabChild{ rootRecord.Id, expanded.NodeIds[i] });
			}
		}
		m_Scene->update_world_transforms();
		m_Root = root;
	}
}
