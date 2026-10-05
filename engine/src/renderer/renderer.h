#pragma once
#include <core/module.h>

namespace wiwa
{
	class Renderer : public IModule
	{
	public:
		Renderer() = default;
		virtual ~Renderer() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual void pre_update(float deltaTime)override;
		virtual void update(float deltaTime) override;
		virtual void post_update(float deltaTime) override;

		virtual void pre_render_update(float deltaTime) override;
		virtual void render_update(float deltaTime) override;
		virtual void post_render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const { return "Renderer"; };

		virtual int get_priority() const { return 0; }

	private:

	};
}