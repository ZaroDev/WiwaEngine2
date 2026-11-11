# Navigation: navmesh and agents (Recast/Detour) - design

Status: draft for review. Branch `ai-nav`. Sub-project 1 of 2; sub-project 2 (behaviour trees, `.wbt`) gets its own spec and builds on this one.

## Goal

AI navigation on a navigation mesh: baked in the editor from scene geometry with Recast, stored as a `.wnav` asset, queried at run time with Detour, with agents steered by DetourCrowd. Includes the Lua API and the editor tools to author, bake and inspect it.

## Decisions (agreed)

- Vendor recastnavigation (Recast, Detour, DetourCrowd, DetourTileCache not included) under `engine/vendor/recastnavigation`, unedited. Added to premake.
- Baking (Recast) lives only in `editor/src/asset_pipeline`. The engine links Detour and DetourCrowd only; `rc*` never appears in `engine/`.
- `dt*` types stay inside `engine/src/nav/` (like `JPH::` in physics).
- Navigation runs on the main thread (scene, ECS, Lua are main thread). Query data is immutable after load; nothing crosses to the render or physics threads.
- Runs only while the game clock plays; Editor Play runs on a clone of the edit scene, Stop discards it.
- Static navmesh only in this cut (no dynamic obstacles, no tile cache, no runtime rebuild).
- Two sub-projects: this one, then behaviour trees. The agent exposes what the BT `MoveTo` leaf will need (destination, state, arrival).

## Asset: `.wnav`

- `AssetType::NavMesh`, appended before `Last` (the enum is written to files).
- Contents: format version, build settings used (for display and staleness check), agent profile (radius, height, max climb, max slope), then the Detour navmesh as tiles (`dtNavMeshParams` + per tile blob). Areas and flags are stored per polygon by Recast.
- CPU-side only; no NVRHI. Components store `AssetRef`, never a handle.
- Created by the bake into `Assets/` next to the scene (name from the volume entity); imported/packed like other binary assets. Cooking a scene with a volume does not rebake: the baked file is the source of truth, and the Navigation panel flags a stale bake (hash of input geometry and settings differs).
- Details in `assets.md` (new section): importer, loader (`engine/src/assets`), metadata.

## Authoring components (`WI_COMPONENT`/`WI_FIELD`)

