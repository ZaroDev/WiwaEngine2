#include <pch.h>
#include "render_test.h"

#include <cmath>
#include <limits>

#include <math/gizmo_math.h>

// Checks of the gizmo math: rays, handle hit testing, drag helpers. Pure functions, no GPU.
namespace wiwa
{
	namespace
	{
		constexpr float PI = 3.14159265358979f;
		constexpr float TOLERANCE = 1e-3f;
		const float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();

		bool nearly(float a, float b, float tolerance = TOLERANCE) { return std::abs(a - b) <= tolerance; }
		bool nearly(const glm::vec3& a, const glm::vec3& b, float tolerance = TOLERANCE) { return glm::length(a - b) <= tolerance; }
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		gizmo::Ray down_z(float x, float y, float z = 5.0f) { return gizmo::Ray{ glm::vec3(x, y, z), glm::vec3(0.0f, 0.0f, -1.0f) }; }

		gizmo::Frame world_frame(float scale = 1.0f) { return gizmo::Frame{ glm::vec3(0.0f), glm::mat3(1.0f), scale }; }

		glm::mat4 view_matrix(glm::vec3 eye) { return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)); }
		glm::mat4 projection_matrix() { return glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f); }
	}

	void register_gizmo_checks()
	{
		register_render_check("gizmo_screen_ray", [](RenderCheckContext& context)
			{
				const glm::mat4 view = view_matrix(glm::vec3(0.0f, 0.0f, 4.0f));
				const glm::mat4 projection = projection_matrix();

				const gizmo::Ray center = gizmo::screen_ray(glm::vec2(32.0f, 32.0f), glm::vec2(64.0f), view, projection);
				context.check(nearly(center.Direction, glm::vec3(0.0f, 0.0f, -1.0f)), "the middle pixel looks straight ahead");
				context.check(nearly(center.Origin.x, 0.0f) && nearly(center.Origin.y, 0.0f) && center.Origin.z < 4.0f, "and starts in front of the camera, on its axis");

				const gizmo::Ray corner = gizmo::screen_ray(glm::vec2(0.0f, 0.0f), glm::vec2(64.0f), view, projection);
				context.check(corner.Direction.x < -0.3f && corner.Direction.y > 0.3f && nearly(glm::length(corner.Direction), 1.0f), "the top left pixel looks up and to the left, with a unit direction");

				const gizmo::Ray bottomRight = gizmo::screen_ray(glm::vec2(64.0f, 64.0f), glm::vec2(64.0f), view, projection);
				context.check(bottomRight.Direction.x > 0.3f && bottomRight.Direction.y < -0.3f, "the bottom right pixel looks down and to the right");

				const gizmo::Ray empty = gizmo::screen_ray(glm::vec2(10.0f), glm::vec2(0.0f), view, projection);
				context.check(finite(empty.Origin) && finite(empty.Direction), "a zero-size viewport gives a finite ray");
				const gizmo::Ray broken = gizmo::screen_ray(glm::vec2(10.0f), glm::vec2(64.0f), glm::mat4(NAN_VALUE), projection);
				context.check(finite(broken.Origin) && finite(broken.Direction), "a broken matrix gives a finite ray");
			});

		register_render_check("gizmo_screen_constant_scale", [](RenderCheckContext& context)
			{
				const glm::mat4 projection = projection_matrix();
				const float near4 = gizmo::screen_constant_scale(glm::vec3(0.0f), view_matrix(glm::vec3(0.0f, 0.0f, 4.0f)), projection, 10.0f, 64.0f);
				const float far8 = gizmo::screen_constant_scale(glm::vec3(0.0f), view_matrix(glm::vec3(0.0f, 0.0f, 8.0f)), projection, 10.0f, 64.0f);
				const float expected = 10.0f * 2.0f * 4.0f * std::tan(glm::radians(30.0f)) / 64.0f;
				context.check(nearly(near4, expected, 1e-3f), "10 pixels at distance 4 are 0.7217 world units");
				context.check(nearly(far8 / near4, 2.0f, 1e-3f), "twice the distance, twice the size");

				const float behind = gizmo::screen_constant_scale(glm::vec3(0.0f, 0.0f, 10.0f), view_matrix(glm::vec3(0.0f, 0.0f, 4.0f)), projection, 10.0f, 64.0f);
				context.check(std::isfinite(behind) && behind > 0.0f, "a point behind the camera still gives a finite positive size");
				const float onTop = gizmo::screen_constant_scale(glm::vec3(0.0f, 0.0f, 4.0f), view_matrix(glm::vec3(0.0f, 0.0f, 4.0f)), projection, 10.0f, 64.0f);
				context.check(std::isfinite(onTop) && onTop > 0.0f, "a point at the camera gives a finite positive size");
				const float broken = gizmo::screen_constant_scale(glm::vec3(NAN_VALUE), view_matrix(glm::vec3(0.0f, 0.0f, 4.0f)), projection, 10.0f, 64.0f);
				context.check(std::isfinite(broken) && broken > 0.0f, "a NaN position gives a finite positive size");
				const float orthographic = gizmo::screen_constant_scale(glm::vec3(0.0f), view_matrix(glm::vec3(0.0f, 0.0f, 4.0f)), glm::orthoRH_ZO(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 100.0f), 16.0f, 64.0f);
				context.check(nearly(orthographic, 1.0f, 1e-3f), "orthographic: 16 of 64 pixels over a 4 unit view is 1 unit");
			});

		register_render_check("gizmo_ray_plane", [](RenderCheckContext& context)
			{
				const std::optional<float> hit = gizmo::ray_plane(down_z(1.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				context.check(hit && nearly(*hit, 5.0f), "a ray down -Z hits the XY plane 5 units away");
				context.check(!gizmo::ray_plane(down_z(0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)), "a ray that runs inside the plane never hits it");
				context.check(!gizmo::ray_plane(down_z(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 1.0f)), "a plane behind the ray is not hit");
				context.check(!gizmo::ray_plane(down_z(0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f)), "a plane without a normal is not hit");
				context.check(!gizmo::ray_plane(gizmo::Ray{ glm::vec3(NAN_VALUE), glm::vec3(0.0f, 0.0f, -1.0f) }, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)), "a NaN ray hits nothing");
			});

		register_render_check("gizmo_axis_parameter", [](RenderCheckContext& context)
			{
				const gizmo::Ray crossing{ glm::vec3(2.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
				context.check(nearly(gizmo::axis_parameter(crossing, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)), 2.0f), "a ray crossing the X axis at x = 2 gives 2");
				context.check(nearly(gizmo::axis_parameter(crossing, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)), 1.0f), "relative to an axis that starts at x = 1 it gives 1");
				context.check(nearly(gizmo::axis_parameter(crossing, glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)), -2.0f), "along the reversed axis it gives -2");

				const float parallel = gizmo::axis_parameter(down_z(1.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				context.check(std::isfinite(parallel), "a ray parallel to the axis gives a finite value");
				const float broken = gizmo::axis_parameter(crossing, glm::vec3(0.0f), glm::vec3(0.0f));
				context.check(std::isfinite(broken), "an axis without a direction gives a finite value");
			});

		register_render_check("gizmo_signed_angle", [](RenderCheckContext& context)
			{
				const glm::vec3 x(1.0f, 0.0f, 0.0f);
				const glm::vec3 y(0.0f, 1.0f, 0.0f);
				const glm::vec3 z(0.0f, 0.0f, 1.0f);
				context.check(nearly(gizmo::signed_angle(x, y, z), PI * 0.5f), "X to Y about Z is +90 degrees");
				context.check(nearly(gizmo::signed_angle(x, y, -z), -PI * 0.5f), "X to Y about -Z is -90 degrees");
				context.check(nearly(gizmo::signed_angle(y, x, z), -PI * 0.5f), "Y to X about Z is -90 degrees");
				context.check(nearly(gizmo::signed_angle(x, x, z), 0.0f), "no turn is 0");
				context.check(nearly(std::abs(gizmo::signed_angle(x, -x, z)), PI, 1e-2f), "a half turn is 180 degrees either way");
				context.check(nearly(gizmo::signed_angle(glm::vec3(2.0f, 0.0f, 1.0f), glm::vec3(0.0f, 3.0f, -1.0f), z), PI * 0.5f), "components along the axis are ignored");
				context.check(nearly(gizmo::signed_angle(glm::vec3(0.0f), y, z), 0.0f), "a zero vector gives 0");
				context.check(nearly(gizmo::signed_angle(x, y, glm::vec3(0.0f)), 0.0f), "a zero axis gives 0");
				context.check(nearly(gizmo::signed_angle(glm::vec3(NAN_VALUE), y, z), 0.0f), "a NaN vector gives 0");
			});

		register_render_check("gizmo_snap", [](RenderCheckContext& context)
			{
				context.check(nearly(gizmo::snap(1.3f, 0.5f), 1.5f), "1.3 snaps to 1.5 in steps of 0.5");
				context.check(nearly(gizmo::snap(1.2f, 0.5f), 1.0f), "1.2 snaps to 1.0");
				context.check(nearly(gizmo::snap(-1.3f, 0.5f), -1.5f), "negative values snap too");
				context.check(nearly(gizmo::snap(0.7f, 0.0f), 0.7f), "a step of 0 changes nothing");
				context.check(nearly(gizmo::snap(0.7f, -1.0f), 0.7f), "a negative step changes nothing");
				context.check(nearly(gizmo::snap(0.7f, NAN_VALUE), 0.7f), "a NaN step changes nothing");
				context.check(nearly(gizmo::snap(PI * 0.3f, glm::radians(15.0f)), glm::radians(15.0f) * 4.0f, 1e-4f), "angles snap to 15 degree steps (54 degrees to 60)");
			});

		register_render_check("gizmo_pick_handle", [](RenderCheckContext& context)
			{
				using gizmo::Handle;
				using gizmo::Mode;
				const gizmo::Frame frame = world_frame();
				const float radius = 0.1f;

				// Translate: arrows along the axes, squares on the planes.
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), frame, radius) == Handle::AxisX, "a ray through the X arrow picks X");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.0f, 0.8f), frame, radius) == Handle::AxisY, "a ray through the Y arrow picks Y");
				context.check(gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) }, frame, radius) == Handle::AxisZ, "a ray down the Z arrow picks Z");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.45f, 0.45f), frame, radius) == Handle::PlaneXY, "a ray through the XY square picks the XY plane");
				context.check(gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(5.0f, 0.45f, 0.45f), glm::vec3(-1.0f, 0.0f, 0.0f) }, frame, radius) == Handle::PlaneYZ, "a ray through the YZ square picks the YZ plane");
				context.check(gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(0.45f, 5.0f, 0.45f), glm::vec3(0.0f, -1.0f, 0.0f) }, frame, radius) == Handle::PlaneXZ, "a ray through the XZ square picks the XZ plane");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(3.0f, 3.0f), frame, radius) == Handle::None, "a ray far from the gizmo picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.3f), frame, radius) == Handle::None, "a ray between an arrow and a square picks nothing");

				// Scale: cubes on the axes and a uniform handle in the middle.
				context.check(gizmo::pick_handle(Mode::Scale, down_z(0.9f, 0.0f), frame, radius) == Handle::AxisX, "scale: the X handle");
				context.check(gizmo::pick_handle(Mode::Scale, down_z(0.0f, 0.0f), frame, radius) == Handle::Uniform, "scale: the middle is the uniform handle");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.0f, 0.0f), frame, radius) != Handle::Uniform, "translate has no uniform handle");

				// Rotate: rings of radius 1 around the axes.
				context.check(gizmo::pick_handle(Mode::Rotate, down_z(1.0f, 0.0f), frame, radius) == Handle::AxisZ, "rotate: the ring around Z");
				context.check(gizmo::pick_handle(Mode::Rotate, down_z(0.0f, 0.0f), frame, radius) == Handle::None, "rotate: the middle of the ring is nothing");
				context.check(gizmo::pick_handle(Mode::Rotate, gizmo::Ray{ glm::vec3(5.0f, 0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f) }, frame, radius) == Handle::AxisX, "rotate: the ring around X");

				// A gizmo of another size and place.
				const gizmo::Frame big{ glm::vec3(10.0f, 0.0f, 0.0f), glm::mat3(1.0f), 4.0f };
				context.check(gizmo::pick_handle(Mode::Translate, down_z(13.2f, 0.0f), big, radius) == Handle::AxisX, "the handles scale with the frame");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), big, radius) == Handle::None, "and move with it");

				// Local axes: the frame turned a quarter about Z puts the X handle on world Y.
				const gizmo::Frame turned{ glm::vec3(0.0f), glm::mat3(glm::rotate(glm::mat4(1.0f), PI * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f))), 1.0f };
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.0f, 0.8f), turned, radius) == Handle::AxisX, "local X lies along world Y in a turned frame");

				// Bad input.
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), world_frame(0.0f), radius) == Handle::None, "a zero-size gizmo picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), world_frame(-1.0f), radius) == Handle::None, "a negative size picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), gizmo::Frame{ glm::vec3(NAN_VALUE), glm::mat3(1.0f), 1.0f }, radius) == Handle::None, "a NaN origin picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(NAN_VALUE), glm::vec3(0.0f, 0.0f, -1.0f) }, frame, radius) == Handle::None, "a NaN ray picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f) }, frame, radius) == Handle::None, "a ray without a direction picks nothing");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(3.0f, 3.0f), frame, 0.0f) == Handle::None, "a zero hit radius still misses what is far away");
				context.check(gizmo::pick_handle(Mode::Translate, down_z(0.8f, 0.0f), frame, NAN_VALUE) == Handle::None, "a NaN hit radius picks nothing");
				const Handle inside = gizmo::pick_handle(Mode::Translate, gizmo::Ray{ glm::vec3(0.5f, 0.1f, 0.1f), glm::vec3(0.0f, 0.0f, -1.0f) }, frame, radius);
				context.check(static_cast<int>(inside) <= static_cast<int>(Handle::Uniform), "a ray that starts inside the gizmo gives a valid answer");
			});
	}
}
