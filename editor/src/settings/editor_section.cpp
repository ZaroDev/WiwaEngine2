#include <editor_common.h>
#include "editor_section.h"

bool EditorSection::load()
{
	m_Gizmos = wiwa::GizmoSettings::load();
	m_Dirty = false;
	m_Error.clear();
	return true;
}

bool EditorSection::save()
{
	m_Error.clear();
	if (!m_Gizmos.save())
	{
		m_Error = "could not write " + wiwa::GizmoSettings::default_path().string();
		return false;
	}
	return true;
}

void EditorSection::flush(bool mouseDown)
{
	if (m_Dirty && !mouseDown)
	{
		m_Dirty = false;
		save();
	}
}
