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
		m_Events.clear();
		m_Scene.reset();
		m_SkeletonHandles.clear();
		m_ClipHandles.clear();
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
			used = used || m_SkeletonHandles.contains(guid) || m_ClipHandles.contains(guid);
		}
		if (used)
		{
			m_Instances.clear();
			for (AssetGuid guid : guids)
			{
				m_SkeletonHandles.erase(guid);
				m_ClipHandles.erase(guid);
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
		tick(scenes->get_active(), time.get_delta_time_seconds(), time.get_game_clock().IsPlaying);
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

	// A new instance for an enabled Animator that has none, playing its Clip if PlayOnStart.
	AnimationModule::Entry* AnimationModule::make_entry(entt::registry& registry, entt::entity entity)
	{
		const Animator* animator = registry.try_get<Animator>(entity);
		const Id* id = registry.try_get<Id>(entity);
		if (animator == nullptr || id == nullptr || !animator->Enabled)
		{
			return nullptr;
		}

		Entry entry;
		entry.Instance = std::make_unique<AnimatorInstance>(m_Sources, *animator);
		entry.Guid = id->Guid;
		if (animator->PlayOnStart && animator->Clip.valid())
		{
			entry.Instance->play(ClipSelector{ {}, animator->Clip });
		}
		return &m_Instances.emplace(entity, std::move(entry)).first->second;
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

			const AnimatorInstance& instance = *m_Instances.at(owner).Instance;
			if (!instance.ready())
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
			const glm::mat4 offset = glm::translate(glm::mat4(1.0f), socket.OffsetPosition) * glm::mat4_cast(socket.OffsetRotation);
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
