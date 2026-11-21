#!/usr/bin/env python3
"""Writes the stress hall: editor/assets/scenes/stress.wscene and the models it uses (editor/assets/models/stress/**), a scene that is meant to
keep the engine at the edge of 60 FPS: the Sponza atrium (262k triangles, 25 materials, 69 textures), a plaza of several thousand props, ships of
150k triangles, a crowd of animated skinned characters, hundreds of point lights, shadow casting spot lights, particle fires and a wall of
physics crates, seen by a camera that flies through the atrium, out over the plaza and back (a loop of about a minute).

    python scripts/gen_stress_scene.py [cache_dir] [--scale=4.0] [--seed=1]

The models are imported with levels of detail (the importer's lodLevels, so the ships and the props draw fewer triangles with distance; RenderQuality
lodPixelError sets how soon) and the static things (Sponza, the plaza, the props, the ships) are marked Static; the crowd is skinned and animated, so it is
not. The bench log shows how many primitives were drawn at each level.

Run scripts/gen_demo_scene.py first: the stress hall reuses its characters, materials, particle effects, scripts and sky (by their guids).
--scale multiplies the counts of props, characters, lights, fires and crates (4.0, the default, is the size measured at 60.1 FPS on average, 46 FPS 0.1% low, on an RTX 3070 Ti with vsync off, Vulkan; 3.0 gave 78 FPS).

Models (downloaded once into cache_dir, default .demo_cache):
- Sponza, Khronos glTF-Sample-Assets (Crytek's Sponza, textures by Alexandre Pestana). NOT CC0: its license is in the folder written here. These
  files, the other models and the generated scene are git-ignored on purpose; only this script is committed.
- ship_pinnace, treasure_chest, Chandelier_01, Lantern_01, vintage_cabinet_01, Barrel_01, wooden_crate_01 from Poly Haven (polyhaven.com), CC0.

Measure with scripts/bench_stress.py (it packs the scene and runs the game with --bench).
"""
import json
import math
import pathlib
import random
import sys
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_demo_scene as demo  # noqa: E402  (its helpers; importing it generates nothing)

ROOT = demo.ROOT
FLAGS = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
SCALE = float(FLAGS.get("scale", "4.0"))
SEED = int(FLAGS.get("seed", "1"))

SPONZA_API = "https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models/Sponza/glTF"
SPONZA_RAW = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Sponza/glTF/"
POLY_PROPS = ("ship_pinnace", "treasure_chest", "Chandelier_01", "Lantern_01", "vintage_cabinet_01", "Barrel_01", "wooden_crate_01")

PLAZA_Z0 = 14.0     # the plaza starts south of the atrium
PLAZA_SIZE = 200.0  # and is this wide and long


def write_model(rel, importer_settings=None):
	# lodLevels/lodReduction: the importer bakes levels of detail into the static meshes (3 levels, each half the triangles of the one before)
	return demo.write_meta(rel, "ModelImporter", 10, importer_settings or {"generatePhysicsShapes": False, "lodLevels": 3, "lodReduction": 0.5})


def import_sponza():
	listing = json.loads(demo.download(SPONZA_API, "sponza_listing.json").read_text())
	for item in listing:
		path = demo.download(item["download_url"], "sponza_" + item["name"])
		demo.write("models/stress/sponza/" + item["name"], path.read_bytes(), binary=True)
	demo.write("models/stress/sponza/LICENSE.txt",
		"Sponza from the Khronos glTF-Sample-Assets (https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza).\n"
		"Crytek's Sponza (https://www.cryengine.com/marketplace/product/crytek/sponza-sample-scene), PBR textures by Alexandre Pestana\n"
		"(http://www.alexandre-pestana.com/pbr-textures-sponza/); the glTF conversion is by the Khronos Group. See the README of the model for the license:\n"
		"https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Sponza/README.md. Not CC0: do not redistribute it with the engine.\n")
	return write_model("models/stress/sponza/Sponza.gltf")


