#pragma once

#include <core/module.h>

class GameModule : public wiwa::IModule
{
public:
	GameModule() = default;
	virtual ~GameModule() override = default;
	
	virtual void init() override;
	virtual void shutdown() override;

	virtual void update(float deltaTime) override;

	wiwa::UpdateFlags get_update_flags() const override { return wiwa::UpdateFlags::UPDATE; }
	std::string get_name() const override { return "Game"; }
	int get_priority() const override { return 0; }

private:
	void draw_debug_stats();
};