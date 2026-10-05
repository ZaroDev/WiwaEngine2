#pragma once
#include <string>
#include <core/common.h>

namespace wiwa
{
	enum class UpdateFlags
	{
		NONE = 0,
		UPDATE = BIT(0),
		UPDATE_PHYSICS = BIT(1),
		UPDATE_RENDERER = BIT(2),

		ALL = UPDATE | UPDATE_PHYSICS | UPDATE_RENDERER,
	};

	ENUM_CLASS_FLAG_OPERATORS(UpdateFlags)

	class IModule
	{
	public:
		virtual ~IModule() = default;

		virtual void init() {};
		virtual void shutdown() {};

		virtual void pre_update(float deltaTime) {};
		virtual void update(float deltaTime) {};
		virtual void post_update(float deltaTime) {};

		virtual void pre_physics_update(float deltaTime) {};
		virtual void physics_update(float deltaTime) {};
		virtual void post_physics_update(float deltaTime) {};

		virtual void pre_render_update(float deltaTime) {};
		virtual void render_update(float deltaTime) {};
		virtual void post_render_update(float deltaTime) {};

		virtual UpdateFlags get_update_flags() const = 0;
		virtual std::string get_name() const = 0;

		virtual int get_priority() const = 0;
	};
}