def import_poly_haven(asset):
	info = json.loads(demo.download("https://api.polyhaven.com/files/" + asset, "polyhaven_%s.json" % asset).read_text())
	gltf = info["gltf"]["1k"]["gltf"]
	name = "%s_1k.gltf" % asset
	demo.write("models/stress/%s/%s" % (asset, name), demo.download(gltf["url"], "ph_%s_%s" % (asset, name)).read_bytes(), binary=True)
	for rel, entry in gltf["include"].items():
		demo.write("models/stress/%s/%s" % (asset, rel), demo.download(entry["url"], "ph_%s_%s" % (asset, rel.replace("/", "_"))).read_bytes(), binary=True)
	demo.write("models/stress/%s/LICENSE.txt" % asset, "%s from Poly Haven (https://polyhaven.com/a/%s), CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/).\n" % (asset, asset))
	return write_model("models/stress/%s/%s" % (asset, name))


STATIC_LUA = """-- Marks every mesh below the entity Static (MeshRenderer.Static): they never move, so the renderer keeps no previous matrix for them.
local MakeStatic = Script:extend{}

local function mark(entity)
  for _, child in ipairs(entity.children) do
    if child:has("MeshRenderer") then
      child:get("MeshRenderer").static = true
    end
    mark(child)
  end
end

function MakeStatic:on_init()
  if self.entity:has("MeshRenderer") then
    self.entity:get("MeshRenderer").static = true
  end
  mark(self.entity)
end

return MakeStatic
"""

CAMERA_LUA = """-- The stress hall's camera: a loop through the atrium, up out of it, over the plaza and back. Waypoints are position and the point looked at.
local PATH = {
  { { -13.5, 1.8, 0 }, { -4, 2.2, 0 } },
  { { -6, 1.8, 0.5 }, { 4, 3.0, 0 } },
  { { 3, 1.8, 0 }, { 12, 3.5, 1 } },
  { { 11, 2.5, 0 }, { 0, 6, 0 } },
  { { 6, 9, 0 }, { 0, 4, 12 } },
  { { 0, 22, 2 }, { 0, 3, 40 } },
  { { 0, 18, 22 }, { 0, 1, 60 } },
  { { -35, 12, 60 }, { 0, 1, 80 } },
  { { -38, 9, 110 }, { 0, 1, 100 } },
  { { 0, 15, 190 }, { 0, 1, 110 } },
  { { 40, 8, 120 }, { 0, 1, 70 } },
  { { 38, 12, 60 }, { 0, 2, 30 } },
  { { 10, 20, 20 }, { 0, 3, 0 } },
  { { -13.5, 1.8, 0 }, { -4, 2.2, 0 } },
}

local Camera = Script:extend{
  properties = {
    duration = { type = "float", default = 70, min = 5, max = 600 },
  },
}

local function smooth(a, b, c, d, t)
  -- Catmull-Rom through b and c
  local t2, t3 = t * t, t * t * t
  return 0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
end

function Camera:on_init()
  self.t = 0
end

function Camera:on_update(dt)
  self.t = (self.t + dt) % self.duration
  local segments = #PATH - 1
  local f = self.t / self.duration * segments
  local i = math.floor(f)
  local t = f - i
  local function at(k) return PATH[math.min(#PATH, math.max(1, k + 1))] end
  local p0, p1, p2, p3 = at(i - 1), at(i), at(i + 1), at(i + 2)
  local pos, look = {}, {}
  for axis = 1, 3 do
    pos[axis] = smooth(p0[1][axis], p1[1][axis], p2[1][axis], p3[1][axis], t)
    look[axis] = smooth(p0[2][axis], p1[2][axis], p2[2][axis], p3[2][axis], t)
  end
  local dx, dy, dz = look[1] - pos[1], look[2] - pos[2], look[3] - pos[3]
  local yaw = math.deg(math.atan(-dx, -dz))
  local pitch = math.deg(math.atan(dy, math.sqrt(dx * dx + dz * dz)))
  self.transform.position.x = pos[1]
  self.transform.position.y = pos[2]
  self.transform.position.z = pos[3]
  self.transform.rotation = quat.from_euler(pitch, yaw, 0)
end

return Camera
"""


