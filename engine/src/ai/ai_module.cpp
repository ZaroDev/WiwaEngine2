#include <pch.h>
#include "ai_module.h"

#include <core/engine.h>
#include <animation/animation_module.h>
#include <nav/nav_module.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <scene/scene_module.h>

#include "bt_file.h"

namespace wiwa
{
	void AIModule::init()
	{
		m_Hooks.Nav = Engine::get().get_module<NavModule>();
		m_Services.Leaf = [this](BtLeafCall& call) { return leaf(call); };

		// The animator leaf: the entity's Animator instance of the scene that plays.
		m_Hooks.PlayClip = [this](entt::entity entity, const std::string& name)
		{
			AnimationModule* animation = Engine::get().get_module<AnimationModule>();
			const Ref<Scene> scene = world_scene();
			AnimatorInstance* animator = (animation != nullptr && scene) ? animation->acquire(scene, entity, true) : nullptr;
			AssetRef clip;
			if (animator == nullptr || animator->lookup(name, clip) == ClipLookup::Unknown)
			{
				return false;
			}
			ClipSelector selector;
			selector.Name = name;
			animator->play(selector);
			return true;
		};
		m_Hooks.ClipBusy = [this](entt::entity entity)
		{
			AnimationModule* animation = Engine::get().get_module<AnimationModule>();
			const AnimatorInstance* animator = animation != nullptr ? animation->instance_for(entity) : nullptr;
			return animator != nullptr && (animator->pending_commands() > 0 || animator->playing());
		};

		if (AssetManager* assets = Engine::get().get_module<AssetManager>())
		{
			m_ReloadListener = assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_DirtyMutex);
				m_Dirty.insert(guid);
			});
		}
	}

	void AIModule::shutdown()
	{
		stop_world();
		if (m_ReloadListener != 0)
		{
			if (AssetManager* assets = Engine::get().get_module<AssetManager>())
			{
				assets->remove_reload_listener(m_ReloadListener);
			}
			m_ReloadListener = 0;
		}
	}

	void AIModule::update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		Time& time = Engine::get().get_time();
		tick(scenes != nullptr ? scenes->get_active() : nullptr, time.get_game_clock().IsPlaying, time.get_delta_time_seconds());
	}

	BtStatus AIModule::leaf(BtLeafCall& call)
	{
		const std::string& type = call.Node->Type;
		if (type == "MoveTo" || type == "PlayAnimation")
		{
			m_Hooks.TheScene = world_scene().get();
			return bt_engine_leaf(call, m_Hooks);
		}
		if (m_LeafHandler)
		{
			return m_LeafHandler(call);
		}
		return BtStatus::Failure;
	}

	bool AIModule::start_world(Ref<Scene> scene)
	{
		stop_world();
		if (!scene)
		{
			return false;
		}
		if (!m_Services.Leaf)
		{
			m_Services.Leaf = [this](BtLeafCall& call) { return leaf(call); };
		}
		m_World = scene;
		m_HasWorld = true;
		m_Time = 0.0;
		m_Hooks.TheScene = scene.get();
		return true;
	}

	void AIModule::drop(Controller& controller)
	{
		if (controller.Instance)
		{
			controller.Instance->abort(m_Services, m_Time);
			controller.Instance.reset();
		}
	}

	void AIModule::stop_world()
	{
		if (!m_HasWorld)
		{
			return;
		}
		for (auto& [entity, controller] : m_Controllers)
		{
			drop(controller);
		}
		m_Controllers.clear();
		m_World.reset();
		m_HasWorld = false;
		m_Hooks.TheScene = nullptr;
	}

	bool AIModule::register_tree(const AssetRef& ref, const BehaviorTreeDoc& doc, std::string* error)
	{
		std::string why;
		Ref<const CompiledTree> tree = compile_bt(doc, why);
		if (!tree)
		{
			if (error != nullptr)
			{
				*error = why;
			}
			return false;
		}
		TreeEntry& entry = m_Trees[ref.resolved()];
		entry.Tree = std::move(tree);
		entry.Manual = true;
		entry.Failed = false;
		entry.Loading = false;
		entry.Error.clear();
		return true;
	}

	AIModule::TreeEntry& AIModule::entry_for(const AssetRef& ref)
	{
		const AssetGuid key = ref.resolved();
		const auto it = m_Trees.find(key);
		if (it != m_Trees.end())
		{
			return it->second;
		}
		TreeEntry& entry = m_Trees[key];
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets != nullptr)
		{
			entry.Handle = assets->load<BehaviorTreeData>(ref);
			entry.Loading = true;
		}
		else
		{
			entry.Failed = true;
			entry.Error = "there is no asset manager";
		}
		return entry;
	}

	// Compiles a loaded tree asset; a tree that is invalid or failed to load logs why once and is skipped.
	void AIModule::refresh_entry(const AssetRef& ref, TreeEntry& entry)
	{
		if (entry.Manual || !entry.Loading)
		{
			return;
		}
		switch (entry.Handle.state())
		{
		case LoadState::Loading:
			return;
		case LoadState::Failed:
			entry.Loading = false;
			entry.Failed = true;
			entry.Error = "the tree asset failed to load";
			WI_AI_LOG(Error, "Behaviour tree {0}: {1}", ref.Guid.to_string(), entry.Error);
			return;
		case LoadState::Ready:
			break;
		}

		entry.Loading = false;
		std::string error;
		const std::optional<BehaviorTreeDoc> doc = read_bt_file(entry.Handle.get()->Text, error);
		if (doc)
		{
			entry.Tree = compile_bt(*doc, error);
		}
		entry.Failed = entry.Tree == nullptr;
		entry.Error = error;
		if (entry.Failed)
		{
			WI_AI_LOG(Error, "Behaviour tree {0} cannot run: {1}", ref.Guid.to_string(), error);
		}
	}

	BtInstance* AIModule::instance_of(entt::entity entity)
	{
		const auto it = m_Controllers.find(entity);
		return it != m_Controllers.end() ? it->second.Instance.get() : nullptr;
	}

	const BtInstance* AIModule::instance_of(entt::entity entity) const
	{
		const auto it = m_Controllers.find(entity);
		return it != m_Controllers.end() ? it->second.Instance.get() : nullptr;
	}

	u32 AIModule::active_count() const
	{
		u32 count = 0;
		for (const auto& [entity, controller] : m_Controllers)
		{
			count += controller.Instance ? 1u : 0u;
		}
		return count;
	}

	void AIModule::tick(const Ref<Scene>& active, bool playing, float dt)
	{
		if (m_HasWorld)
		{
			const Ref<Scene> scene = m_World.lock();
			if (!scene || (m_AutoStart && scene != active))
			{
				stop_world(); // the scene was replaced or is gone: what runs is aborted
			}
		}
		if (m_AutoStart && !m_HasWorld && playing && active != nullptr)
		{
			start_world(active);
		}
		if (!m_HasWorld || !playing)
		{
			return;
		}
		const Ref<Scene> scene = m_World.lock();
		if (!scene)
		{
			return;
		}
		m_Time += dt;
		tick_controllers(*scene, dt);
	}

	void AIModule::tick_controllers(Scene& scene, float dt)
	{
		entt::registry& registry = scene.registry();

		// Reimported trees compile again; every controller that used the old one restarts.
		{
			std::set<AssetGuid> dirty;
			{
				std::lock_guard<std::mutex> lock(m_DirtyMutex);
				dirty.swap(m_Dirty);
			}
			for (const AssetGuid& guid : dirty)
			{
				const auto it = m_Trees.find(guid);
				if (it != m_Trees.end() && !it->second.Manual)
				{
					m_Trees.erase(it);
				}
			}
		}

		for (auto& [entity, controller] : m_Controllers)
		{
			controller.Seen = false;
		}

		for (auto [entity, ai] : registry.view<AIController>().each())
		{
			Controller& controller = m_Controllers[entity];
			controller.Seen = true;

			if (!ai.Enabled || !ai.Tree.valid())
			{
				drop(controller);
				controller.Tree.reset();
				if (ai.Enabled && !controller.Warned)
				{
					controller.Warned = true;
					WI_AI_LOG(Warning, "An AIController has no behaviour tree");
				}
				continue;
			}

			TreeEntry& entry = entry_for(ai.Tree);
			refresh_entry(ai.Tree, entry);
			if (!entry.Tree)
			{
				drop(controller);
				controller.Tree.reset();
				continue; // loading, or it failed (logged once when it did)
			}

			if (!controller.Instance || controller.Tree != entry.Tree)
			{
				drop(controller);
				controller.Tree = entry.Tree;
				controller.Instance = CreateScope<BtInstance>(entry.Tree, entity, static_cast<u32>(ai.Seed));
				controller.LastTick = m_Time - dt;
				controller.NextTick = m_Time;
				if (!ai.Overrides.empty())
				{
					const nlohmann::json overrides = nlohmann::json::parse(ai.Overrides, nullptr, false);
					if (overrides.is_discarded() || !overrides.is_object())
					{
						WI_AI_LOG(Warning, "The blackboard overrides of an AIController are not a JSON object and are ignored");
					}
					else
					{
						for (const std::string& problem : controller.Instance->blackboard().apply_overrides(overrides))
						{
							WI_AI_LOG(Warning, "{0}", problem);
						}
					}
				}
			}

			if (ai.TickRate > 0.0f && m_Time + 1.0e-9 < controller.NextTick)
			{
				continue;
			}
			const float elapsed = static_cast<float>(m_Time - controller.LastTick);
			controller.LastTick = m_Time;
			if (ai.TickRate > 0.0f)
			{
				controller.NextTick = std::max(controller.NextTick + 1.0 / ai.TickRate, m_Time);
			}
			controller.Instance->tick(m_Services, elapsed, m_Time);
		}

		// Controllers whose component or entity is gone: what runs is aborted, once.
		for (auto it = m_Controllers.begin(); it != m_Controllers.end();)
		{
			if (!it->second.Seen)
			{
				drop(it->second);
				it = m_Controllers.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
