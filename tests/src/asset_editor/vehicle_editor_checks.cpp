#include <editor_common.h>
#include "vehicle_editor_checks.h"

#include <cmath>
#include <limits>

#include <asset_editor/vehicle_edit_model.h>
#include <gizmo/vehicle_gizmo.h>

namespace wiwa
{
	std::string run_vehicle_editor_checks()
	{
		std::string failures;
		const auto check = [&](bool condition, const std::string& what)
		{
			if (!condition)
			{
				WI_ERROR("Editor", "Vehicle editor check FAILED: {0}", what);
				failures += (failures.empty() ? "" : "; ") + what;
			}
		};

		// the edit model: an edit is undoable and dirties; an edit that would not be valid is refused and leaves no step
		VehicleEditModel model(default_vehicle_settings());
		check(!model.dirty() && !model.can_undo(), "vehicle_edit_model: a fresh document is clean");
		VehicleSettingsData next = model.data();
		next.MaxRpm = 8000.0f;
		check(model.edit(next) && model.data().MaxRpm == 8000.0f && model.dirty() && model.can_undo(), "vehicle_edit_model: an edit applies and can be undone");
		model.undo();
		check(model.data().MaxRpm == default_vehicle_settings().MaxRpm && !model.dirty() && model.can_redo(), "vehicle_edit_model: undo restores and is clean again");
		model.redo();
		check(model.data().MaxRpm == 8000.0f, "vehicle_edit_model: redo applies again");
		VehicleSettingsData invalid = model.data();
		invalid.Gears = { 3.0f, -1.0f };
		const bool refusedUndo = model.can_undo();
		check(!model.edit(invalid) && model.data().MaxRpm == 8000.0f && !model.error().empty(), "vehicle_edit_model: an invalid edit is refused with a reason");
		check(model.can_undo() == refusedUndo, "vehicle_edit_model: a refused edit leaves no undo step");

		// a slider drag is one step
		VehicleEditModel dragged(default_vehicle_settings());
		for (int i = 1; i <= 5; i++)
		{
			VehicleSettingsData step = dragged.data();
			step.DragArea = 0.7f + 0.1f * static_cast<float>(i);
			dragged.edit(step, true);
		}
		dragged.end_gesture();
		dragged.undo();
		check(std::abs(dragged.data().DragArea - default_vehicle_settings().DragArea) < 1e-6f && !dragged.can_undo(), "vehicle_edit_model: one drag is one undo step");

		// the lists: points and gears are added and removed; the last gear and the last torque point stay
		VehicleEditModel lists(default_vehicle_settings());
		const size_t gears = lists.data().Gears.size();
		check(lists.add_gear() && lists.data().Gears.size() == gears + 1, "vehicle_edit_model: a gear is added");
		check(lists.remove_gear(0) && lists.data().Gears.size() == gears, "vehicle_edit_model: a gear is removed");
		VehicleSettingsData one = lists.data();
		one.Gears = { 1.0f };
		lists.edit(one);
		check(!lists.remove_gear(0) && lists.data().Gears.size() == 1, "vehicle_edit_model: the last gear cannot be removed");
		const size_t points = lists.data().TorqueCurve.size();
		check(lists.add_torque_point() && lists.data().TorqueCurve.size() == points + 1 && vehicle_settings_valid(lists.data()), "vehicle_edit_model: a torque point is added past the last (the curve stays sorted)");
		check(lists.remove_torque_point(0) && lists.data().TorqueCurve.size() == points, "vehicle_edit_model: a torque point is removed");

		// the wheel gizmo: a circle of the wheel's radius round its centre, a travel line through it; nothing for a broken transform
		VehicleWheel wheel;
		wheel.Radius = 0.4f;
		const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
		const VehicleWheelGizmoShapes shapes = vehicle_wheel_gizmo_shapes(wheel, world, 16);
		bool onCircle = shapes.Circle.size() == 16;
		for (const glm::vec3& point : shapes.Circle)
		{
			onCircle = onCircle && std::abs(glm::length(point - glm::vec3(1.0f, 2.0f, 3.0f)) - 0.4f) < 1e-4f && std::abs(point.x - 1.0f) < 1e-4f;
		}
		check(onCircle, "vehicle_gizmo_shapes: the circle has the radius, round the centre, in the plane across the axle");
		check(std::abs(glm::length(shapes.TravelTop - shapes.TravelBottom) - (wheel.SuspensionMaxLength - wheel.SuspensionMinLength)) < 1e-4f, "vehicle_gizmo_shapes: the travel line is as long as the suspension travel");
		glm::mat4 broken(1.0f);
		broken[3] = glm::vec4(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f);
		check(vehicle_wheel_gizmo_shapes(wheel, broken).Circle.empty(), "vehicle_gizmo_shapes: a transform that is not finite draws nothing");
		return failures;
	}
}
