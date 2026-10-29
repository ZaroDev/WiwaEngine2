#include <pch.h>
#include "render_test.h"

#include <cmath>
#include <limits>

#include <math/gizmo_math.h>

// Checks of the gizmo math (what keeps a marker the same size on screen). Pure functions, no GPU.
namespace wiwa
{
	namespace
	{
		constexpr float TOLERANCE = 1e-3f;
		const float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();

		bool nearly(float a, float b, float tolerance = TOLERANCE) { return std::abs(a - b) <= tolerance; }
		glm::mat4 view_matrix(glm::vec3 eye) { return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)); }
		glm::mat4 projection_matrix() { return glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f); }
	}

	void register_gizmo_checks()
	{
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
	}
}
