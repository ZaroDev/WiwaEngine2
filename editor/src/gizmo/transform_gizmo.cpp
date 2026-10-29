#include <editor_common.h>
#include "transform_gizmo.h"

#include <cmath>

#include <scene/components.h>

namespace wiwa
{
	namespace
	{
		constexpr float MIN_FACTOR = 0.01f;
		constexpr float MAX_FACTOR = 100.0f;

		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		bool finite(const glm::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

		// 0, 1, 2 for the axis a handle sits on (X, Y, Z) or is the normal of (plane handles); -1 for the others.
		int axis_index(gizmo::Handle handle)
		{
			switch (handle)
			{
			case gizmo::Handle::AxisX: case gizmo::Handle::PlaneYZ: return 0;
			case gizmo::Handle::AxisY: case gizmo::Handle::PlaneXZ: return 1;
			case gizmo::Handle::AxisZ: case gizmo::Handle::PlaneXY: return 2;
			default: return -1;
			}
		}

		bool is_plane(gizmo::Handle handle)
		{
			return handle == gizmo::Handle::PlaneYZ || handle == gizmo::Handle::PlaneXZ || handle == gizmo::Handle::PlaneXY;
		}

		// A unit axis from a matrix column, or the fallback when the column has no length (a scale of 0).
		glm::vec3 unit_column(const glm::mat4& matrix, int column, const glm::vec3& fallback)
		{
			const glm::vec3 v(matrix[column]);
			const float length = glm::length(v);
			return finite(v) && length > 1e-6f ? v / length : fallback;
		}

		glm::mat3 axes_of(const glm::mat4& matrix)
		{
			return glm::mat3(unit_column(matrix, 0, glm::vec3(1.0f, 0.0f, 0.0f)), unit_column(matrix, 1, glm::vec3(0.0f, 1.0f, 0.0f)), unit_column(matrix, 2, glm::vec3(0.0f, 0.0f, 1.0f)));
		}

		glm::quat rotation_of(const glm::mat4& matrix)
		{
			const glm::quat q = glm::quat_cast(axes_of(matrix));
			return finite(q) ? glm::normalize(q) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		bool invertible(const glm::mat4& matrix)
		{
			const float determinant = glm::determinant(matrix);
			return std::isfinite(determinant) && std::abs(determinant) > 1e-12f;
		}

		const glm::vec3 AXIS_COLORS[3] = { glm::vec3(1.0f, 0.25f, 0.25f), glm::vec3(0.35f, 1.0f, 0.35f), glm::vec3(0.35f, 0.55f, 1.0f) };
		const glm::vec3 HIGHLIGHT(1.0f, 0.9f, 0.2f);
		const glm::vec3 UNIFORM_COLOR(0.9f, 0.9f, 0.9f);
	}

	bool TransformGizmo::update(DebugDraw& draw, const GizmoInput& input, SceneEditor& editor)
	{
		m_Hovered = gizmo::Handle::None;

		const entt::entity entity = editor.selected();
		entt::registry& registry = editor.scene().registry();
		if (!registry.valid(entity) || !registry.all_of<Transform, WorldTransform>(entity))
		{
			m_Drag.reset();
			return false;
		}
		if (m_Drag && m_Drag->Entity != entity)
		{
			m_Drag.reset(); // the selection moved on in the middle of a drag
		}

		const glm::mat4 world = registry.get<WorldTransform>(entity).Matrix;
		gizmo::Frame frame;
		frame.Origin = glm::vec3(world[3]);
		frame.Axes = space == gizmo::Space::Local ? axes_of(world) : glm::mat3(1.0f);
		frame.Scale = gizmo::screen_constant_scale(frame.Origin, input.View, input.Projection, SIZE_PIXELS, input.ViewportSize.y);
		if (!finite(frame.Origin) || !std::isfinite(frame.Scale) || frame.Scale <= 0.0f)
		{
			m_Drag.reset();
			return false;
		}

		const gizmo::Ray ray = gizmo::screen_ray(input.MousePixel, input.ViewportSize, input.View, input.Projection);

		if (m_Drag)
		{
			if (input.LeftDown)
			{
				apply_drag(ray, input, editor);
			}
			else
			{
				m_Drag.reset();
			}
		}

		if (!m_Drag && input.Hovered)
		{
			const float hitRadius = gizmo::screen_constant_scale(frame.Origin, input.View, input.Projection, HIT_PIXELS, input.ViewportSize.y);
			m_Hovered = gizmo::pick_handle(mode, ray, frame, hitRadius);
			if (m_Hovered != gizmo::Handle::None && input.LeftPressed)
			{
				begin_drag(ray, input, m_Hovered, frame, entity, editor);
			}
		}

		draw_handles(draw, frame, m_Drag ? m_Drag->Handle : m_Hovered);
		return m_Drag.has_value() || m_Hovered != gizmo::Handle::None;
	}

	bool TransformGizmo::begin_drag(const gizmo::Ray& ray, const GizmoInput& input, gizmo::Handle handle, const gizmo::Frame& frame, entt::entity entity, SceneEditor& editor)
	{
		entt::registry& registry = editor.scene().registry();
		const glm::mat4 world = registry.get<WorldTransform>(entity).Matrix;

		Drag drag;
		drag.Entity = entity;
		drag.Mode = mode;
		drag.Handle = handle;
		drag.Frame = frame;
		drag.StartScale = registry.get<Transform>(entity).Scale;
		drag.StartWorldRotation = rotation_of(world);

		const Hierarchy& hierarchy = registry.get<Hierarchy>(entity);
		if (hierarchy.Parent != entt::null && registry.valid(hierarchy.Parent) && registry.all_of<WorldTransform>(hierarchy.Parent))
		{
			drag.ParentWorld = registry.get<WorldTransform>(hierarchy.Parent).Matrix;
		}

		drag.CameraRight = glm::vec3(input.View[0][0], input.View[1][0], input.View[2][0]);
		drag.CameraUp = glm::vec3(input.View[0][1], input.View[1][1], input.View[2][1]);
		drag.CameraForward = -glm::vec3(input.View[0][2], input.View[1][2], input.View[2][2]);

		const int index = axis_index(handle);
		if (mode == gizmo::Mode::Rotate)
		{
			const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, frame.Axes[index]);
			if (!t)
			{
				return false;
			}
			const glm::vec3 offset = ray.Origin + ray.Direction * *t - frame.Origin;
			if (glm::dot(offset, offset) < 1e-12f)
			{
				return false;
			}
			drag.GrabVector = glm::normalize(offset);
		}
		else if (handle == gizmo::Handle::Uniform)
		{
			const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, drag.CameraForward);
			if (!t)
			{
				return false;
			}
			drag.GrabPoint = ray.Origin + ray.Direction * *t;
		}
		else if (is_plane(handle))
		{
			const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, frame.Axes[index]);
			if (!t)
			{
				return false;
			}
			drag.GrabPoint = ray.Origin + ray.Direction * *t;
		}
		else
		{
			drag.GrabParameter = gizmo::axis_parameter(ray, frame.Origin, frame.Axes[index]);
		}

