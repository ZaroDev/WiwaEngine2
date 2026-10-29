#include <editor_common.h>
#include "gizmo_smoke_test.h"

#include <cmath>
#include <limits>

#include <core/engine.h>
#include <renderer/render_module.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <scene/scene_components.h>
#include <scene/scene_editor.h>
#include <scene/scene_module.h>

#include "gizmo_settings.h"
#include "scene_gizmos.h"
#include "gizmo_apply.h"
#include "viewport_selection.h"

#define WI_GIZMO_LOG(level, ...) WI_LOG(level, "Editor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_GIZMO_LOG(Error, "Gizmo smoke test FAILED: {0}", what);
				}
			}
		};

		constexpr glm::vec2 VIEWPORT(512.0f, 512.0f);
		constexpr float TOLERANCE = 2e-3f;

		bool near_to(float a, float b, float tolerance = TOLERANCE) { return std::abs(a - b) <= tolerance; }
		bool near_to(const glm::vec3& a, const glm::vec3& b, float tolerance = TOLERANCE) { return glm::length(a - b) <= tolerance; }
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		bool finite(const glm::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

		struct Camera
		{
			glm::mat4 View{ 1.0f };
			glm::mat4 Projection{ 1.0f };
		};

		Camera make_camera(glm::vec3 eye, glm::vec3 target)
		{
			return Camera{ glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f)), glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) };
		}

		// Everything a check needs: a scene in memory and a camera.
		class Rig
		{
		public:
			explicit Rig(glm::vec3 eye = glm::vec3(0.0f, 0.0f, 6.0f), glm::vec3 target = glm::vec3(0.0f))
				: Editor(*Engine::get().get_module<AssetManager>(), *Engine::get().get_module<SceneModule>()), Cam(make_camera(eye, target))
			{
				Editor.set_scene(CreateRef<Scene>(), {});
			}

			entt::registry& registry() { return Editor.scene().registry(); }

			entt::entity make(glm::vec3 position, entt::entity parent = entt::null)
			{
				const entt::entity entity = Editor.create_entity(parent);
				Editor.set_field(entity, "Transform", "Position", FieldValue(position));
				Editor.scene().update_world_transforms();
				return entity;
			}

			glm::vec3 position(entt::entity entity) { return registry().get<Transform>(entity).Position; }
			glm::vec3 scale(entt::entity entity) { return registry().get<Transform>(entity).Scale; }
			glm::quat rotation(entt::entity entity) { return registry().get<Transform>(entity).Rotation; }
			glm::vec3 world_position(entt::entity entity) { return glm::vec3(registry().get<WorldTransform>(entity).Matrix[3]); }

			SceneEditor Editor;
			Camera Cam;
		};

		void check_selection_helpers(Check& check)
		{
			Rig rig;
			const entt::entity a = rig.make(glm::vec3(0.0f));
			const entt::entity b = rig.make(glm::vec3(1.0f));
			check(!selected_guid(rig.Editor), "nothing selected, no guid");

			rig.Editor.select(a);
			check(selected_guid(rig.Editor) == rig.registry().get<Id>(a).Guid, "the selected entity's guid");

			apply_pick(rig.Editor, PickResult{ 1, rig.registry().get<Id>(b).Guid });
			check(rig.Editor.selected() == b, "a pick that found an entity selects it");
			apply_pick(rig.Editor, PickResult{ 2, std::nullopt });
			check(rig.Editor.selected() == entt::null && !selected_guid(rig.Editor), "a pick that found nothing clears the selection");

			rig.Editor.select(a);
			apply_pick(rig.Editor, PickResult{ 3, EntityGuid{ 0xDEAD, 0xBEEF } });
			check(rig.Editor.selected() == entt::null, "a pick that found an entity that is not in the scene clears the selection");

			rig.Editor.select(a);
			rig.Editor.delete_entity(a);
			check(!selected_guid(rig.Editor), "a deleted selection has no guid");
		}

		glm::quat world_rotation(Rig& rig, entt::entity entity)
		{
			const glm::mat4 world = rig.registry().get<WorldTransform>(entity).Matrix;
			return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])), glm::normalize(glm::vec3(world[2])))));
		}

		// The editor runs with a renderer: the collector it draws into takes shapes.
		void check_renderer_debug_draw(Check& check)
		{
			RenderModule* render = Engine::get().get_module<RenderModule>();
			check(render != nullptr && render->is_ready(), "the renderer is running in the editor process");
			if (render != nullptr && render->is_ready())
			{
				check(render->get_debug_draw().enabled(), "the debug draw collector takes shapes while the renderer runs");
			}
		}

		void check_gizmo_apply(Check& check)
		{
			// The display matrix: an entity with no scale still has axes to show.
			{
				const glm::mat4 plain = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
				check(gizmo_display_matrix(plain) == plain, "an ordinary matrix is shown as it is");

				const glm::mat4 flat = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)))
					* glm::scale(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 1.0f));
				const glm::mat4 shown = gizmo_display_matrix(flat);
				check(near_to(glm::length(glm::vec3(shown[0])), 1.0f) && near_to(glm::vec3(shown[0]), glm::vec3(0.0f, 1.0f, 0.0f)) && near_to(glm::vec3(shown[3]), glm::vec3(5.0f, 0.0f, 0.0f)),
					"an axis of no length is shown as a unit axis in its direction (here the X axis turned a quarter), the position stays");
				check(near_to(glm::length(glm::vec3(shown[0])), 1.0f), "and a zero axis with no direction gets the unit axis");
				const glm::mat4 empty = glm::mat4(glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
				const glm::mat4 emptyShown = gizmo_display_matrix(empty);
				check(near_to(glm::vec3(emptyShown[0]), glm::vec3(1.0f, 0.0f, 0.0f)) && near_to(glm::vec3(emptyShown[1]), glm::vec3(0.0f, 1.0f, 0.0f)) && near_to(glm::vec3(emptyShown[2]), glm::vec3(0.0f, 0.0f, 1.0f)),
					"a matrix with no axes at all is shown with the world axes");
				const glm::mat4 broken = gizmo_display_matrix(glm::mat4(std::numeric_limits<float>::quiet_NaN()));
				check(finite(glm::vec3(broken[0])) && finite(glm::vec3(broken[3])), "a NaN matrix is shown as a finite one");
			}

			// Translate: the entity follows the gizmo, through its parent's space.
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(1.0f, 0.0f, 0.0f));
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(entity).Matrix);
				check(apply_gizmo_result(rig.Editor, entity, GizmoOperation::Translate, before, glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)) * before)
					&& near_to(rig.position(entity), glm::vec3(3.0f, 0.0f, 0.0f)), "translating the gizmo by 2 moves the entity by 2");
				check(near_to(rig.scale(entity), glm::vec3(1.0f)) && near_to(rig.rotation(entity).w, 1.0f), "and changes nothing else");
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 now = gizmo_display_matrix(rig.registry().get<WorldTransform>(entity).Matrix);
				check(!apply_gizmo_result(rig.Editor, entity, GizmoOperation::Translate, now, now), "a gizmo that did not move writes nothing");
			}

			{
				// A child of a parent that is scaled by 2 and turned: a world move of 2 along X is a local move that undoes the parent's turn and scale.
				Rig rig;
				const entt::entity parent = rig.make(glm::vec3(10.0f, 0.0f, 0.0f));
				rig.Editor.set_field(parent, "Transform", "Scale", FieldValue(glm::vec3(2.0f)));
				const entt::entity child = rig.make(glm::vec3(1.0f, 0.0f, 0.0f), parent);
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(child).Matrix);
				check(apply_gizmo_result(rig.Editor, child, GizmoOperation::Translate, before, glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)) * before), "a child can be moved");
				rig.Editor.scene().update_world_transforms();
				check(near_to(rig.world_position(child), glm::vec3(14.0f, 0.0f, 0.0f)) && near_to(rig.position(child), glm::vec3(2.0f, 0.0f, 0.0f)),
					"2 in the world is 1 in the space of a parent scaled by 2: local x goes from 1 to 2");
			}

			// Rotate: about the axis the gizmo turned it, in the world.
			{
				Rig rig;
				const entt::entity parent = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(parent, "Transform", "Rotation", FieldValue(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
				const entt::entity child = rig.make(glm::vec3(0.0f, 0.0f, 0.0f), parent);
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(child).Matrix);
				const glm::quat turn = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				check(apply_gizmo_result(rig.Editor, child, GizmoOperation::Rotate, before, glm::mat4_cast(turn) * before), "a child can be turned");
				rig.Editor.scene().update_world_transforms();
				const glm::quat expected = turn * glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				check(std::abs(glm::dot(world_rotation(rig, child), expected)) > std::cos(glm::radians(0.5f)), "its world rotation is the turn applied on top of what it was, whatever the parent's turn");
				check(near_to(rig.position(child), glm::vec3(0.0f)) && near_to(rig.scale(child), glm::vec3(1.0f)), "turning changes neither position nor scale");
			}

			// Scale: by the factor the gizmo scaled each axis.
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(entity, "Transform", "Scale", FieldValue(glm::vec3(1.0f, 2.0f, 3.0f)));
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(entity).Matrix);
				check(apply_gizmo_result(rig.Editor, entity, GizmoOperation::Scale, before, before * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.5f)))
					&& near_to(rig.scale(entity), glm::vec3(2.0f, 2.0f, 1.5f)), "scaling the gizmo by (2, 1, 0.5) scales the entity's (1, 2, 3) to (2, 2, 1.5)");
				check(near_to(rig.position(entity), glm::vec3(0.0f)), "and does not move it");
			}

			// An entity whose scale was zeroed can be scaled up again.
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(entity, "Transform", "Scale", FieldValue(glm::vec3(0.0f, 1.0f, 1.0f)));
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(entity).Matrix);
				check(apply_gizmo_result(rig.Editor, entity, GizmoOperation::Scale, before, before * glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, 1.0f))), "an entity with no scale on X can be scaled");
				check(finite(rig.scale(entity)) && rig.scale(entity).x > 0.0f && near_to(rig.scale(entity).y, 1.0f), "and gets a scale on X again (" + std::to_string(rig.scale(entity).x) + ")");
			}

			// Input that cannot be used writes nothing.
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(1.0f, 2.0f, 3.0f));
				const glm::mat4 before = gizmo_display_matrix(rig.registry().get<WorldTransform>(entity).Matrix);
				const glm::mat4 nan(std::numeric_limits<float>::quiet_NaN());
				check(!apply_gizmo_result(rig.Editor, entity, GizmoOperation::Translate, before, nan) && !apply_gizmo_result(rig.Editor, entity, GizmoOperation::Rotate, before, nan)
					&& !apply_gizmo_result(rig.Editor, entity, GizmoOperation::Scale, before, nan), "a NaN result writes nothing");
				check(near_to(rig.position(entity), glm::vec3(1.0f, 2.0f, 3.0f)) && near_to(rig.scale(entity), glm::vec3(1.0f)), "and the entity is as it was");

				check(!apply_gizmo_result(rig.Editor, entt::null, GizmoOperation::Translate, before, before), "no entity: nothing");
				const entt::entity gone = rig.make(glm::vec3(0.0f));
				rig.Editor.delete_entity(gone);
				check(!apply_gizmo_result(rig.Editor, gone, GizmoOperation::Translate, before, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * before), "a deleted entity: nothing");

				// A parent with no scale cannot be undone: the entity is left alone.
				const entt::entity flat = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(flat, "Transform", "Scale", FieldValue(glm::vec3(0.0f)));
				const entt::entity under = rig.make(glm::vec3(1.0f, 0.0f, 0.0f), flat);
				rig.Editor.scene().update_world_transforms();
				const glm::mat4 underBefore = gizmo_display_matrix(rig.registry().get<WorldTransform>(under).Matrix);
				check(!apply_gizmo_result(rig.Editor, under, GizmoOperation::Translate, underBefore, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * underBefore) && finite(rig.position(under)),
					"a parent with no scale cannot be undone: the child is left alone, with no NaN");
			}
		}

		size_t line_count(const DebugDrawData& data) { return data.Lines[0].size() + data.Lines[1].size(); }

		entt::entity make_light(Rig& rig, glm::vec3 position, LightKind kind, float range = 10.0f)
		{
			const entt::entity entity = rig.make(position);
			rig.Editor.add_component(entity, "Light");
			rig.Editor.set_field(entity, "Light", "Kind", FieldValue(static_cast<i32>(kind)));
			rig.Editor.set_field(entity, "Light", "Range", FieldValue(range));
			return entity;
		}

		void check_scene_gizmos(Check& check)
		{
			Rig rig;
			const entt::entity point = make_light(rig, glm::vec3(1.0f, 0.0f, 0.0f), LightKind::Point);
			const entt::entity spot = make_light(rig, glm::vec3(2.0f, 0.0f, 0.0f), LightKind::Spot);
			const entt::entity sun = make_light(rig, glm::vec3(3.0f, 0.0f, 0.0f), LightKind::Directional);
			const entt::entity camera = rig.make(glm::vec3(0.0f, 0.0f, 5.0f));
			rig.Editor.add_component(camera, "Camera");
			const entt::entity mesh = rig.make(glm::vec3(0.0f, 2.0f, 0.0f));
			rig.Editor.add_component(mesh, "MeshRenderer");
			rig.Editor.scene().update_world_transforms();

			SceneGizmos gizmos([](const AssetRef&) { return std::optional<std::pair<glm::vec3, glm::vec3>>(std::pair<glm::vec3, glm::vec3>(glm::vec3(-1.0f), glm::vec3(1.0f))); });
			SceneGizmoView view;
			view.View = rig.Cam.View;
			view.Projection = rig.Cam.Projection;
			view.ViewportHeight = VIEWPORT.y;
			Scene& scene = rig.Editor.scene();
			DebugDraw draw;
			GizmoSettings settings;
			settings.Grid = false; // counted on their own below
			settings.Axes = false;

			// Nothing selected: a marker for every light (3 lines), the sun's arrow (5), the camera's marker (3) and frustum (12).
			gizmos.draw(draw, scene, entt::null, settings, view);
			DebugDrawData data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12, "nothing selected: light markers, the sun's arrow, the camera's marker and frustum (" + std::to_string(line_count(data)) + " lines)");
			check(data.PickIcons.size() == 4, "every light and camera can be picked");
			check(data.Triangles[0].empty() && data.Triangles[1].empty(), "no triangles are drawn");

			// The ground grid: 21 lines each way, shown faded (so each line is in both depth layers), flat at y = 0.
			GizmoSettings withGrid = settings;
			withGrid.Grid = true;
			gizmos.draw(draw, scene, entt::null, withGrid, view);
			data = draw.seal(0.0f);
			u32 flat = 0;
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					flat += near_to(line.A.y, 0.0f, 1e-5f) && near_to(line.B.y, 0.0f, 1e-5f) && glm::length(line.A - line.B) > 5.0f ? 1u : 0u;
				}
			}
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 2 * 42 && flat == 2 * 42, "the grid adds 42 lines on the ground, each in both layers (" + std::to_string(line_count(data)) + " lines, " + std::to_string(flat) + " flat)");

			// The world axes: three lines from the origin along X, Y and Z, faded (each in both layers).
			GizmoSettings withAxes = settings;
			withAxes.Axes = true;
			gizmos.draw(draw, scene, entt::null, withAxes, view);
			data = draw.seal(0.0f);
			u32 fromOrigin[3] = { 0, 0, 0 };
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					if (near_to(line.A, glm::vec3(0.0f), 1e-5f) && glm::length(line.B) > 0.5f)
					{
						const glm::vec3 direction = glm::normalize(line.B);
						for (int axis = 0; axis < 3; axis++)
						{
							fromOrigin[axis] += near_to(direction[axis], 1.0f, 1e-4f) ? 1u : 0u;
						}
					}
				}
			}
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 2 * 3 && fromOrigin[0] == 2 && fromOrigin[1] == 2 && fromOrigin[2] == 2,
				"the axes add a line from the origin along X, Y and Z, each in both layers (" + std::to_string(line_count(data)) + " lines)");

			// A selected point light also shows its range: a sphere (3 circles of 32 segments).
			gizmos.draw(draw, scene, point, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 96, "a selected point light adds its range sphere (" + std::to_string(line_count(data)) + " lines)");

			// A selected spot light shows its cone (a base circle of 32 segments and 8 side lines).
			gizmos.draw(draw, scene, spot, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 40, "a selected spot light adds its cone (" + std::to_string(line_count(data)) + " lines)");

			// The light points down -Z (its forward axis): the 8 lines from the apex reach the base, a range of 10 away.
			const glm::vec3 apex(2.0f, 0.0f, 0.0f);
			u32 sides = 0;
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					const bool fromApex = near_to(line.A, apex, 1e-4f);
					const glm::vec3& other = fromApex ? line.B : line.A;
					if ((fromApex || near_to(line.B, apex, 1e-4f)) && near_to(other.z, -10.0f, 1e-3f))
					{
						sides++;
					}
				}
			}
			check(sides == 8, "the spot light's cone opens along its forward axis, 10 units deep (" + std::to_string(sides) + " side lines)");

			// A selected sun has nothing more than every sun has.
			gizmos.draw(draw, scene, sun, settings, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "a selected sun adds nothing");

			// A selected mesh shows its world bounds (12 lines).
			gizmos.draw(draw, scene, mesh, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 12, "a selected mesh adds its bounds (" + std::to_string(line_count(data)) + " lines)");

			// A mesh that has not loaded has no bounds to show.
			SceneGizmos unloaded([](const AssetRef&) { return std::optional<std::pair<glm::vec3, glm::vec3>>(); });
			unloaded.draw(draw, scene, mesh, settings, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "a mesh that has not loaded shows no bounds");

			// The categories switch off separately.
			GizmoSettings noLights = settings;
			noLights.Lights = false;
			gizmos.draw(draw, scene, point, noLights, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 + 12 && data.PickIcons.size() == 1, "lights off: only the camera is left, with its icon (" + std::to_string(line_count(data)) + " lines)");

			GizmoSettings noCameras = settings;
			noCameras.Cameras = false;
			gizmos.draw(draw, scene, entt::null, noCameras, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 && data.PickIcons.size() == 3, "cameras off: only the lights are left");

			GizmoSettings noBounds = settings;
			noBounds.Bounds = false;
			gizmos.draw(draw, scene, mesh, noBounds, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "bounds off: a selected mesh shows nothing extra");

			// Values that cannot be drawn are dropped, not drawn.
			rig.Editor.set_field(point, "Light", "Range", FieldValue(0.0f));
			rig.Editor.set_field(spot, "Light", "Range", FieldValue(-1.0f));
			gizmos.draw(draw, scene, point, settings, view);
			gizmos.draw(draw, scene, spot, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 2 * (3 * 3 + 5 + 3 + 12), "a light with no range shows no sphere or cone");

			// A view that cannot size the markers falls back to a size, and nothing is NaN.
			SceneGizmoView broken;
			broken.View = glm::mat4(std::numeric_limits<float>::quiet_NaN());
			gizmos.draw(draw, scene, entt::null, settings, broken);
			data = draw.seal(0.0f);
			bool allFinite = true;
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					allFinite = allFinite && finite(line.A) && finite(line.B);
				}
			}
			check(allFinite && line_count(data) > 0, "a view that cannot be used still gives finite markers");

			// An entity that was deleted, as the selection.
			const entt::entity gone = rig.make(glm::vec3(0.0f));
			rig.Editor.delete_entity(gone);
			gizmos.draw(draw, scene, gone, settings, view);
			check(line_count(draw.seal(0.0f)) > 0, "a stale selection is ignored");
		}

		void check_gizmo_settings(Check& check)
		{
			namespace fs = std::filesystem;
			const fs::path file = fs::temp_directory_path() / "wiwa_gizmo_settings_smoke.json";
			std::error_code error;
			fs::remove(file, error);

			check(GizmoSettings::load(file) == GizmoSettings{}, "a missing file gives the defaults");

			GizmoSettings changed;
			changed.Lights = false;
			changed.Cameras = false;
			changed.Bounds = false;
			changed.Grid = false;
			changed.Axes = false;
			changed.TransformHandles = false;
			changed.Outline.Color = glm::vec3(0.1f, 0.2f, 0.3f);
			changed.Outline.WidthPx = 3.0f;
			changed.Outline.OccludedAlpha = 0.6f;
			changed.Outline.Enabled = false;
			check(changed.save(file), "the settings can be saved");
			check(GizmoSettings::load(file) == changed, "and load back as they were");

			auto write = [&](const std::string& text)
			{
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				out << text;
			};

			write("this is not json {{{");
			check(GizmoSettings::load(file) == GizmoSettings{}, "a corrupt file gives the defaults");
			write("");
			check(GizmoSettings::load(file) == GizmoSettings{}, "an empty file gives the defaults");
			write("[1, 2, 3]");
			check(GizmoSettings::load(file) == GizmoSettings{}, "json of the wrong shape gives the defaults");

			write(R"({"lights": false})");
			GizmoSettings partial = GizmoSettings::load(file);
			check(!partial.Lights && partial.Cameras && partial.Bounds && partial.Grid && partial.Axes && partial.TransformHandles && partial.Outline == SelectionOutlineStyle{}, "keys that are missing keep their defaults");
			check(GizmoSettings{}.Lights && GizmoSettings{}.Cameras && GizmoSettings{}.Bounds && GizmoSettings{}.Grid && GizmoSettings{}.Axes && GizmoSettings{}.TransformHandles && GizmoSettings{}.Outline.Enabled, "everything is shown by default");

			write(R"({"lights": "yes", "outline": {"width": "wide", "color": [1, 2], "occludedAlpha": null}})");
			check(GizmoSettings::load(file) == GizmoSettings{}, "values of the wrong type are ignored");

			write(R"({"outline": {"width": 100, "occludedAlpha": 7, "color": [5, -5, 0.5]}})");
			const GizmoSettings clamped = GizmoSettings::load(file);
			check(clamped.Outline.WidthPx == 8.0f && clamped.Outline.OccludedAlpha == 1.0f && clamped.Outline.Color == glm::vec3(1.0f, 0.0f, 0.5f), "out-of-range values are clamped");

			write(R"({"outline": {"width": 0}})");
			check(GizmoSettings::load(file).Outline.WidthPx == 1.0f, "a width of 0 becomes the smallest width");

			fs::remove(file, error);
			check(!changed.save(fs::temp_directory_path() / "wiwa_no_such_folder_xyz" / "settings.json"), "saving where it cannot fails without a crash");
		}
	}

	bool run_gizmo_smoke_test(AssetPipelineModule& pipeline)
	{
		register_scene_components();
		Check check;

		check_selection_helpers(check);
		check_gizmo_apply(check);
		check_renderer_debug_draw(check);
		check_scene_gizmos(check);
		check_gizmo_settings(check);

		if (check.Failures == 0)
		{
			WI_GIZMO_LOG(Info, "Gizmo smoke test passed");
		}
		return check.Failures == 0;
	}
}
