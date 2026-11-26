#include <editor_common.h>
#include "vehicle_edit_model.h"

namespace wiwa
{
	bool VehicleEditModel::edit(VehicleSettingsData next, bool continuing)
	{
		if (next == m_Data)
		{
			if (!continuing)
			{
				m_Gesture = false;
			}
			return false;
		}
		std::string why;
		if (!vehicle_settings_valid(next, &why))
		{
			m_Error = why;
			return false;
		}
		m_Error.clear();
		if (!m_Gesture)
		{
			m_History.push(m_Data);
		}
		m_Gesture = continuing;
		m_Data = std::move(next);
		return true;
	}

	bool VehicleEditModel::add_torque_point()
	{
		VehicleSettingsData next = m_Data;
		const TorquePoint last = next.TorqueCurve.empty() ? TorquePoint{ next.MinRpm, 100.0f } : next.TorqueCurve.back();
		next.TorqueCurve.push_back({ last.Rpm + 500.0f, last.Torque });
		next.MaxRpm = std::max(next.MaxRpm, last.Rpm + 500.0f);
		return edit(std::move(next));
	}

	bool VehicleEditModel::remove_torque_point(size_t index)
	{
		if (index >= m_Data.TorqueCurve.size() || m_Data.TorqueCurve.size() <= 1)
		{
			return false;
		}
		VehicleSettingsData next = m_Data;
		next.TorqueCurve.erase(next.TorqueCurve.begin() + static_cast<std::ptrdiff_t>(index));
		return edit(std::move(next));
	}

	bool VehicleEditModel::add_gear()
	{
		VehicleSettingsData next = m_Data;
		next.Gears.push_back(next.Gears.empty() ? 1.0f : next.Gears.back() * 0.8f);
		return edit(std::move(next));
	}

	bool VehicleEditModel::remove_gear(size_t index)
	{
		if (index >= m_Data.Gears.size() || m_Data.Gears.size() <= 1)
		{
			return false;
		}
		VehicleSettingsData next = m_Data;
		next.Gears.erase(next.Gears.begin() + static_cast<std::ptrdiff_t>(index));
		return edit(std::move(next));
	}

	void VehicleEditModel::undo()
	{
		if (m_History.can_undo())
		{
			m_Data = m_History.undo(std::move(m_Data));
			m_Gesture = false;
		}
	}

	void VehicleEditModel::redo()
	{
		if (m_History.can_redo())
		{
			m_Data = m_History.redo(std::move(m_Data));
			m_Gesture = false;
		}
	}
}
