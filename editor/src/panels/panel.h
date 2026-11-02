#pragma once
#include <string>

#include <core/common.h>
#include <math/math.h>
#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>



enum class PanelCategory : wiwa::u8
{
	File = BIT(0),
	Window = BIT(1),
	Info = BIT(2),
	Renderer = BIT(3)
};

ENUM_CLASS_FLAG_OPERATORS(PanelCategory)

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