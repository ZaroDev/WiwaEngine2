#include <pch.h>
#include "animation_module.h"

#include <core/engine.h>
#include <scene/scene_module.h>

#include "animation_log.h"

namespace wiwa
{
	void AnimationModule::init()
	{
		m_Assets = Engine::get().get_module<AssetManager>();
		if (m_Assets == nullptr)
		{
			return;
		}

		m_Sources.Skeleton = [this](const AssetRef& ref) -> const SkeletonData*
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			auto it = m_SkeletonHandles.find(ref.resolved());
			if (it == m_SkeletonHandles.end())
			{
				it = m_SkeletonHandles.emplace(ref.resolved(), m_Assets->load<SkeletonData>(ref)).first;
			}
			return it->second.get();
		};
		m_Sources.Clip = [this](const AssetRef& ref) -> const AnimationClipData*
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			auto it = m_ClipHandles.find(ref.resolved());
			if (it == m_ClipHandles.end())
			{
				it = m_ClipHandles.emplace(ref.resolved(), m_Assets->load<AnimationClipData>(ref)).first;
			}
			return it->second.get();
		};
		m_Sources.Graph = [this](const AssetRef& ref) -> const AnimationGraphData*
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			auto it = m_GraphHandles.find(ref.resolved());
			if (it == m_GraphHandles.end())
			{
				it = m_GraphHandles.emplace(ref.resolved(), m_Assets->load<AnimationGraphData>(ref)).first;
			}
			return it->second.get();
		};

		// the listener runs on whatever thread cooked the asset: it only notes the guid
		m_ReloadListener = m_Assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				m_ReloadQueue.push_back(guid);
			});
	}

	void AnimationModule::shutdown()
	{
		if (m_Assets != nullptr && m_ReloadListener != 0)
		{
			m_Assets->remove_reload_listener(m_ReloadListener);
			m_ReloadListener = 0;
		}
		m_Instances.clear();
		m_PreviewScenes.clear();
		m_Events.clear();
		m_Scene.reset();
		m_SkeletonHandles.clear();
		m_ClipHandles.clear();
		m_GraphHandles.clear();
	}

	// An asset that was cooked again: the instances hold pointers into the old data, so they end (the next tick makes new ones), and then
	// the handles go.
	void AnimationModule::drain_reloads()
	{
		std::vector<AssetGuid> guids;
		{
			std::lock_guard<std::mutex> lock(m_ReloadMutex);
			guids.swap(m_ReloadQueue);
		}

		bool used = false;
		for (AssetGuid guid : guids)
		{
			used = used || m_SkeletonHandles.contains(guid) || m_ClipHandles.contains(guid) || m_GraphHandles.contains(guid);
		}
		if (used)
		{
			m_Instances.clear();
			for (auto& [scene, preview] : m_PreviewScenes)
			{
				preview.Instances.clear();
			}
			for (AssetGuid guid : guids)
			{
				m_SkeletonHandles.erase(guid);
				m_ClipHandles.erase(guid);
				m_GraphHandles.erase(guid);
			}
		}
	}

	void AnimationModule::update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (scenes == nullptr)
		{
			return;
		}
		const Time& time = Engine::get().get_time();
		const bool playing = time.get_game_clock().IsPlaying;
		tick(scenes->get_active(), step_seconds(playing, time.get_delta_time_seconds(), time.get_real_delta_time_seconds()), playing);

		for (auto it = m_PreviewScenes.begin(); it != m_PreviewScenes.end();)
		{
			if (it->second.Owner.expired())
			{
				it = m_PreviewScenes.erase(it);
				continue;
			}
			tick_preview_scene(it->second, time.get_real_delta_time_seconds());
			++it;
		}
	}

	void AnimationModule::register_preview_scene(const Ref<Scene>& scene)
	{
		if (scene != nullptr)
		{
			PreviewScene& entry = m_PreviewScenes[scene.get()];
			if (entry.Owner.lock() != scene)
			{
				entry = PreviewScene{}; // an address a dead scene had: nothing of it is kept
				entry.Owner = scene;
			}
		}
	}

	void AnimationModule::unregister_preview_scene(const Ref<Scene>& scene)
	{
		if (scene != nullptr)
		{
			m_PreviewScenes.erase(scene.get());
		}
	}

	const AnimatorInstance* AnimationModule::instance_in(const Scene* scene, entt::entity entity) const
	{
		const auto preview = m_PreviewScenes.find(scene);
		if (preview != m_PreviewScenes.end())
		{
			if (preview->second.Owner.lock().get() != scene)
			{
				return nullptr; // the scene that was registered at this address is gone
			}
			const auto it = preview->second.Instances.find(entity);
			return it != preview->second.Instances.end() ? it->second.Instance.get() : nullptr;
		}
		return m_Scene.get() == scene ? instance_for(entity) : nullptr;
	}

	AnimatorInstance* AnimationModule::instance_in(const Scene* scene, entt::entity entity)
	{
		return const_cast<AnimatorInstance*>(std::as_const(*this).instance_in(scene, entity));
	}

	void AnimationModule::set_preview_paused_in(const Scene* scene, entt::entity entity, bool paused)
	{
		const auto preview = m_PreviewScenes.find(scene);
		if (preview == m_PreviewScenes.end())
		{
			return;
		}
		const auto it = preview->second.Instances.find(entity);
		if (it != preview->second.Instances.end())
		{
			it->second.Paused = paused;
		}
	}

	// Every enabled Animator of a looked-at scene has an instance that advances with real time; an instance whose Animator is gone ends.
	void AnimationModule::tick_preview_scene(PreviewScene& preview, float deltaTime)
	{
		const Ref<Scene> scene = preview.Owner.lock();
		if (scene == nullptr)
		{
			return;
		}
		drain_reloads();
		entt::registry& registry = scene->registry();
		std::erase_if(preview.Instances, [&](const auto& item)
			{
				const Animator* animator = registry.valid(item.first) ? registry.try_get<Animator>(item.first) : nullptr;
				return animator == nullptr || !animator->Enabled;
			});
		for (const auto [entity, id, animator] : registry.view<Id, Animator>().each())
		{
			if (animator.Enabled && !preview.Instances.contains(entity))
			{
				if (std::optional<Entry> entry = make_instance(registry, entity, true))
				{
					preview.Instances.emplace(entity, std::move(*entry));
				}
			}
		}
		std::vector<ClipEvent> ignored;
		for (auto& [entity, entry] : preview.Instances)
		{
			ignored.clear();
			entry.Instance->update(entry.Paused ? 0.0f : deltaTime * registry.get<Animator>(entity).Speed, ignored);
		}
	}

	void AnimationModule::adopt(const Ref<Scene>& scene)
	{
		if (scene != m_Scene)
		{
			m_Instances.clear();
			m_Events.clear();
			m_Scene = scene;
		}
	}

	// A new instance for an enabled Animator, playing its Clip if PlayOnStart.
	std::optional<AnimationModule::Entry> AnimationModule::make_instance(entt::registry& registry, entt::entity entity, bool previewOnly)
	{
		const Animator* animator = registry.try_get<Animator>(entity);
		const Id* id = registry.try_get<Id>(entity);
		if (animator == nullptr || id == nullptr || !animator->Enabled)
		{
			return std::nullopt;
		}

		Entry entry;
		entry.Instance = std::make_unique<AnimatorInstance>(m_Sources, *animator);
		entry.Guid = id->Guid;
		entry.Preview = previewOnly;
		if (animator->PlayOnStart && animator->Clip.valid() && !animator->Graph.valid()) // a graph plays instead; the instance falls back to the clip if it cannot
		{
			entry.Instance->play(ClipSelector{ {}, animator->Clip });
		}
		return entry;
	}

	AnimationModule::Entry* AnimationModule::make_entry(entt::registry& registry, entt::entity entity)
	{
		std::optional<Entry> entry = make_instance(registry, entity, false);
		return entry ? &m_Instances.emplace(entity, std::move(*entry)).first->second : nullptr;
	}

	AnimatorInstance* AnimationModule::acquire(const Ref<Scene>& scene, entt::entity entity, bool playing)
	{
		if (scene == nullptr)
		{
			return nullptr;
		}
		adopt(scene);
		if (AnimatorInstance* existing = instance_for(entity))
		{
			return existing;
		}
		if (!playing || !scene->registry().valid(entity))
		{
			return nullptr;
		}
		Entry* entry = make_entry(scene->registry(), entity);
		return entry != nullptr ? entry->Instance.get() : nullptr;
	}

	void AnimationModule::tick(const Ref<Scene>& scene, float deltaTime, bool playing)
	{
		adopt(scene);
		if (scene == nullptr)
		{
			return;
		}

		drain_reloads();
		entt::registry& registry = scene->registry();

		for (auto it = m_Instances.begin(); it != m_Instances.end();)
		{
			const Animator* animator = registry.valid(it->first) ? registry.try_get<Animator>(it->first) : nullptr;
			if (animator == nullptr || !animator->Enabled)
			{
				it = m_Instances.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (playing)
		{
			for (const auto [entity, id, animator] : registry.view<Id, Animator>().each())
			{
				if (!animator.Enabled || m_Instances.contains(entity))
				{
					continue;
				}
				make_entry(registry, entity);
			}
		}

		std::vector<ClipEvent> events;
		for (auto& [entity, entry] : m_Instances)
		{
			if (!playing && !entry.Preview)
			{
				continue; // a paused game keeps its last pose
			}
			const float speed = registry.get<Animator>(entity).Speed;
			events.clear();
			entry.Instance->update(entry.Paused ? 0.0f : deltaTime * speed, events);
			m_EventsTotal += events.size();
			for (ClipEvent& event : events)
			{
				m_Events.push_back(QueuedAnimationEvent{ entry.Guid, std::move(event) });
			}
		}

		if (!m_Instances.empty())
		{
			write_sockets(*scene);
		}
	}

	void AnimationModule::write_sockets(Scene& scene)
	{
		entt::registry& registry = scene.registry();
		if (registry.view<AnimationSocket>().empty())
		{
			return;
		}

		scene.update_world_transforms(); // the animators may have moved this frame
		for (const auto [entity, socket, transform, hierarchy] : registry.view<AnimationSocket, Transform, Hierarchy>().each())
		{
			// the nearest ancestor that has an animator with a pose
			entt::entity owner = hierarchy.Parent;
			while (owner != entt::null && !m_Instances.contains(owner))
			{
				owner = registry.get<Hierarchy>(owner).Parent;
			}
			if (owner == entt::null)
			{
				continue;
			}

			const Entry& entry = m_Instances.at(owner);
			const AnimatorInstance& instance = *entry.Instance;
			if (entry.Preview || !instance.ready())
			{
				continue;
			}
			const i32 joint = find_joint(*instance.skeleton(), socket.Joint);
			if (joint < 0 || static_cast<size_t>(joint) >= instance.model().size())
			{
				continue;
			}

			const glm::mat4 animatorWorld = registry.get<WorldTransform>(owner).Matrix;
			const glm::mat4 parentWorld = hierarchy.Parent != entt::null ? registry.get<WorldTransform>(hierarchy.Parent).Matrix : glm::mat4(1.0f);
			const glm::mat4 offset = glm::translate(glm::mat4(1.0f), socket.OffsetPosition) * glm::mat4_cast(socket.OffsetRotation) * glm::scale(glm::mat4(1.0f), socket.OffsetScale);
			const glm::mat4 local = glm::inverse(parentWorld) * animatorWorld * instance.model()[static_cast<size_t>(joint)] * offset;
			decompose_matrix(local, transform.Position, transform.Rotation, transform.Scale);
		}
		scene.update_world_transforms();
	}

	AnimatorInstance* AnimationModule::instance_for(entt::entity entity)
	{
		auto it = m_Instances.find(entity);
		return it != m_Instances.end() ? it->second.Instance.get() : nullptr;
	}

	const AnimatorInstance* AnimationModule::instance_for(entt::entity entity) const
	{
		auto it = m_Instances.find(entity);
		return it != m_Instances.end() ? it->second.Instance.get() : nullptr;
	}

	AnimatorInstance* AnimationModule::preview(entt::entity entity)
	{
		if (m_Scene == nullptr)
		{
			return nullptr;
		}
		if (AnimatorInstance* existing = instance_for(entity))
		{
			return existing;
		}

		entt::registry& registry = m_Scene->registry();
		if (!registry.valid(entity))
		{
			return nullptr;
		}
		const Animator* animator = registry.try_get<Animator>(entity);
		const Id* id = registry.try_get<Id>(entity);
		if (animator == nullptr || id == nullptr || !animator->Enabled)
		{
			return nullptr;
		}

		Entry entry;
		entry.Instance = std::make_unique<AnimatorInstance>(m_Sources, *animator);
		entry.Guid = id->Guid;
		entry.Preview = true;
		return m_Instances.emplace(entity, std::move(entry)).first->second.Instance.get();
	}

	void AnimationModule::set_preview_paused(entt::entity entity, bool paused)
	{
		auto it = m_Instances.find(entity);
		if (it != m_Instances.end())
		{
			it->second.Paused = paused;
		}
	}

	void AnimationModule::drop_previews()
	{
		std::erase_if(m_Instances, [](const auto& item) { return item.second.Preview; });
	}

	std::vector<QueuedAnimationEvent> AnimationModule::take_events()
	{
		return std::exchange(m_Events, {});
	}

	glm::mat4 AnimationModule::joint_world(entt::entity animator, const std::string& joint) const
	{
		const AnimatorInstance* instance = instance_for(animator);
		if (instance == nullptr || !instance->ready() || m_Scene == nullptr)
		{
			return glm::mat4(1.0f);
		}
		const i32 index = find_joint(*instance->skeleton(), joint);
		if (index < 0 || static_cast<size_t>(index) >= instance->model().size())
		{
			return glm::mat4(1.0f);
		}
		const WorldTransform* world = m_Scene->registry().try_get<WorldTransform>(animator);
		return (world != nullptr ? world->Matrix : glm::mat4(1.0f)) * instance->model()[static_cast<size_t>(index)];
	}
}