		m_Drag = drag;
		return true;
	}

	void TransformGizmo::apply_drag(const gizmo::Ray& ray, const GizmoInput& input, SceneEditor& editor)
	{
		const Drag& drag = *m_Drag;
		entt::registry& registry = editor.scene().registry();
		const Transform current = registry.get<Transform>(drag.Entity);
		const gizmo::Frame& frame = drag.Frame;
		const int index = axis_index(drag.Handle);

		auto write_position = [&](const glm::vec3& worldPosition)
		{
			if (!invertible(drag.ParentWorld) || !finite(worldPosition))
			{
				return;
			}
			const glm::vec3 local = glm::vec3(glm::inverse(drag.ParentWorld) * glm::vec4(worldPosition, 1.0f));
			if (finite(local) && local != current.Position)
			{
				editor.set_field(drag.Entity, "Transform", "Position", FieldValue(local));
			}
		};

		auto write_scale = [&](const glm::vec3& scale)
		{
			if (finite(scale) && scale != current.Scale)
			{
				editor.set_field(drag.Entity, "Transform", "Scale", FieldValue(scale));
			}
		};

		switch (drag.Mode)
		{
		case gizmo::Mode::Translate:
		{
			if (is_plane(drag.Handle))
			{
				const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, frame.Axes[index]);
				if (!t)
				{
					return;
				}
				const glm::vec3 moved = ray.Origin + ray.Direction * *t - drag.GrabPoint;
				float along[2] = { glm::dot(moved, frame.Axes[(index + 1) % 3]), glm::dot(moved, frame.Axes[(index + 2) % 3]) };
				if (input.Ctrl)
				{
					along[0] = gizmo::snap(along[0], TranslateSnap);
					along[1] = gizmo::snap(along[1], TranslateSnap);
				}
				write_position(frame.Origin + frame.Axes[(index + 1) % 3] * along[0] + frame.Axes[(index + 2) % 3] * along[1]);
			}
			else
			{
				float delta = gizmo::axis_parameter(ray, frame.Origin, frame.Axes[index]) - drag.GrabParameter;
				if (input.Ctrl)
				{
					delta = gizmo::snap(delta, TranslateSnap);
				}
				write_position(frame.Origin + frame.Axes[index] * delta);
			}
			break;
		}

		case gizmo::Mode::Rotate:
		{
			const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, frame.Axes[index]);
			if (!t)
			{
				return;
			}
			const glm::vec3 offset = ray.Origin + ray.Direction * *t - frame.Origin;
			float angle = gizmo::signed_angle(drag.GrabVector, offset, frame.Axes[index]);
			if (input.Ctrl)
			{
				angle = gizmo::snap(angle, glm::radians(RotateSnapDegrees));
			}

			const glm::quat worldRotation = glm::angleAxis(angle, frame.Axes[index]) * drag.StartWorldRotation;
			const glm::quat local = glm::normalize(glm::inverse(rotation_of(drag.ParentWorld)) * worldRotation);
			if (finite(local) && glm::dot(local, current.Rotation) < 0.999999f)
			{
				editor.set_field(drag.Entity, "Transform", "Rotation", FieldValue(local));
			}
			break;
		}

		case gizmo::Mode::Scale:
		{
			float factor = 1.0f;
			if (drag.Handle == gizmo::Handle::Uniform)
			{
				const std::optional<float> t = gizmo::ray_plane(ray, frame.Origin, drag.CameraForward);
				if (!t)
				{
					return;
				}
				const glm::vec3 moved = ray.Origin + ray.Direction * *t - drag.GrabPoint;
				factor = 1.0f + glm::dot(moved, glm::normalize(drag.CameraRight + drag.CameraUp)) / frame.Scale;
			}
			else
			{
				if (std::abs(drag.GrabParameter) < 1e-4f * frame.Scale)
				{
					return;
				}
				factor = gizmo::axis_parameter(ray, frame.Origin, frame.Axes[index]) / drag.GrabParameter;
			}

			if (input.Ctrl)
			{
				factor = gizmo::snap(factor, ScaleSnap);
			}
			if (!std::isfinite(factor))
			{
				return;
			}
			factor = std::clamp(factor, MIN_FACTOR, MAX_FACTOR);

			glm::vec3 scale = drag.StartScale;
			if (drag.Handle == gizmo::Handle::Uniform)
			{
				scale *= factor;
			}
			else
			{
				scale[index] *= factor; // scale lives in the entity's own axes
			}
			write_scale(scale);
			break;
		}
		}
	}

	void TransformGizmo::draw_handles(DebugDraw& draw, const gizmo::Frame& frame, gizmo::Handle highlighted) const
	{
		constexpr gizmo::Handle AXIS_HANDLES[3] = { gizmo::Handle::AxisX, gizmo::Handle::AxisY, gizmo::Handle::AxisZ };
		constexpr gizmo::Handle PLANE_HANDLES[3] = { gizmo::Handle::PlaneYZ, gizmo::Handle::PlaneXZ, gizmo::Handle::PlaneXY };
		constexpr float WIDTH = 3.0f;
		const float s = frame.Scale;
		auto color_of = [&](gizmo::Handle handle, const glm::vec3& normal, float alpha = 1.0f)
		{
			return glm::vec4(handle == highlighted ? HIGHLIGHT : normal, alpha);
		};

		for (int i = 0; i < 3; i++)
		{
			const glm::vec3 axis = frame.Axes[i];
			const glm::vec4 color = color_of(AXIS_HANDLES[i], AXIS_COLORS[i]);
			switch (mode)
			{
			case gizmo::Mode::Translate:
			{
				draw.arrow(frame.Origin + axis * (gizmo::AXIS_START * s), frame.Origin + axis * (gizmo::ARROW_LENGTH * s), color, DebugDepth::AlwaysOnTop, WIDTH);

				// the square of the plane with this normal spans the other two axes
				const glm::vec3 a = frame.Axes[(i + 1) % 3];
				const glm::vec3 b = frame.Axes[(i + 2) % 3];
				const float low = gizmo::PLANE_MIN * s;
				const float high = gizmo::PLANE_MAX * s;
				const glm::vec4 plane = color_of(PLANE_HANDLES[i], AXIS_COLORS[i]);
				const glm::vec3 p00 = frame.Origin + a * low + b * low;
				const glm::vec3 p10 = frame.Origin + a * high + b * low;
				const glm::vec3 p11 = frame.Origin + a * high + b * high;
				const glm::vec3 p01 = frame.Origin + a * low + b * high;
				draw.line(p00, p10, plane, DebugDepth::AlwaysOnTop, 2.0f);
				draw.line(p10, p11, plane, DebugDepth::AlwaysOnTop, 2.0f);
				draw.line(p11, p01, plane, DebugDepth::AlwaysOnTop, 2.0f);
				draw.line(p01, p00, plane, DebugDepth::AlwaysOnTop, 2.0f);
				draw.quad((p00 + p11) * 0.5f, a * ((high - low) * 0.5f), b * ((high - low) * 0.5f), glm::vec4(glm::vec3(plane), 0.3f), DebugDepth::AlwaysOnTop);
				break;
			}
			case gizmo::Mode::Rotate:
				draw.circle(frame.Origin, axis, gizmo::RING_RADIUS * s, color, DebugDepth::AlwaysOnTop, WIDTH);
				break;
			case gizmo::Mode::Scale:
			{
				const glm::vec3 tip = frame.Origin + axis * (gizmo::ARROW_LENGTH * s);
				draw.line(frame.Origin + axis * (gizmo::UNIFORM_RADIUS * s), tip, color, DebugDepth::AlwaysOnTop, WIDTH);
				glm::mat4 cube(frame.Axes);
				cube[3] = glm::vec4(tip, 1.0f);
				draw.box(cube, glm::vec3(0.06f * s), color, DebugDepth::AlwaysOnTop, WIDTH);
				break;
			}
			}
		}

		if (mode == gizmo::Mode::Scale)
		{
			glm::mat4 center(frame.Axes);
			center[3] = glm::vec4(frame.Origin, 1.0f);
			draw.box(center, glm::vec3(gizmo::UNIFORM_RADIUS * 0.5f * s), color_of(gizmo::Handle::Uniform, UNIFORM_COLOR), DebugDepth::AlwaysOnTop, WIDTH);
		}
	}
}