- `NavMeshVolume`: `AssetRef navmesh` (baked output), bounds (box in the entity's transform), agent radius/height/max climb/max slope, cell size, cell height, region min size and merge size, edge max length and error, verts per poly, detail sample distance and error, tile size, source layer mask (which physics/render layers contribute). Defaults suit a 1.8 m humanoid.
- `NavArea`: `area` enum (`Walkable`, `NotWalkable`, `Mud`, `Water`, `Custom0..3`), applied to the entity's mesh triangles during the bake. Area costs live in project settings (`NavSettings`: cost per area, default filter).
- `NavAgent`: `speed`, `acceleration`, `radius`, `height`, `stopping_distance`, `avoidance_quality`, `separation_weight`, `area_mask`, `auto_repath`. Runtime state (`Idle`, `Moving`, `Arrived`, `NoPath`) is not serialized.

Fields feed serializer, inspector, overrides, clone, Lua and stubs from the one description. Add fields there only.

## Bake (editor)

`editor/src/asset_pipeline/nav_bake.*`:

1. Gather world-space triangles of renderable meshes inside the volume bounds, filtered by layer mask; skinned meshes use bind pose; `NavArea` tags set per-triangle area ids. Physics shapes are not used (render geometry is the source, one source of truth).
2. Run the standard Recast pipeline (heightfield, filters, compact heightfield, erode by radius, regions, contours, poly mesh, detail mesh), building Detour tile data with `dtCreateNavMeshData`.
3. Serialize to `.wnav` atomically; update `NavMeshVolume::navmesh`.
4. Bake runs off the main thread (immutable input snapshot), reports progress and the Recast build log through `WI_ASSET_LOG`. Failure leaves the previous asset in place.

Determinism: same inputs and settings produce byte-identical output (needed for the smoke and for stable GUIDs/hashes).

## Runtime (engine)

`engine/src/nav/`:

- `nav_module.*`: `IModule`, update priority 5 (after scripts at 0 so destinations set this frame take effect, before animation at 10 so animators read the moved transform). Owns the active `NavData`, a `dtNavMeshQuery`, and a `dtCrowd`. Main thread only; never called from render or physics threads.
- `nav_data.*`: loads a `.wnav` asset into a `dtNavMesh`; immutable afterwards.
- `nav_query.*`: plain-data API, no `dt*` in the header: `find_path(from, to, filter) -> NavPath` (corners, status: `Complete`/`Partial`/`Failed`), `nearest_point(p, extents)`, `raycast(a, b)`, `random_point_around(p, r)`, `is_walkable(p)`.
- `nav_agents.*`: syncs `NavAgent` components with crowd agents (add on scene start or component creation, remove on destruction, including clones), `set_destination`, `stop`, `warp`. After the crowd update, writes position and facing to the transform; exposes desired velocity so a physics character body can consume it instead of teleporting the transform (opt-in `drive_transform = false`).
- The scene's active navmesh is selected by the first enabled `NavMeshVolume`; none loaded means queries fail cleanly with a logged warning once.
- Nav layers on the module: `NavFilter` built from the project's area costs plus the agent's `area_mask`.

Engine registration: `Engine` ([engine.cpp](../../../engine/src/core/engine.cpp)); the module table in CLAUDE.md gets the new priority.

## Lua

New `Nav` table: `find_path`, `nearest_point`, `raycast`, `random_point`. Agent methods on the entity's `NavAgent` view: `set_destination(v3)`, `stop()`, `warp(v3)`, `state()`, `remaining_distance()`, `velocity()`. Arrival and failure raise the existing script events pattern (`on_nav_arrived`, `on_nav_failed`) per `scripting.md`. LuaLS stubs generated from the same description; `docs/scripting.md` updated.

## Editor tools

- **Navigation panel** (`editor/src/nav/`): volume list, build settings with tooltips and presets (Humanoid, Small, Large), Bake button with progress and log, stats (tiles, polys, verts, size, bake time), stale-bake warning, area cost table (writes `NavSettings`).
- **Viewport overlay** through `DebugDraw`: filled navmesh polys coloured by area, polygon edges, boundary edges highlighted, toggle in the panel. Overlay reads the loaded `.wnav`, not the live bake, so it shows exactly what runs.
- **Path test tool**: click start and end in the viewport; draws the Detour path and corner points, shows status and length. Pure edit-time, uses a standalone `NavData`.
- **Volume gizmo**: box handles for the bounds (reusing the existing gizmo system).
- Inspector UI for the three components comes from `ui_fields()`; `NavArea` gets a colour-coded dropdown.
- Details go in `editor.md` (a new section).

## Demo

`gen_demo_scene.py` adds a walled arena in the demo hall with obstacles, a `NavMeshVolume` with a baked `.wnav` (committed, force-added if ignored), a few `NavAgent` entities walking between waypoints via a Lua script, and a visible mud area. `--demo-smoke` is extended: agents reach their waypoint within N seconds.

## Verification

- `--nav-smoke` (editor): bake the fixture scene and compare to a golden polygon/vert count; round trip `.wnav` (save, load, identical queries); `find_path` on a known layout (valid, endpoints within tolerance, length within bound); blocked route returns `Failed`/`Partial`; area cost changes route choice; crowd agent reaches a target and reports `Arrived`; clone/Stop teardown leaves no agents; Lua API. **Negative control required**: with the obstacle removed from the fixture the path-length check must fail, and with a corrupted `.wnav` load must fail.
- `--nav-panel-smoke` (editor): panel and overlay for 130 frames, ImGui asserts on.
- Both documented in the CLAUDE.md smoke table. Per repo rules, Release builds, and smokes only run when requested.

## Risks and notes

- Recast needs a clean triangle soup: non-manifold and tiny triangles are tolerated by Recast, but huge meshes are slow; bake is off-thread with a cancel.
- Scene units are metres, Y up; Recast is the same convention, no axis swap.
- Recast/Detour are fetched from GitHub (a release tag, pinned in the plan). If unreachable the plan stops at vendoring.
- Windows macros `near`/`far` must not be used as identifiers in the new code.
- Dynamic obstacles, off-mesh links, tile cache and runtime rebuild are explicitly out of scope; the data format reserves a version field so they can be added.

## Out of scope (this sub-project)

Behaviour trees, blackboards, AI perception, animation-driven locomotion (agents move the transform; a `Speed` value is exposed for graphs to read), multiple simultaneous navmeshes per scene.
