#pragma once
#include <string>

#include <core/common.h>
#include <math/math.h>

enum class PanelCategory : wiwa::u8
{
	Window = BIT(0),
	Info = BIT(1),
	Renderer = BIT(2)
};

class Panel
{
public:
	Panel(const char* name, PanelCategory category)
		: m_Name(name), m_Category(category) {}

	virtual ~Panel() = default;

	virtual void on_update() {};
	virtual void on_draw() = 0;

	const char* get_name() const { return m_Name.c_str(); }
	PanelCategory get_category() const { return m_Category; }
	bool is_active() const { return m_Open; }
	void switch_active()
	{
		m_Open = !m_Open;
	}

protected:
	std::string m_Name;
	bool m_Open = true;
	PanelCategory m_Category;
};