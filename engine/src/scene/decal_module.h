#pragma once

#include <core/module.h>

namespace wiwa
{
	// Ages and expires the decals of the active scene (decal_lifetime.h) while the game clock plays. Runs after the scripts (priority 0), so a
	// decal a script spawned this frame already has its first step when the frame is extracted for rendering.
	class DecalModule : public IModule
	{
	public:
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "DecalModule"; }
		virtual int get_priority() const override { return ModulePriority::Decal; }
	};
}
