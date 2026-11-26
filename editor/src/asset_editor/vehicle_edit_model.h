#pragma once

#include <string>

#include <physics/vehicle_settings.h>

#include "edit_history.h"

namespace wiwa
{
	// The document of the vehicle editor: a VehicleSettingsData with snapshot undo. Every change goes through edit(), which refuses one that
	// would not be valid (vehicle_settings_valid: the file must stay importable) and says why. One drag of a slider is one undo step. No ImGui.
	class VehicleEditModel
	{
	public:
		explicit VehicleEditModel(VehicleSettingsData data) : m_Data(std::move(data)) {}

		const VehicleSettingsData& data() const { return m_Data; }
		// Why the last edit was refused (empty after one that applied).
		const std::string& error() const { return m_Error; }

		// Replaces the document. continuing: part of a gesture that goes on (a slider being dragged), which joins the gesture's undo step.
		// Returns false when nothing changed or the edit is not valid.
		bool edit(VehicleSettingsData next, bool continuing = false);
		void end_gesture() { m_Gesture = false; }

		// A torque point after the last (500 rpm further, the same torque), or a gear after the last (a little taller); removing refuses the
		// last point and the last gear.
		bool add_torque_point();
		bool remove_torque_point(size_t index);
		bool add_gear();
		bool remove_gear(size_t index);

		bool dirty() const { return m_History.dirty(); }
		void mark_saved() { m_History.mark_saved(); }
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		void undo();
		void redo();

	private:
		VehicleSettingsData m_Data{};
		EditHistory<VehicleSettingsData> m_History{};
		std::string m_Error{};
		bool m_Gesture = false;
	};
}