def main():
	rng = random.Random(SEED)

	# --- assets ---------------------------------------------------------------------------------------------------
	models = {"Sponza": import_sponza()}
	for asset in POLY_PROPS:
		models[asset] = import_poly_haven(asset)
	camera_script = "scripts/stress_camera.lua"
	demo.write(camera_script, CAMERA_LUA)
	camera_guid = demo.write_meta(camera_script, "ScriptImporter", 1)
	static_script = "scripts/stress_static.lua"
	demo.write(static_script, STATIC_LUA)
	static_guid = demo.write_meta(static_script, "ScriptImporter", 1)
	make_static = {"script:" + static_guid: {}}

	# the demo's assets, by their guids (gen_demo_scene.py wrote them)
	assets = demo.ASSETS
	missing = [r for r in ("models/characters/Warrior.fbx.meta", "materials/ground.wmat.meta", "vfx/campfire.wfx.meta", "environments/sky.hdr.meta") if not (assets / r).exists()]
	if missing:
		sys.exit("run scripts/gen_demo_scene.py first (missing %s)" % ", ".join(missing))
	shapes = demo.guid("asset:models/shapes/shapes.glb")
	ground_material = demo.guid("asset:materials/ground.wmat")
	crate_material = demo.guid("asset:materials/crate.wmat")
	clip_player = demo.guid("asset:scripts/clip_player.lua")
	environment = demo.guid("asset:environments/sky.hdr")
	fire = demo.guid("asset:vfx/campfire.wfx")
	fountain = demo.guid("asset:vfx/spark_fountain.wfx")

	characters = {}
	for folder, names in (("characters", demo.CHARACTERS), ("monsters", demo.MONSTERS)):
		for name, wanted in names.items():
			data = (assets / "models" / folder / (name + ".fbx")).read_bytes()
			clips = demo.fbx_clips(data)
			characters[name] = (demo.guid("asset:models/%s/%s.fbx" % (folder, name)), demo.pick_clip(clips, wanted if isinstance(wanted, list) else wanted[0]))

	ref = demo.ref
	s = demo.Scene()

	def light(kind, color, intensity, rng_=10, inner=20, outer=30, shadows=False):
		return {"Light": {"Kind": kind, "Color": list(color), "Intensity": intensity, "Range": rng_, "InnerAngle": inner, "OuterAngle": outer, "CastsShadows": shadows}}

	def prefab(name):
		return {"PrefabInstance": {"Prefab": ref(models[name])}}

	# --- the camera, the sun ----------------------------------------------------------------------------------------
	s.add("Camera", pos=(-13.5, 1.8, 0), rot=demo.qaxis((0, 1, 0), -90),
		Camera={"Projection": "Perspective", "FovY": 70, "Near": 0.1, "Far": 500, "OrthoSize": 10, "Primary": True},
		**{"script:" + camera_guid: {}})
	s.add("Audio listener", "Camera", AudioListener={"Active": True})
	s.add("Sun", pos=(0, 60, 0), rot=demo.euler(-48, 28, 0), **light("Directional", (1, 0.95, 0.88, 1), 4.0, shadows=True))

	# --- the atrium and the plaza -----------------------------------------------------------------------------------
	s.add("World")
	s.add("Sponza", "World", **prefab("Sponza"), **make_static)
	plaza_centre = PLAZA_Z0 + PLAZA_SIZE / 2
	s.add("Plaza", "World", pos=(0, -0.02, plaza_centre), scale=(PLAZA_SIZE, 1, PLAZA_SIZE),
		MeshRenderer={"Mesh": ref(shapes, demo.MESH["plane_tiled"]), "Materials": [ref(ground_material)], "Static": True})
	s.add("Plaza collider", "World", pos=(0, -0.5, plaza_centre), scale=(PLAZA_SIZE, 1, PLAZA_SIZE), Collider=demo.BOX)

	# --- props: a grid of a few thousand, jittered, each a real model --------------------------------------------------
	# name, weight, scale range, y offset
	kinds = (("treasure_chest", 3, (0.9, 1.3)), ("Chandelier_01", 1, (0.9, 1.4)), ("Lantern_01", 3, (0.9, 1.5)), ("vintage_cabinet_01", 2, (0.9, 1.2)),
		("Barrel_01", 5, (0.9, 1.3)), ("wooden_crate_01", 5, (0.9, 1.4)))
	weighted = [k for k in kinds for _ in range(k[1])]
	spacing = 5.0
	cells = int(PLAZA_SIZE / spacing)
	prop_target = int(1500 * SCALE)
	cell_list = [(i, j) for i in range(cells) for j in range(cells)]
	rng.shuffle(cell_list)
	s.add("Props", "World", **make_static)
	count = 0
	for i, j in cell_list:
		if count >= prop_target:
			break
		x = (i - cells / 2 + 0.5) * spacing + rng.uniform(-1.4, 1.4)
		z = PLAZA_Z0 + (j + 0.5) * spacing + rng.uniform(-1.4, 1.4)
		if abs(x) < 3.5 and z < PLAZA_Z0 + 40:
			continue  # keep the gate of the atrium open
		name, _, scale_range = rng.choice(weighted)
		sc = rng.uniform(*scale_range)
		s.add("Prop %04d" % count, "Props", pos=(x, 0, z), rot=demo.qaxis((0, 1, 0), rng.uniform(0, 360)), scale=(sc, sc, sc), **prefab(name))
		count += 1

	# --- ships: the heavy meshes -------------------------------------------------------------------------------------
	s.add("Ships", "World", **make_static)
	ship_count = max(1, int(10 * SCALE))
	for k in range(ship_count):
		side = -1 if k % 2 == 0 else 1
		row = k // 2
		x = side * rng.uniform(25, 80)
		z = PLAZA_Z0 + 25 + row * (PLAZA_SIZE - 50) / max(1, ship_count // 2)
		s.add("Ship %02d" % k, "Ships", pos=(x, 0.2, z), rot=demo.qaxis((0, 1, 0), rng.uniform(0, 360)), **prefab("ship_pinnace"))

	# --- the crowd: skinned, animated characters ------------------------------------------------------------------------
	s.add("Crowd", "World")
	crowd_target = int(160 * SCALE)
	names = list(characters)
	for k in range(crowd_target):
		name = names[k % len(names)]
		model, clip = characters[name]
		if k < crowd_target // 4:
			x, z = rng.uniform(-12, 11), rng.uniform(-3.5, 3.5)  # in the atrium
			if abs(abs(z) - 4) < 0.8:
				z += 1.5
		else:
			x, z = rng.uniform(-90, 90), rng.uniform(PLAZA_Z0 + 4, PLAZA_Z0 + PLAZA_SIZE - 4)
		s.add("Crowd %03d" % k, "Crowd", pos=(x, 0, z), rot=demo.qaxis((0, 1, 0), rng.uniform(0, 360)), scale=(1.0, 1.0, 1.0), PrefabInstance={"Prefab": ref(model)},
			**{"script:" + clip_player: {"clip": clip, "speed": rng.uniform(0.7, 1.3)}})

	# --- lights: points (clustered culling) and shadow casting spots --------------------------------------------------------
	s.add("Lights", "World")
	light_target = int(320 * SCALE)
	palette = ((1, 0.6, 0.3), (0.4, 0.7, 1), (1, 0.3, 0.5), (0.5, 1, 0.5), (1, 0.9, 0.5))
	for k in range(light_target):
		if k < light_target // 5:
			x, z = rng.uniform(-14, 13), rng.uniform(-8, 8)
		else:
			x, z = rng.uniform(-95, 95), rng.uniform(PLAZA_Z0, PLAZA_Z0 + PLAZA_SIZE)
		r, g, b = rng.choice(palette)
		s.add("Point light %03d" % k, "Lights", pos=(x, rng.uniform(0.8, 3.2), z), **light("Point", (r, g, b, 1), rng.uniform(4, 14), rng_=rng.uniform(5, 9)))
	spot_target = max(1, int(6 * SCALE))
	for k in range(spot_target):
		x = -10 + 20 * k / max(1, spot_target - 1)
		s.add("Spot light %02d" % k, "Lights", pos=(x, 9.5, rng.uniform(-3, 3)), rot=demo.qaxis((1, 0, 0), -90),
			**light("Spot", (1, 0.9, 0.7, 1), 40, rng_=14, inner=25, outer=45, shadows=True))

	# --- fire: particle systems --------------------------------------------------------------------------------------------
	s.add("Fires", "World")
	fire_target = int(30 * SCALE)
	for k in range(fire_target):
		x, z = rng.uniform(-80, 80), rng.uniform(PLAZA_Z0 + 8, PLAZA_Z0 + PLAZA_SIZE - 8)
		s.add("Fire %02d" % k, "Fires", pos=(x, 0.25, z), ParticleSystem={"Effect": ref(fire)})
	for k in range(max(1, int(8 * SCALE))):
		s.add("Fountain %02d" % k, "Fires", pos=(-9 + 18 * k / max(1, int(8 * SCALE) - 1), 0.3, rng.uniform(-2, 2)), ParticleSystem={"Effect": ref(fountain)})

	# --- physics: a wall of crates in front of the gate -----------------------------------------------------------------------
	s.add("Crate wall", "World")
	body = {"RigidBody": {"Motion": "Dynamic"}}
	wall_w, wall_h, wall_d = int(14 * SCALE ** 0.5), int(10 * SCALE ** 0.5), 3
	index = 0
	for ix in range(wall_w):
		for iy in range(wall_h):
			for iz in range(wall_d):
				s.add("Wall crate %03d" % index, "Crate wall", pos=(14 + ix * 1.02, 0.5 + iy * 1.02, PLAZA_Z0 + 6 + iz * 1.02), scale=(1, 1, 1),
					MeshRenderer={"Mesh": ref(shapes, demo.MESH["cube"]), "Materials": [ref(crate_material)]}, Collider=demo.BOX, **body)
				index += 1

	settings = {"source": "file", "environment": ref(environment), "background": "sky", "ambient": 0.9, "rotation": 120,
		"exposure": {"auto": True, "compensation": -0.3, "minEv": -3, "maxEv": 3, "adaptSpeed": 2},
		"bloom": {"enabled": True, "threshold": 1, "knee": 0.5, "intensity": 0.07},
		"tonemap": {"operator": "aces", "contrast": 1.06, "saturation": 1.12, "temperature": 0.05, "vignette": 0.25},
		"ao": {"enabled": True, "radius": 1.2, "power": 1.6},
		"motionBlur": {"enabled": True}}
	demo.write_json("scenes/stress.wscene", {"name": "Stress hall", "settings": settings, "entities": s.entities})
	guid = demo.write_meta("scenes/stress.wscene", "SceneImporter", 5)
	print("wrote scenes/stress.wscene (guid %s): %d entities: %d props, %d ships, %d characters, %d point lights, %d spot lights, %d fires, %d crates"
		% (guid, len(s.entities), count, ship_count, crowd_target, light_target, spot_target, fire_target, index))


if __name__ == "__main__":
	main()
