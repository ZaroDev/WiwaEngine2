#!/usr/bin/env python3
"""Writes the demo hall: editor/assets/** (scene, scripts, materials, UI, input, models, textures) and their .meta files.

    python scripts/gen_demo_scene.py [cache_dir]

Needs Pillow (to pack the ORM textures). Downloads the free assets it uses into cache_dir (default: .demo_cache, git-ignored) once.
Every asset gets a GUID derived from its path, so running this again changes nothing the scene points at. The .meta files are
written here (they are git-ignored, so `git add -f` them) because the scene refers to the GUIDs and the mesh local ids.

Skeletons, clips and skinned meshes of the models are extracted by the editor into <Model>_animation/ (.wskel, .wanim, .wmesh) the first time it imports a model;
the animationEvents setting written to the model .meta below only seeds the clips of that first extraction (the files own the events afterwards), and the
scene's references to the old sub-asset ids keep working through the catalog's redirects.

The demo smoke test (editor/src/play/demo_smoke.cpp) depends on entity names, positions of the joints and the character lane, the
HUD widget names and colours and the behaviour of the scripts below. Change them together.
"""
import hashlib
import io
import json
import math
import os
import pathlib
import re
import struct
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "editor" / "assets"
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
CACHE = pathlib.Path(ARGS[0]) if ARGS else ROOT / ".demo_cache"

SCENE_GUID = "2a6ca957fed3526d8d53224e96ff22f1"  # editor/build_settings.json names it


def guid(key):
	return hashlib.md5(key.encode()).hexdigest()


def ref(g, local=0):
	return {"guid": g, "localId": local}


_FNV = 0xCBF29CE484222325
_FNV_PRIME = 0x100000001B3
_U64 = 0xFFFFFFFFFFFFFFFF


def node_id(path):
	"""The stable id of a prefab node (engine/src/scene/prefab_node_id.cpp compute_node_id): FNV-1a 64 of the path of names ("Street lamp/Head"). Only for nodes
	with unique names among their siblings (the engine adds "#n" to a repeated name and bumps a collision)."""
	h = _FNV
	for b in path.encode():
		h = ((h ^ b) * _FNV_PRIME) & _U64
	return h or 1


def compose_node_id(outer, inner):
	"""The id of a node of a nested prefab instance (compose_node_id in prefab_node_id.cpp): `outer` is the id of the instance's node, `inner` the id in the inner prefab."""
	h = _FNV
	for part in (outer, inner):
		for shift in range(0, 64, 8):
			h = ((h ^ ((part >> shift) & 0xFF)) * _FNV_PRIME) & _U64
		h = ((h ^ 0x9E) * _FNV_PRIME) & _U64
	if h == 0:
		return 1
	return _U64 - 1 if h == _U64 else h


def set_field_override(node, component, field, value):
	return {"kind": "SetField", "node": "%016x" % node, "component": component, "field": field, "value": value}


# ---------------------------------------------------------------------------------------------------------------- files

def write(rel, text, binary=False):
	path = ASSETS / rel
	path.parent.mkdir(parents=True, exist_ok=True)
	if binary:
		path.write_bytes(text)
	else:
		path.write_text(text, encoding="utf-8", newline="\n")


def write_json(rel, data):
	write(rel, json.dumps(data, indent=2) + "\n")


def write_meta(rel, importer, version, settings=None, sub_assets=None, fixed_guid=None):
	g = fixed_guid or guid("asset:" + rel)
	write_json(rel + ".meta", {"guid": g, "importer": importer, "version": version,
		"importSettings": settings or {}, "subAssets": sub_assets or {}})
	return g


def download(url, name, agent="wiwa2-demo-generator"):
	CACHE.mkdir(parents=True, exist_ok=True)
	path = CACHE / name
	if not path.exists():
		print("downloading", url)
		request = urllib.request.Request(url, headers={"User-Agent": agent})
		with urllib.request.urlopen(request, timeout=180) as response:
			path.write_bytes(response.read())
	return path


# ---------------------------------------------------------------------------------------------------- primitive meshes

def triangulate(positions, normals, uvs, quads):
	"""quads: lists of 4 vertex indices (a quad grid cell); winding is fixed against the vertex normals."""
	indices = []
	for a, b, c, d in quads:
		for tri in ((a, b, c), (a, c, d)):
			p0, p1, p2 = (positions[i] for i in tri)
			e1 = [p1[k] - p0[k] for k in range(3)]
			e2 = [p2[k] - p0[k] for k in range(3)]
			n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
			avg = [sum(normals[i][k] for i in tri) for k in range(3)]
			if sum(n[k] * avg[k] for k in range(3)) < 0:
				tri = (tri[0], tri[2], tri[1])
			if sum(x * x for x in n) > 1e-12:
				indices.extend(tri)
	return indices


def grid(positions, normals, uvs, quads, rows, cols, fn):
	"""Adds a (rows+1) x (cols+1) grid of vertices from fn(u, v) -> (position, normal)."""
	base = len(positions)
	for i in range(rows + 1):
		for j in range(cols + 1):
			u, v = j / cols, i / rows
			p, n = fn(u, v)
			positions.append(p)
			normals.append(n)
			uvs.append((u, v))
	for i in range(rows):
		for j in range(cols):
			a = base + i * (cols + 1) + j
			quads.append((a, a + 1, a + cols + 2, a + cols + 1))


def mesh_cube():
	pos, nrm, uv, quads = [], [], [], []
	faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
		((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
		((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
	for n, right, up in faces:
		def fn(u, v, n=n, right=right, up=up):
			return (tuple(0.5 * n[k] + (u - 0.5) * right[k] + (v - 0.5) * up[k] for k in range(3)), n)
		grid(pos, nrm, uv, quads, 1, 1, fn)
	return pos, nrm, uv, triangulate(pos, nrm, uv, quads)


def mesh_sphere(rows=16, cols=32):
	pos, nrm, uv, quads = [], [], [], []

	def fn(u, v):
		theta, phi = u * 2 * math.pi, v * math.pi
		n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
		return (tuple(0.5 * x for x in n), n)
	grid(pos, nrm, uv, quads, rows, cols, fn)
	return pos, nrm, uv, triangulate(pos, nrm, uv, quads)


def mesh_cylinder(cols=32, cone=False):
	pos, nrm, uv, quads = [], [], [], []

	def side(u, v):
		a = u * 2 * math.pi
		radius = 0.5 * ((1 - v) if cone else 1.0)
		slope = 0.5 if cone else 0.0
		length = math.sqrt(1 + slope * slope)
		n = (math.cos(a) / length, slope / length if cone else 0.0, math.sin(a) / length)
		return ((radius * math.cos(a), v - 0.5, radius * math.sin(a)), n)
	grid(pos, nrm, uv, quads, 1, cols, side)
	for sign in ((-1,) if cone else (-1, 1)):
		def cap(u, v, sign=sign):
			a = u * 2 * math.pi
			return ((0.5 * v * math.cos(a), sign * 0.5, 0.5 * v * math.sin(a)), (0, sign, 0))
		grid(pos, nrm, uv, quads, 1, cols, cap)
	return pos, nrm, uv, triangulate(pos, nrm, uv, quads)


def mesh_torus(rows=16, cols=32, major=0.5, minor=0.15):
	pos, nrm, uv, quads = [], [], [], []

	def fn(u, v):
		a, b = u * 2 * math.pi, v * 2 * math.pi
		n = (math.cos(b) * math.cos(a), math.sin(b), math.cos(b) * math.sin(a))
		return (((major + minor * math.cos(b)) * math.cos(a), minor * math.sin(b), (major + minor * math.cos(b)) * math.sin(a)), n)
	grid(pos, nrm, uv, quads, rows, cols, fn)
	return pos, nrm, uv, triangulate(pos, nrm, uv, quads)


def mesh_plane(tiles=1.0):
	pos, nrm, uv, quads = [], [], [], []

	def fn(u, v):
		return ((u - 0.5, 0.0, v - 0.5), (0, 1, 0))
	grid(pos, nrm, uv, quads, 1, 1, fn)
	uv[:] = [(u * tiles, v * tiles) for u, v in uv]
	return pos, nrm, uv, triangulate(pos, nrm, uv, quads)


# the local ids in shapes.glb.meta: mesh/N -> N + 1 (they are what MeshRenderer.Mesh points at)
SHAPES = [("cube", mesh_cube()), ("sphere", mesh_sphere()), ("cylinder", mesh_cylinder()), ("cone", mesh_cylinder(cone=True)),
	("torus", mesh_torus()), ("plane", mesh_plane()), ("plane_tiled", mesh_plane(24.0))]
MESH = {name: i + 1 for i, (name, _) in enumerate(SHAPES)}


def build_glb():
	blob = bytearray()
	views, accessors, meshes, nodes = [], [], [], []

	def add(data, target, component, count, kind, minmax=None):
		while len(blob) % 4:
			blob.append(0)
		views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
		blob.extend(data)
		accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
		if minmax:
			accessor["min"], accessor["max"] = minmax
		accessors.append(accessor)
		return len(accessors) - 1

	for name, (pos, nrm, uv, idx) in SHAPES:
		lo = [min(p[k] for p in pos) for k in range(3)]
		hi = [max(p[k] for p in pos) for k in range(3)]
		a_pos = add(struct.pack("<%df" % (3 * len(pos)), *[c for p in pos for c in p]), 34962, 5126, len(pos), "VEC3", (lo, hi))
		a_nrm = add(struct.pack("<%df" % (3 * len(nrm)), *[c for n in nrm for c in n]), 34962, 5126, len(nrm), "VEC3")
		a_uv = add(struct.pack("<%df" % (2 * len(uv)), *[c for t in uv for c in t]), 34962, 5126, len(uv), "VEC2")
		a_idx = add(struct.pack("<%dH" % len(idx), *idx), 34963, 5123, len(idx), "SCALAR")
		meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv}, "indices": a_idx, "mode": 4}]})
		nodes.append({"name": name, "mesh": len(meshes) - 1})
	while len(blob) % 4:
		blob.append(0)
	doc = {"asset": {"version": "2.0", "generator": "gen_demo_scene.py"}, "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
		"nodes": nodes, "meshes": meshes, "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
	text = json.dumps(doc, separators=(",", ":")).encode()
	text += b" " * (-len(text) % 4)
	total = 12 + 8 + len(text) + 8 + len(blob)
	return b"glTF" + struct.pack("<II", 2, total) + struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob)


# ------------------------------------------------------------------------------------------------------------- downloads

CHARS_URL = "https://opengameart.org/sites/default/files/rpg_characters_-_nov_2020.zip"
MONSTERS_URL = "https://opengameart.org/sites/default/files/cute_animated_monsters_-_aug_2020.zip"
HDRI_URL = "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloppenheim_06_puresky_1k.hdr"
TEXTURES = {"bricks": "Bricks059", "paving": "PavingStones070", "metal": "Metal032"}
# Sounds, all CC0: events are the file names (docs/superpowers/specs/audio.md, MiniAudio backend). Kenney's packs through their Godot mirrors, pinned to a commit.
KENNEY_UI = "https://raw.githubusercontent.com/Calinou/kenney-interface-sounds/4596a49eaf5a533948d49a47467f606bcdea70ff/addons/kenney_interface_sounds/"
KENNEY_IMPACT = "https://raw.githubusercontent.com/Boyquotes/kenney-impact-sounds-for-godot/999dd1684873f8b020a3aa5b26e713da21688924/addons/kenney%20impact%20sounds/"
OGA_WIND = "https://opengameart.org/sites/default/files/Birds%20and%20Wind%20-%20Ambient_0.ogg"
AUDIO = (  # (event / file name in the project, source url, cache name)
	("Amb_Hall_loop.ogg", OGA_WIND, "oga_birds_and_wind_ambient.ogg"),  # the "_loop" ending makes the MiniAudio backend loop it
	("Beacon_Blip.wav", KENNEY_UI + "bong_001.wav", "kenney_bong_001.wav"),
	("UI_Click.wav", KENNEY_UI + "click_001.wav", "kenney_click_001.wav"),
	("Footstep_1.ogg", KENNEY_IMPACT + "footstep_concrete_000.ogg", "kenney_footstep_concrete_000.ogg"),
	("Footstep_2.ogg", KENNEY_IMPACT + "footstep_concrete_001.ogg", "kenney_footstep_concrete_001.ogg"),
	("Footstep_3.ogg", KENNEY_IMPACT + "footstep_concrete_002.ogg", "kenney_footstep_concrete_002.ogg"),
	("Impact_Soft.ogg", KENNEY_IMPACT + "impact_soft_medium_000.ogg", "kenney_impact_soft_medium_000.ogg"),
)
AUDIO_LICENSE = """The sounds in this folder are all CC0 1.0 (public domain):

- Beacon_Blip.wav (bong_001), UI_Click.wav (click_001): Kenney, "Interface Sounds", https://kenney.nl/assets/interface-sounds
- Footstep_1..3.ogg (footstep_concrete_000..002), Impact_Soft.ogg (impact_soft_medium_000): Kenney, "Impact Sounds", https://kenney.nl/assets/impact-sounds
- Amb_Hall_loop.ogg ("Birds and Wind - Ambient.ogg"): Spring Spring, "Birds and Wind - Ambient, Birds, Wind and Synth", https://opengameart.org/content/birds-and-wind-ambient-birds-wind-and-synth
  (bird sounds in it by isaiah658, syncopika and pauliuw, public domain)

They are copied under the names the demo scripts post as events. scripts/gen_demo_scene.py downloads them.
"""


def import_audio():
	for name, url, cache in AUDIO:
		write("audio/clips/" + name, download(url, cache, "Mozilla/5.0").read_bytes(), binary=True)
		write_meta("audio/clips/" + name, "AudioClipImporter", 1)
	write("audio/clips/LICENSE.txt", AUDIO_LICENSE)


FONT_URL = "https://github.com/google/fonts/raw/main/ofl/lato/Lato-Regular.ttf"
FONT_LICENSE_URL = "https://github.com/google/fonts/raw/main/ofl/lato/OFL.txt"

CHARACTERS = {  # model -> (preferred clips, the pose of its station slot)
	"Warrior": ["Idle"], "Wizard": ["Spell1", "Idle"], "Ranger": ["Walk", "Idle"], "Cleric": ["Idle_Weapon", "Idle"]}
MONSTERS = {"Yeti": ["Dance", "Idle"], "Panda": ["Dance", "Idle"], "YellowDragon": ["Dance", "Idle"]}


def fbx_clips(data):
	return sorted(set(m.group(1).decode("latin1") for m in re.finditer(rb"([\w|. -]{2,60})\x00\x01AnimStack", data)))


def pick_clip(clips, wanted):
	for name in wanted:
		for clip in clips:
			if clip.endswith("|" + name):
				return clip
	return clips[0]


def write_texture_meta(rel, kind):
	write_meta(rel, "TextureImporter", 1, {"kind": kind})  # written up front: a scan that reaches the file first would make it "color"


def write_model_materials(z, group, folder, model, data):
	"""The FBX files name materials but link no texture, so the extracted .wmat files are written here, with the model's PNG as base color.
	The importer never overwrites an existing material file, and models/<folder>/<Model>.fbx.meta (materialFiles) says which file is whose."""
	files = {}
	names = sorted(set(m.group(1).decode("latin1") for m in re.finditer(rb"([\w. -]{1,40})\x00\x01Material", data)))
	available = set(e.rsplit("/", 1)[1] for e in z.namelist() if e.startswith(group + "/Textures/"))
	for material in names:
		png = next((c for c in (material + ".png", "%s_%s.png" % (model, material)) if c in available), None)
		stem = re.sub(r"[^\w.-]", "_", material)
		textures = {"baseColor": "../textures/" + png} if png else {}
		wmat_rel = "models/%s/%s_materials/%s.wmat" % (folder, model, stem)
		write_json(wmat_rel, {"version": 1, "name": material, "alpha": "opaque", "alphaCutoff": 0.5, "baseColor": [1, 1, 1, 1], "clearcoat": 0.0,
			"clearcoatRoughness": 0.0, "doubleSided": False, "emissive": [0, 0, 0], "metallic": 0.0, "roughness": 0.8, "sheen": 0.0,
			"sheenRoughness": 0.0, "transmission": 0.0, "textures": textures})
		write_meta(wmat_rel, "MaterialImporter", 2)
		files[material] = stem
	return files


def import_models():
	"""Copies the FBX models; returns {name: (guid, clips)}."""
	models = {}
	for archive, url, group, names, folder in (("chars.zip", CHARS_URL, "RPG Characters - Nov 2020", CHARACTERS, "characters"),
			("monsters.zip", MONSTERS_URL, "Cute Animated Monsters - Aug 2020", MONSTERS, "monsters")):
		with zipfile.ZipFile(download(url, archive)) as z:
			for name in names:
				data = z.read("%s/FBX/%s.fbx" % (group, name))
				rel = "models/%s/%s.fbx" % (folder, name)
				write(rel, data, binary=True)
				clips = fbx_clips(data)
				material_files = write_model_materials(z, group, folder, name, data)
				events = []
				for clip in clips:
					if clip.endswith("|Run"):
						events = [{"clip": clip, "name": "footstep", "time": 0.2}, {"clip": clip, "name": "footstep", "time": 0.62}]
				g = write_meta(rel, "ModelImporter", 7, {"generatePhysicsShapes": False, "animationEvents": events, "materialFiles": material_files})
				models[name] = (g, clips)
			# the FBX files name their textures without a usable path: the importer finds them next to the model or in textures/
			for entry in z.namelist():
				if entry.startswith(group + "/Textures/") and entry.endswith(".png"):
					write("models/%s/textures/%s" % (folder, entry.rsplit("/", 1)[1]), z.read(entry), binary=True)
					write_texture_meta("models/%s/textures/%s" % (folder, entry.rsplit("/", 1)[1]), "color")
			write("models/%s/LICENSE.txt" % folder, "%s by Quaternius, CC0 1.0 Universal (https://creativecommons.org/publicdomain/zero/1.0/).\nSource: %s\n" % (group, url))
	return models


def import_textures():
	from PIL import Image
	out = {}
	for key, asset in TEXTURES.items():
		with zipfile.ZipFile(download("https://ambientcg.com/get?file=%s_1K-JPG.zip" % asset, asset + ".zip")) as z:
			def load(suffix):
				name = "%s_1K-JPG_%s.jpg" % (asset, suffix)
				return Image.open(io.BytesIO(z.read(name))) if name in z.namelist() else None
			color, normal, rough = load("Color"), load("NormalGL"), load("Roughness")
			ao, metal = load("AmbientOcclusion"), load("Metalness")
			size = color.size
			white = Image.new("L", size, 255)
			black = Image.new("L", size, 0)
			orm = Image.merge("RGB", ((ao.convert("L") if ao else white), rough.convert("L").resize(size), (metal.convert("L").resize(size) if metal else black)))
			buf = io.BytesIO()
			orm.save(buf, "PNG")
			write("textures/%s_color.jpg" % key, z.read("%s_1K-JPG_Color.jpg" % asset), binary=True)
			write("textures/%s_normal.jpg" % key, z.read("%s_1K-JPG_NormalGL.jpg" % asset), binary=True)
			write("textures/%s_orm.png" % key, buf.getvalue(), binary=True)
			write_texture_meta("textures/%s_color.jpg" % key, "color")
			write_texture_meta("textures/%s_normal.jpg" % key, "normal")
			write_texture_meta("textures/%s_orm.png" % key, "metallicRoughness")
		out[key] = key
	write("textures/LICENSE.txt", "Bricks059, PavingStones070, Metal032 from ambientCG.com, CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/).\n"
		"The *_orm.png files pack the AmbientOcclusion, Roughness and Metalness maps (R, G, B).\n")
	return out


# ---------------------------------------------------------------------------------------------------------------- materials

def wmat(name, base=(1, 1, 1, 1), metallic=0.0, roughness=0.5, emissive=(0, 0, 0), alpha="opaque", double=False, clearcoat=0.0,
		sheen=0.0, transmission=0.0, textures=None, shader=None, params=None):
	data = {"version": 1, "name": name, "alpha": alpha, "alphaCutoff": 0.5, "baseColor": list(base), "clearcoat": clearcoat,
		"clearcoatRoughness": 0.0, "doubleSided": double, "emissive": list(emissive), "metallic": metallic, "roughness": roughness,
		"sheen": sheen, "sheenRoughness": 0.0, "transmission": transmission, "textures": textures or {}}
	if shader:
		data["shader"] = shader
		data["params"] = params or {}
	rel = "materials/%s.wmat" % name
	write_json(rel, data)
	return write_meta(rel, "MaterialImporter", 2)


def textured(key):
	return {"baseColor": "../textures/%s_color.jpg" % key, "normal": "../textures/%s_normal.jpg" % key, "orm": "../textures/%s_orm.png" % key}


def build_materials():
	m = {}
	m["ground"] = wmat("ground", textures=textured("paving"), roughness=1.0)
	m["podium"] = wmat("podium", textures=textured("metal"), roughness=1.0, metallic=1.0)
	m["column"] = wmat("column", textures=textured("bricks"), roughness=1.0)
	m["backdrop"] = wmat("backdrop", textures=textured("bricks"), roughness=1.0, base=(0.8, 0.85, 1, 1))
	m["crate"] = wmat("crate", base=(0.55, 0.38, 0.2, 1), roughness=0.7)
	m["gold"] = wmat("gold", base=(1.0, 0.77, 0.25, 1), metallic=1.0, roughness=0.22)
	m["steel"] = wmat("steel", base=(0.8, 0.82, 0.85, 1), metallic=1.0, roughness=0.3)
	m["plastic_red"] = wmat("plastic_red", base=(0.8, 0.05, 0.05, 1), roughness=0.35, clearcoat=0.5)
	m["ring_blue"] = wmat("ring_blue", base=(0.1, 0.3, 0.9, 1), metallic=0.8, roughness=0.25, emissive=(0.02, 0.08, 0.3))
	m["carpaint"] = wmat("carpaint", base=(0.7, 0.02, 0.02, 1), metallic=0.6, roughness=0.25, clearcoat=1.0)
	m["cloth"] = wmat("cloth", base=(0.5, 0.08, 0.15, 1), roughness=0.9, sheen=1.0)
	m["glass"] = wmat("glass", base=(0.7, 0.9, 1.0, 0.25), roughness=0.05, alpha="blend", transmission=0.8)
	m["double_sided"] = wmat("double_sided", base=(0.2, 0.7, 0.2, 1), roughness=0.6, double=True)
	m["plain"] = wmat("plain", base=(0.85, 0.85, 0.85, 1), roughness=0.6)
	m["marker"] = wmat("marker", base=(0.1, 0.1, 0.12, 1), roughness=0.4, emissive=(0.05, 0.3, 0.6))
	for name, color in (("neon_cyan", (0.1, 0.9, 1.0)), ("neon_magenta", (1.0, 0.1, 0.8)), ("neon_orange", (1.0, 0.5, 0.05))):
		m[name] = wmat(name, base=(0, 0, 0, 1), emissive=tuple(c * 6 for c in color))
	for r in range(5):
		for mt in range(3):
			metallic = (0.0, 0.5, 1.0)[mt]
			m["pbr_r%d_m%d" % (r, mt)] = wmat("pbr_r%d_m%d" % (r, mt), base=(0.9, 0.55, 0.2, 1), metallic=metallic, roughness=max(0.04, r / 4.0))

	write("shaders/toon.wshader", TOON_SHADER)
	toon = write_meta("shaders/toon.wshader", "SurfaceShaderImporter", 1)
	write("shaders/hologram.wshader", HOLOGRAM_SHADER)
	write_meta("shaders/hologram.wshader", "SurfaceShaderImporter", 1)
	for name, tint in (("toon_red", (0.9, 0.15, 0.12, 1)), ("toon_teal", (0.1, 0.7, 0.7, 1)), ("toon_yellow", (0.95, 0.8, 0.1, 1)), ("toon_puffy", (0.9, 0.4, 0.8, 1))):
		m[name] = wmat(name, shader="../shaders/toon.wshader", params={"Tint": list(tint), "Bands": 3, "Saturation": 1.25, "ShadeStrength": 0.45,
			"RimColor": [1, 0.8, 0.6, 1], "RimPower": 3.0, "RimStrength": 0.8, "Inflate": 0.06 if name == "toon_puffy" else 0.0})
	m["hologram"] = wmat("hologram", shader="../shaders/hologram.wshader", params={"Color": [0.2, 0.9, 1.0, 1], "Wobble": 0.04, "Scan": 14.0})
	return m


TOON_SHADER = """// Toon-styled PBR: a demo of the surface shader contract (engine/surface_contract.hlsli).
// The engine keeps the lighting; the cel look comes from what the shader feeds it: a posterized albedo, bands by view angle and a hard rim.
struct MaterialParams
{
	float4 Tint;          // color default(1,1,1,1)
	int    Bands;         // range(2,8) default(3) label("Color bands")
	float  Saturation;    // range(0,2) default(1.25)
	float  ShadeStrength; // range(0,1) default(0.45) label("Shade strength")
	float4 RimColor;      // color default(1,1,1,1) label("Rim color")
	float  RimPower;      // range(0.5,8) default(3) label("Rim sharpness")
	float  RimStrength;   // range(0,4) default(0.8) label("Rim strength")
	float  Inflate;       // range(0,0.1) default(0) label("Inflate (puffy)")
};

void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)
{
	float bands = (float)p.Bands;
	float3 albedo = o.Albedo * p.Tint.rgb;
	float luma = max(dot(albedo, float3(0.299, 0.587, 0.114)), 1e-4);
	float3 chroma = lerp(float3(1, 1, 1), albedo / luma, p.Saturation);
	float quantized = floor(saturate(luma) * bands + 0.5) / bands;

	float facing = saturate(dot(i.WorldNormal, i.ViewDirection));
	float band = floor(facing * bands) / max(bands - 1.0, 1.0);
	float shade = lerp(1.0 - p.ShadeStrength, 1.0, saturate(band));

	o.Albedo = saturate(chroma * max(quantized, 0.02)) * shade;
	o.Metallic = 0.0;
	o.Roughness = 0.9;
	o.Occlusion = 1.0;

	float rim = step(0.5, pow(1.0 - facing, p.RimPower)) * p.RimStrength;
	o.Emissive += p.RimColor.rgb * rim;
}

float3 VertexOffset(VertexInput v, MaterialParams p)
{
	return normalize(v.Normal) * p.Inflate;
}
"""

HOLOGRAM_SHADER = """// Uses the time in SurfaceInput: moving scan lines in the emissive and a wobbling vertex offset.
struct MaterialParams
{
	float4 Color;   // color default(0.2,0.9,1,1)
	float  Wobble;  // range(0,0.2) default(0.04)
	float  Scan;    // range(1,40) default(14) label("Scan line density")
};

void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)
{
	float facing = saturate(dot(i.WorldNormal, i.ViewDirection));
	float lines = 0.5 + 0.5 * sin(i.WorldPosition.y * p.Scan - i.Time * 4.0);
	o.Albedo = p.Color.rgb * 0.15;
	o.Metallic = 0.0;
	o.Roughness = 0.3;
	o.Occlusion = 1.0;
	o.Emissive = p.Color.rgb * (0.4 + 2.5 * lines) * (0.3 + 0.7 * pow(1.0 - facing, 1.5));
}

float3 VertexOffset(VertexInput v, MaterialParams p)
{
	return normalize(v.Normal) * p.Wobble * sin(v.Position.y * 9.0 + v.InstanceIndex);
}
"""


# ------------------------------------------------------------------------------------------------------------------ scripts

SCRIPTS = {}

SCRIPTS["lib/wave.lua"] = """-- A shared helper: scripts load it with require. Edit it while the game plays and every script that uses it changes.
local wave = {}

function wave.sine(t)
  return math.sin(t)
end

return wave
"""

SCRIPTS["spinner.lua"] = """-- Turns the entity around an axis. Change degrees_per_second in the Inspector while playing.
local Spinner = Script:extend{
  properties = {
    degrees_per_second = { type = "float", default = 60, min = -720, max = 720 },
    axis = { type = "vec3", default = { 0, 1, 0 } },
  },
}

function Spinner:on_init()
  self.base = self.transform.rotation
  self.angle = 0
end

function Spinner:on_update(dt)
  self.angle = self.angle + self.degrees_per_second * dt
  self.transform.rotation = self.base * quat.from_axis_angle(self.axis:normalize(), self.angle)
end

return Spinner
"""

SCRIPTS["bobber.lua"] = """-- Bobs the entity up and down with the shared wave helper.
local wave = require("assets/scripts/lib/wave.lua")

local Bobber = Script:extend{
  properties = {
    height = { type = "float", default = 0.3, min = 0, max = 5 },
    speed = { type = "float", default = 2.5, min = 0, max = 20 },
    phase = { type = "float", default = 0, min = 0, max = 6.3 },
  },
}

function Bobber:on_init()
  self.base_y = self.transform.position.y
  self.t = 0
end

function Bobber:on_update(dt)
  self.t = self.t + dt
  self.transform.position.y = self.base_y + self.height * wave.sine(self.t * self.speed + self.phase)
end

return Bobber
"""

SCRIPTS["light_pulse.lua"] = """-- Reads and writes the Light component of its own entity.
local wave = require("assets/scripts/lib/wave.lua")

local LightPulse = Script:extend{
  properties = {
    amplitude = { type = "float", default = 2, min = 0, max = 20 },
    speed = { type = "float", default = 6, min = 0, max = 30 },
  },
}

function LightPulse:on_init()
  self.base = self.entity:get("Light").intensity
  self.t = 0
end

function LightPulse:on_update(dt)
  self.t = self.t + dt
  self.entity:get("Light").intensity = self.base + self.amplitude * wave.sine(self.t * self.speed)
end

return LightPulse
"""

SCRIPTS["camera_rig.lua"] = """-- The demo camera: drifts sideways, and keys 1 to 7 jump to a station of the hall.
local wave = require("assets/scripts/lib/wave.lua")

-- name, position, pitch and yaw in degrees (plain tables: vec3 is only made while the game runs)
local STATIONS = {
  { "Lighting and post", { 0, 4.5, 17 }, -4, 0 },
  { "Materials and shaders", { 0, 5, -4 }, -6, 0 },
  { "Models and animation", { 0, 2.6, 3.5 }, -5, 180 },
  { "Physics", { 11, 5, 11 }, -22, 0 },
  { "Joints", { -10, 5, 19 }, -16, 0 },
  { "Character controller", { 24, 6, 14 }, -22, 0 },
  { "Scripts, input and UI", { 2, 4, 14 }, -14, 0 },
}

local CameraRig = Script:extend{
  properties = {
    distance = { type = "float", default = 1.5, min = 0, max = 20 },
    speed = { type = "float", default = 0.4, min = 0, max = 5 },
  },
}

function CameraRig:on_init()
  self.base_x = self.transform.position.x
  self.t = 0
end

function CameraRig:go(index)
  local station = STATIONS[index]
  local p = station[2]
  self.base_x = p[1]
  self.transform.position.x = p[1]
  self.transform.position.y = p[2]
  self.transform.position.z = p[3]
  self.transform.rotation = quat.from_euler(station[3], station[4], 0)
  log("Camera", "Station " .. index .. ": " .. station[1])
end

function CameraRig:on_update(dt)
  for index = 1, #STATIONS do
    if input.pressed("Key." .. index) then
      self:go(index)
    end
  end
  self.t = self.t + dt
  self.transform.position.x = self.base_x + self.distance * wave.sine(self.t * self.speed)
end

return CameraRig
"""

SCRIPTS["audio_demo.lua"] = """-- Audio (see "Audio" in docs/scripting.md): the entity has an AudioEmitter, so it has a voice: it hums the ambience (PlayOnStart, heard across the hall) and
-- this script blips from the beacon every few seconds. The sounds are assets/audio/*.ogg and .wav (CC0, see its LICENSE.txt); the project plays them
-- with the MiniAudio backend (Settings, Audio). Another backend, or none (Null), takes the same calls.
local AudioDemo = Script:extend{
  properties = {
    event = { type = "string", default = "Beacon_Blip" },
    every = { type = "float", default = 4, min = 0.5, max = 60 },
  },
}

function AudioDemo:on_init()
  self.t = 0
  Audio.set_rtpc("Volume", 0.8)
  self.last = Audio.post(self.event, self.entity)
end

function AudioDemo:on_update(dt)
  self.t = self.t + dt
  if self.t >= self.every then
    self.t = 0
    self.last = Audio.post(self.event, self.entity)
  end
end

return AudioDemo
"""

SCRIPTS["scene_overlay.lua"] = """-- A scene-level script (listed in the scene settings, no entity): logs and draws debug shapes around the stations.
local SceneOverlay = Script:extend{
  properties = {},
}

function SceneOverlay:on_init()
  log("Demo", "The demo hall is running: keys 1 to 7 jump between the stations")
end

function SceneOverlay:on_update(dt)
  debug_draw.box(vec3(-2.5, 0, -2.5), vec3(2.5, 3.6, 2.5), vec4(1, 0.8, 0.2, 1), 0)
  debug_draw.box(vec3(5.5, 0, 0), vec3(7.5, 2, 3), vec4(0.2, 1, 0.4, 1), 0)
end

function SceneOverlay:on_destroy()
  log("Demo", "The demo hall stopped")
end

return SceneOverlay
"""

SCRIPTS["demo_ui.lua"] = """-- The HUD (assets/ui/demo_hud.wuipanel) on the entity that has the UICanvas: change a widget (score, health bar, beacon bar),
-- listen to one (the button switches the light, the slider sets its intensity) and use a widget that follows an entity on the screen.
local DemoUi = Script:extend{
  properties = {},
}

function DemoUi:on_init()
  self.panel = UI.panel(self.entity)
  self.light = find_entity("Demo light")
  self.t = 0
  self.on = true
  self.ready = false
end

-- The panel is an asset that loads in the background: its widgets are looked up once it is ready.
function DemoUi:setup()
  local panel = self.panel
  self.score = panel:find("Stats/Score")
  self.health = panel:find("Stats/Health")
  self.beacon = panel:find("BeaconTag/BeaconBar")
  self.label = panel:find("Controls/ToggleLight/Label")
  self.slider = panel:find("Controls/LightIntensity")
  self.level = self.slider.value

  panel:find("Controls/ToggleLight"):on("click", function(me)
    Audio.post("UI_Click")
    me.on = not me.on
    me:apply_light()
  end)
  self.slider:on("value_changed", function(me, value)
    me.level = value
    me:apply_light()
  end)

  self.ready = true
  self:apply_light()
end

function DemoUi:apply_light()
  if self.light ~= nil and self.light:is_valid() then
    self.light:get("Light").intensity = self.on and self.level or 0
  end
  self.label.text = self.on and "Light: on" or "Light: off"
end

function DemoUi:on_update(dt)
  if not self.ready then
    if self.panel == nil or not self.panel.ready then
      return
    end
    self:setup()
  end

  self.t = self.t + dt
  self.score.text = "Score " .. math.floor(self.t * 10)
  self.health.value = 0.5 + 0.5 * math.sin(self.t)
  self.beacon.value = (self.t * 0.2) % 1
end

return DemoUi
"""

SCRIPTS["input_demo.lua"] = """-- Input actions (assets/input/demo.winput): a ball moved with Move and Jump, Fire flashes, and the HUD shows the device, the Move vector
-- and the Jump bindings; "Rebind Jump" listens for the next key and saves it. In the editor click the Viewport image first.
local InputDemo = Script:extend{
  properties = {
    speed = { type = "float", default = 4, min = 0, max = 20 },
    jump_speed = { type = "float", default = 6, min = 0, max = 20 },
  },
}

function InputDemo:on_init()
  self.panel = UI.panel(find_entity("Hud"))
  self.pushed = false
  self.ready = false
  self.listening = false
  self.want_jump = false
end

function InputDemo:setup()
  local panel = self.panel
  self.device = panel:find("InputInfo/Device")
  self.move = panel:find("InputInfo/Move")
  self.jump = panel:find("InputInfo/JumpKey")
  self.rebind_label = panel:find("InputInfo/RebindJump/Label")
  panel:find("InputInfo/RebindJump"):on("click", function(me) Audio.post("UI_Click") me:start_rebind() end)
  self.ready = true
end

function InputDemo:start_rebind()
  if self.listening then
    return
  end
  self.listening = true
  self.rebind_label.text = "Press a key..."
  input.listen(function(control)
    input.rebind("Gameplay", "Jump", 1, control)
    input.save_bindings()
  end)
end

function InputDemo:update_hud()
  if not self.ready then
    if self.panel == nil or not self.panel.ready then
      return
    end
    self:setup()
  end

  -- Escape cancels a listen without calling back; the prompt goes back to normal either way
  if self.listening and not input.listening() then
    self.listening = false
    self.rebind_label.text = "Rebind Jump"
  end

  local move = input.action("Gameplay", "Move"):vector()
  self.device.text = project.name .. " " .. project.version .. " - Device: " .. input.last_device()
  self.move.text = string.format("Move: %.2f, %.2f", move.x, move.y)

  local keys = {}
  for _, binding in ipairs(input.bindings("Gameplay", "Jump")) do
    keys[#keys + 1] = binding.controls[1]
  end
  self.jump.text = "Jump: " .. table.concat(keys, ", ")
end

function InputDemo:on_update(dt)
  -- the Gameplay map lives in the project's input map, which loads in the background: push it as soon as it is there
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
    if not self.pushed then
      return
    end
  end

  -- a press lasts one frame and a frame is shorter than a physics step: it is kept until the next step uses it
  if input.action("Gameplay", "Jump"):pressed() then
    self.want_jump = true
  end

  if input.action("Gameplay", "Fire"):pressed() then
    debug_draw.sphere(self.transform.position, 0.7, vec4(1, 0.3, 0.1, 1), 0.3)
  end

  self:update_hud()
end

function InputDemo:on_physics_update(dt)
  if not self.pushed then
    return
  end

  local body = self.entity:body()
  local velocity = body:get_velocity() or vec3(0, 0, 0)
  local move = input.action("Gameplay", "Move"):vector()
  body:add_impulse(vec3((move.x * self.speed - velocity.x) * 0.5, 0, (-move.y * self.speed - velocity.z) * 0.5))

  if self.want_jump then
    self.want_jump = false
    body:add_impulse(vec3(0, self.jump_speed, 0))
  end
end

return InputDemo
"""

SCRIPTS["character_demo.lua"] = """-- A CharacterController walked with the Move action, or (autopilot) back and forth along its lane with a jump at each turn.
local CharacterDemo = Script:extend{
  properties = {
    speed = { type = "float", default = 4, min = 0, max = 20 },
    jump_speed = { type = "float", default = 6, min = 0, max = 20 },
    autopilot = { type = "bool", default = false },
    leg = { type = "float", default = 4, min = 0.5, max = 60 },
  },
}

function CharacterDemo:on_init()
  self.pushed = false
  self.want_jump = false
  self.t = 0
  self.direction = 1
end

function CharacterDemo:on_update(dt)
  if self.autopilot then
    return
  end
  if not self.pushed then
    self.pushed = pcall(input.push_map, "Gameplay")
    if not self.pushed then
      return
    end
  end
  if input.action("Gameplay", "Jump"):pressed() then
    self.want_jump = true
  end
end

function CharacterDemo:on_physics_update(dt)
  local move
  if self.autopilot then
    self.t = self.t + dt
    if self.t >= self.leg then
      self.t = 0
      self.direction = -self.direction
      self.want_jump = true
    end
    move = vec3(self.direction * self.speed, 0, 0)
  else
    if not self.pushed then
      return
    end
    local stick = input.action("Gameplay", "Move"):vector()
    move = vec3(stick.x * self.speed, 0, -stick.y * self.speed)
  end

  character.move(self.entity, move)
  if self.want_jump then
    self.want_jump = false
    if character.is_grounded(self.entity) then
      character.jump(self.entity, self.jump_speed)
    end
  end
end

return CharacterDemo
"""

SCRIPTS["joint_motor_swing.lua"] = """-- Drives the Joint of its entity with a velocity motor and turns it around every period.
local Swing = Script:extend{
  properties = {
    speed = { type = "float", default = 1, min = 0, max = 20 },
    period = { type = "float", default = 3, min = 0.2, max = 30 },
    max_force = { type = "float", default = 2000, min = 0, max = 100000 },
  },
}

function Swing:on_init()
  self.t = 0
  self.direction = 1
  self.started = false
end

function Swing:on_physics_update(dt)
  if not self.started then
    self.started = true
    joint.set_motor(self.entity, "velocity", self.direction * self.speed, self.max_force)
  end
  self.t = self.t + dt
  if self.t >= self.period then
    self.t = 0
    self.direction = -self.direction
    joint.set_motor(self.entity, "velocity", self.direction * self.speed, self.max_force)
  end
end

return Swing
"""

SCRIPTS["joint_breaker.lua"] = """-- Hears on_joint_break when its Joint goes over its break force.
local Breaker = Script:extend{}

function Breaker:on_joint_break(other)
  log("Joints", self.name .. " broke away" .. (other and (" from " .. other.name) or ""))
  debug_draw.sphere(self.transform.position, 0.6, vec4(1, 0.3, 0.1, 1), 1.0)
end

return Breaker
"""

SCRIPTS["joint_wrecker.lua"] = """-- Launches its body once with set_velocity.
local Wrecker = Script:extend{
  properties = {
    velocity = { type = "vec3", default = { -10, 0, 0 } },
  },
}

function Wrecker:on_init()
  self.launched = false
end

function Wrecker:on_physics_update(dt)
  if not self.launched then
    self.launched = true
    self.entity:body():set_velocity(self.velocity)
  end
end

return Wrecker
"""

SCRIPTS["slide_platform.lua"] = """-- Moves a kinematic body along x with the physics step; characters standing on it ride along.
local SlidePlatform = Script:extend{
  properties = {
    amplitude = { type = "float", default = 2.5, min = 0, max = 20 },
    speed = { type = "float", default = 0.6, min = 0, max = 5 },
  },
}

function SlidePlatform:on_init()
  self.base_x = self.transform.position.x
  self.t = 0
end

function SlidePlatform:on_physics_update(dt)
  self.t = self.t + dt
  self.transform.position.x = self.base_x + self.amplitude * math.sin(self.t * self.speed)
end

return SlidePlatform
"""

SCRIPTS["physics_hopper.lua"] = """-- Body control, a raycast and collision events: hops by setting its velocity, draws a line to the ground, marks hard hits.
local Hopper = Script:extend{
  properties = {
    interval = { type = "float", default = 3, min = 0.5, max = 20 },
    speed = { type = "float", default = 6, min = 0, max = 30 },
  },
}

function Hopper:on_init()
  self.t = 0
end

function Hopper:on_physics_update(dt)
  self.t = self.t + dt
  if self.t >= self.interval then
    self.t = 0
    self.entity:body():set_velocity(vec3(0, self.speed, 0))
  end

  local from = self.transform.position
  local hit = physics.raycast(from, vec3(0, -1, 0), 20)
  if hit then
    debug_draw.line(from, hit.point, vec4(1, 1, 0, 1), 0.05)
  end
end

function Hopper:on_collision_enter(other, contact)
  if contact.impulse > 30 then
    debug_draw.sphere(contact.point, 0.25, vec4(1, 0.5, 0, 1), 0.4)
    Audio.post("Impact_Soft", self.entity)
  end
end

return Hopper
"""

SCRIPTS["trigger_zone.lua"] = """-- A trigger collider: lights the Light of its entity while something is inside.
local TriggerZone = Script:extend{
  properties = {
    glow = { type = "float", default = 25, min = 0, max = 200 },
  },
}

function TriggerZone:on_init()
  self.inside = 0
end

function TriggerZone:on_trigger_enter(other)
  self.inside = self.inside + 1
  self.entity:get("Light").intensity = self.glow
  log("Physics", other.name .. " entered " .. self.name)
end

function TriggerZone:on_trigger_exit(other)
  self.inside = math.max(0, self.inside - 1)
  if self.inside == 0 then
    self.entity:get("Light").intensity = 0
  end
end

return TriggerZone
"""

SCRIPTS["animation_demo.lua"] = """-- The Animation API: plays the idle clip and crossfades between it and the run clip every period; "footstep" events are counted.
-- With autopilot off the Move action decides (the Gameplay map must be on the stack).
local AnimationDemo = Script:extend{
  properties = {
    autopilot = { type = "bool", default = true },
    period = { type = "float", default = 3, min = 0.5, max = 20 },
    fade = { type = "float", default = 0.2, min = 0, max = 2 },
    idle = { type = "string", default = "CharacterArmature|Idle" },
    run = { type = "string", default = "CharacterArmature|Run" },
  },
}

function AnimationDemo:on_init()
  self.anim = Animation.animator(self.entity)
  self.running = false
  self.pushed = false
  self.t = 0
  self.steps = 0
  if self.anim == nil then
    return
  end
  self.anim:on("footstep", function(me, event)
    me.steps = me.steps + 1
  end)
  self.anim:play(self.idle, { loop = true })
end

function AnimationDemo:on_update(dt)
  if self.anim == nil then
    return
  end

  local want_run
  if self.autopilot then
    self.t = self.t + dt
    want_run = (math.floor(self.t / self.period) % 2) == 1
  else
    if not self.pushed then
      self.pushed = pcall(input.push_map, "Gameplay")
    end
    local stick = self.pushed and input.action("Gameplay", "Move"):vector() or vec2(0, 0)
    want_run = stick:length() > 0.2
  end

  if want_run ~= self.running then
    self.running = want_run
    self.anim:crossfade(want_run and self.run or self.idle, self.fade, { loop = true })
  end
end

return AnimationDemo
"""

SCRIPTS["animation_graph_demo.lua"] = """-- The animation graph API: the warrior plays a graph (idle, and a walk/run blend space on "speed", with an upper body layer for the sword
-- attack). The script only sets parameters: it runs and stands every period (it starts running), and swings the sword every attack_every seconds
-- (the layer weight "upper" follows the attack). "footstep" events of the clips are counted. With autopilot off the Move action decides
-- (the Gameplay map must be on the stack).
local AnimationGraphDemo = Script:extend{
  properties = {
    autopilot = { type = "bool", default = true },
    period = { type = "float", default = 4, min = 0.5, max = 20 },
    attack_every = { type = "float", default = 3, min = 0.5, max = 20 },
    run_speed = { type = "float", default = 3, min = 0, max = 5 },
  },
}

function AnimationGraphDemo:on_init()
  self.anim = Animation.animator(self.entity)
  self.pushed = false
  self.t = 0
  self.speed = 0
  self.upper = 0
  self.next_attack = self.attack_every
  self.attack_until = 0
  self.attack_pending = false
  self.steps = 0
  if self.anim == nil then
    return
  end
  self.anim:on("footstep", function(me, event)
    me.steps = me.steps + 1
    -- the entity has an AudioEmitter (its Event is empty): the step is heard from where the warrior is
    Audio.post("Footstep_" .. (me.steps % 3 + 1), me.entity)
  end)
end

function AnimationGraphDemo:on_update(dt)
  if self.anim == nil then
    return
  end
  self.t = self.t + dt

  local target
  if self.autopilot then
    target = (math.floor(self.t / self.period) % 2) == 0 and self.run_speed or 0
  else
    if not self.pushed then
      self.pushed = pcall(input.push_map, "Gameplay")
    end
    local stick = self.pushed and input.action("Gameplay", "Move"):vector() or vec2(0, 0)
    target = stick:length() * self.run_speed
  end
  self.speed = self.speed + (target - self.speed) * math.min(1, dt * 20)
  self.anim:set("speed", self.speed)

  -- the sword swing: the upper body layer fades in, then the trigger starts the swing, and the layer fades out when it is over
  if self.t >= self.next_attack then
    self.next_attack = self.t + self.attack_every
    self.attack_pending = true
  end
  local want_upper = (self.attack_pending or self.t < self.attack_until) and 1 or 0
  self.upper = self.upper + (want_upper - self.upper) * math.min(1, dt * 30)
  self.anim:set("upper", self.upper)
  if self.attack_pending and self.upper > 0.9 then
    self.attack_pending = false
    self.attack_until = self.t + 0.9
    self.anim:trigger("attack")
  end
end

return AnimationGraphDemo
"""

SCRIPTS["clip_player.lua"] = """-- Loops one named clip of the model on its entity (the lineup of the animation station).
local ClipPlayer = Script:extend{
  properties = {
    clip = { type = "string", default = "" },
    speed = { type = "float", default = 1, min = 0, max = 4 },
  },
}

function ClipPlayer:on_init()
  local anim = Animation.animator(self.entity)
  if anim ~= nil and self.clip ~= "" then
    anim:play(self.clip, { loop = true, speed = self.speed })
  end
end

return ClipPlayer
"""


SCRIPTS["decal_footprints.lua"] = """-- Decals from a script: every "footstep" event of the entity's animator leaves a footprint (Decal.spawn) that fades out on its own, and the
-- mesh of the character does not receive decals itself (MeshRenderer.ReceivesDecals).
local DecalFootprints = Script:extend{
  properties = {
    lifetime = { type = "float", default = 4, min = 0.5, max = 30 },
  },
}

local function stop_receiving(entity)
  for _, child in ipairs(entity.children) do
    if child:has("MeshRenderer") then
      child:get("MeshRenderer").receivesdecals = false
    end
    stop_receiving(child)
  end
end

function DecalFootprints:on_init()
  self.left = true
  self.material = load_asset("assets/materials/decal_footprint.wmat")
  stop_receiving(self.entity)
  local anim = Animation.animator(self.entity)
  if anim ~= nil then
    anim:on("footstep", function(me, event)
      me:step()
    end)
  end
end

function DecalFootprints:step()
  local p = self.transform.position
  local side = self.left and -0.18 or 0.18
  self.left = not self.left
  Decal.spawn{ material = self.material, position = vec3(p.x + side, 0.02, p.z), normal = vec3(0, 1, 0), size = vec3(0.3, 0.5, 0.4),
    lifetime = self.lifetime, fade_in = 0.1, fade_out = 1.5, roll = 180, sort_order = 5, name = "Footprint" }
end

return DecalFootprints
"""


SCRIPTS["nav_patrol.lua"] = """-- Walks the entity's NavAgent between two points of the navmesh, for ever: Nav.agent(...):set_destination, and on_nav_arrived sends it to the other point.
-- The agent has to try again until the navmesh of the scene has loaded (set_destination is false before there is a world).
local NavPatrol = Script:extend{
  properties = {
    a = { type = "vec3", default = { 0, 0, 0 } },
    b = { type = "vec3", default = { 0, 0, 5 } },
  },
}

function NavPatrol:on_init()
  self.agent = Nav.agent(self.entity)
  self.to_b = true
  self.started = false
end

function NavPatrol:target()
  local p = self.to_b and self.b or self.a
  return vec3(p.x, p.y, p.z)
end

function NavPatrol:on_update(dt)
  if not self.started and self.agent:set_destination(self:target()) then
    self.started = true
  end
end

function NavPatrol:on_nav_arrived()
  self.to_b = not self.to_b
  self.agent:set_destination(self:target())
end

function NavPatrol:on_nav_failed()
  log_warning("NavPatrol", "no route to the next point")
end

return NavPatrol
"""


SCRIPTS["heard_check.lua"] = """-- A behaviour tree condition leaf (LuaCondition): succeeds while the guard's blackboard holds a noise it has not gone to look at yet (heard_kind is set
-- by the Perceiver when it hears AI.emit_noise; the tree clears it after the visit).
return BtCondition:extend{
  on_tick = function(self, bb, dt)
    if bb:get("heard_kind") ~= "" then return "success" end
    return "failure"
  end,
}
"""


SCRIPTS["noise_maker.lua"] = """-- Makes a noise at its own position every few seconds (AI.emit_noise): the guards of the behaviour tree demo that hear it go and look.
local NoiseMaker = Script:extend{
  properties = {
    interval = { type = "float", default = 3.0, min = 0.5, max = 30 },
    loudness = { type = "float", default = 1.0, min = 0.1, max = 10 },
  },
}

function NoiseMaker:on_init()
  self.t = 1.0
end

function NoiseMaker:on_update(dt)
  self.t = self.t - dt
  if self.t <= 0 then
    self.t = self.interval
    AI.emit_noise(self.transform.position, self.loudness, "crash")
  end
end

return NoiseMaker
"""


def build_scripts():
	ids = {}
	for rel, text in SCRIPTS.items():
		path = "scripts/" + rel
		write(path, text)
		ids[rel.split("/")[-1][:-4]] = write_meta(path, "ScriptImporter", 1)
	return ids


# ---------------------------------------------------------------------------------------------------------------- decals

def decal_textures():
	"""Procedural textures of the decals (no downloads, no third-party art): a rune circle (color and glow), a bullet hole with a normal map, a
	scorch mark and a footprint. RGBA PNGs: the alpha is where the decal is."""
	import numpy as np
	from PIL import Image, ImageDraw, ImageFilter

	def png(rel, array, kind):
		buf = io.BytesIO()
		Image.fromarray(array).save(buf, "PNG")
		write(rel, buf.getvalue(), binary=True)
		write_texture_meta(rel, kind)

	def smoothstep(a, b, x):
		t = np.clip((x - a) / (b - a), 0.0, 1.0)
		return t * t * (3 - 2 * t)

	def disc(n):
		y, x = np.mgrid[0:n, 0:n].astype(np.float64)
		c = (n - 1) / 2
		return (x - c) / c, (y - c) / c

	rng = np.random.default_rng(7)

	# the rune circle: three rings, twelve spokes between the inner two, and six ticks outside
	n = 256
	dx, dy = disc(n)
	r, a = np.hypot(dx, dy), np.arctan2(dy, dx)
	ring = lambda r0, w: np.exp(-((r - r0) / w) ** 2)
	spokes = ((np.abs(((a * 12 / (2 * math.pi)) % 1) - 0.5) > 0.46) & (r > 0.35) & (r < 0.72)).astype(np.float64)
	ticks = ((np.abs(((a * 6 / (2 * math.pi)) % 1) - 0.5) > 0.44) & (r > 0.76) & (r < 0.9)).astype(np.float64)
	strokes = np.clip(ring(0.95, 0.018) + ring(0.72, 0.014) + ring(0.35, 0.014) + spokes * 0.9 + ticks * 0.9, 0, 1) * (r < 0.99)
	color = np.zeros((n, n, 4), np.uint8)
	color[..., 0], color[..., 1], color[..., 2], color[..., 3] = 255, 150, 40, (strokes * 255).astype(np.uint8)
	png("textures/decals/rune_color.png", color, "color")
	glow = np.zeros((n, n, 3), np.uint8)
	glow[..., 0], glow[..., 1], glow[..., 2] = (strokes * 255).astype(np.uint8), (strokes * 170).astype(np.uint8), (strokes * 60).astype(np.uint8)
	png("textures/decals/rune_emissive.png", glow, "color")

	# the bullet hole: a dark dent with a raised rim, and its normal map (OpenGL convention, +Y up)
	n = 128
	dx, dy = disc(n)
	r = np.hypot(dx, dy)
	height = -1.0 * (1 - smoothstep(0.0, 0.5, r)) + 0.35 * np.exp(-((r - 0.55) / 0.12) ** 2)
	height *= (r < 0.95)
	gy, gx = np.gradient(height)
	normal = np.dstack((-gx * 14, gy * 14, np.ones_like(height)))
	normal /= np.linalg.norm(normal, axis=2, keepdims=True)
	normal_png = np.zeros((n, n, 4), np.uint8)
	normal_png[..., :3] = ((normal * 0.5 + 0.5) * 255).astype(np.uint8)
	normal_png[..., 3] = 255
	png("textures/decals/bullet_normal.png", normal_png, "normal")
	alpha = smoothstep(0.95, 0.55, r) * (0.55 + 0.45 * smoothstep(0.6, 0.0, r))
	hole = np.zeros((n, n, 4), np.uint8)
	hole[..., 0], hole[..., 1], hole[..., 2] = 22, 20, 18
	hole[..., 3] = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
	png("textures/decals/bullet_color.png", hole, "color")

	# the scorch mark: a soft blot with an uneven edge
	n = 256
	dx, dy = disc(n)
	r = np.hypot(dx, dy)
	noise = Image.fromarray((rng.random((n // 8, n // 8)) * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC).filter(ImageFilter.GaussianBlur(4))
	r = r + (np.asarray(noise, np.float64) / 255 - 0.5) * 0.5
	scorch = np.zeros((n, n, 4), np.uint8)
	scorch[..., 0], scorch[..., 1], scorch[..., 2] = 12, 10, 9
	scorch[..., 3] = (np.clip(smoothstep(0.95, 0.2, r) * 0.92, 0, 1) * 255).astype(np.uint8)
	png("textures/decals/scorch_color.png", scorch, "color")

	# a footprint, pointing up: a sole and a heel
	img = Image.new("L", (128, 256), 0)
	draw = ImageDraw.Draw(img)
	draw.ellipse((26, 20, 102, 150), fill=255)
	draw.ellipse((38, 160, 90, 236), fill=255)
	mask = np.asarray(img.filter(ImageFilter.GaussianBlur(2.5)), np.uint8)
	foot = np.zeros((256, 128, 4), np.uint8)
	foot[..., 0], foot[..., 1], foot[..., 2] = 30, 26, 22
	foot[..., 3] = (mask * 0.85).astype(np.uint8)
	png("textures/decals/footprint_color.png", foot, "color")
	write("textures/decals/LICENSE.txt", "Generated by scripts/gen_demo_scene.py (no third-party art); CC0 1.0.\n")


def decal_materials():
	"""The materials of the decals: only the textures, the factors and the base color alpha matter (a decal ignores the alpha mode)."""
	decal_textures()
	m = {}
	m["decal_rune"] = wmat("decal_rune", roughness=0.4, emissive=(3.0, 1.6, 0.5), textures={"baseColor": "../textures/decals/rune_color.png", "emissive": "../textures/decals/rune_emissive.png"})
	m["decal_bullet"] = wmat("decal_bullet", roughness=0.6, textures={"baseColor": "../textures/decals/bullet_color.png", "normal": "../textures/decals/bullet_normal.png"})
	m["decal_scorch"] = wmat("decal_scorch", roughness=1.0, textures={"baseColor": "../textures/decals/scorch_color.png"})
	m["decal_footprint"] = wmat("decal_footprint", roughness=0.9, textures={"baseColor": "../textures/decals/footprint_color.png"})
	return m


def add_decals(s, g):
	"""The decals of the demo (see docs/superpowers/specs/decals.md): a rune circle that glows on the ground, bullet holes with a normal map on a
	column, a scorch mark across the ground and a crate, and (scripts/decal_footprints.lua on the warrior) footprints at its footsteps."""
	def decal(material, size, sort_order=0, **extra):
		return {"Decal": dict({"Material": ref(g["decals"][material]), "Size": list(size), "SortOrder": sort_order}, **extra)}

	up = qaxis((1, 0, 0), -90)  # turns the box's +Z to +Y: the decal projects straight down
	s.add("Decals station", "World")
	s.add("Decal rune circle", "Decals station", pos=(-3, 0.3, 6.5), rot=qmul(up, qaxis((0, 0, 1), 20)), **decal("decal_rune", (4.2, 4.2, 1.0), 1))
	for i, (x, y) in enumerate(((-6.2, 1.4), (-5.85, 2.1), (-6.1, 3.0), (-5.7, 3.7))):
		s.add("Decal bullet hole %d" % (i + 1), "Decals station", pos=(x, y, -3.5), rot=qaxis((0, 0, 1), 37 * i), **decal("decal_bullet", (0.4, 0.4, 0.5), 2 + i))
	s.add("Decal scorch mark", "Decals station", pos=(3.5, 0.2, -3.2), rot=up, **decal("decal_scorch", (3.4, 3.4, 2.0), 0))


def patch_scene_with_decals(g):
	"""--decals-only: adds the decals to the scene that is already written (no downloads): the decals station, and the footprint script on the warrior."""
	path = ASSETS / "scenes/demo.wscene"
	scene = json.loads(path.read_text(encoding="utf-8"))
	fresh = Scene()
	fresh.ids["World"] = guid("entity:World")
	add_decals(fresh, g)
	ours = set(e["id"] for e in fresh.entities)
	entities = [e for e in scene["entities"] if e["id"] not in ours]
	entities.extend(fresh.entities)
	warrior = next(e for e in entities if e["id"] == guid("entity:Warrior (animated)"))
	warrior["components"]["script:" + g["scripts"]["decal_footprints"]] = {}
	scene["entities"] = entities
	write_json("scenes/demo.wscene", scene)


# -------------------------------------------------------------------------------------------------------------------- vfx

SCRIPTS["vfx_demo.lua"] = """-- Particles from a script: plays a one-shot effect (Vfx.spawn) at its own position every few seconds; the entity that spawned it is removed by itself
-- when the effect is over. A placed ParticleSystem (the campfire, the fountain) needs no script.
local VfxDemo = Script:extend{
  properties = {
    interval = { type = "float", default = 2.5, min = 0.2, max = 30 },
  },
}

function VfxDemo:on_init()
  self.effect = load_asset("assets/vfx/impact_sparks.wfx")
  self.t = self.interval
end

function VfxDemo:on_update(dt)
  self.t = self.t - dt
  if self.t <= 0 then
    self.t = self.interval
    Vfx.spawn{ effect = self.effect, position = self.transform.position, name = "Impact sparks" }
  end
end

return VfxDemo
"""

SCRIPTS["vfx_footsteps.lua"] = """-- Particles from animation events: every "footstep" event of the entity's animator kicks up a puff of dust at its feet (Vfx.spawn).
local VfxFootsteps = Script:extend{
  properties = {
    spread = { type = "float", default = 0.18, min = 0, max = 1 },
  },
}

function VfxFootsteps:on_init()
  self.left = true
  self.effect = load_asset("assets/vfx/footstep_dust.wfx")
  local anim = Animation.animator(self.entity)
  if anim ~= nil then
    anim:on("footstep", function(me, event)
      me:puff()
    end)
  end
end

function VfxFootsteps:puff()
  local p = self.transform.position
  local side = self.left and -self.spread or self.spread
  self.left = not self.left
  Vfx.spawn{ effect = self.effect, position = vec3(p.x + side, 0.05, p.z), name = "Footstep dust" }
end

return VfxFootsteps
"""


def vfx_textures():
	"""Procedural sprites of the effects (no downloads, no third-party art): a soft glow, a flame, a smoke puff and a spark. RGBA PNGs (white, alpha is the shape);
	the smoke is a sheet of 2 x 2 puffs."""
	import numpy as np
	from PIL import Image, ImageFilter

	def png(rel, array):
		buf = io.BytesIO()
		Image.fromarray(array).save(buf, "PNG")
		write(rel, buf.getvalue(), binary=True)
		write_texture_meta(rel, "color")

	def smoothstep(a, b, x):
		t = np.clip((x - a) / (b - a), 0.0, 1.0)
		return t * t * (3 - 2 * t)

	def disc(n):
		y, x = np.mgrid[0:n, 0:n].astype(np.float64)
		c = (n - 1) / 2
		return (x - c) / c, (y - c) / c

	def white(alpha):
		out = np.full(alpha.shape + (4,), 255, np.uint8)
		out[..., 3] = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
		return out

	def blurred_noise(cells, n, blur):
		small = Image.fromarray((rng.random((cells, cells)) * 255).astype(np.uint8))
		return np.asarray(small.resize((n, n), Image.BICUBIC).filter(ImageFilter.GaussianBlur(blur)), np.float64) / 255

	rng = np.random.default_rng(11)

	n = 128
	dx, dy = disc(n)
	r = np.hypot(dx, dy)
	png("textures/vfx/glow.png", white((1 - smoothstep(0.0, 1.0, r)) ** 2))

	# a spark: a bright core with a soft halo
	png("textures/vfx/spark.png", white(np.exp(-(r / 0.22) ** 2) + 0.35 * (1 - smoothstep(0.0, 1.0, r)) ** 3))

	# a flame: a teardrop, narrow at the top (v = 0 is the top of the image), wobbled by a little noise
	y, x = np.mgrid[0:n, 0:n].astype(np.float64)
	u, v = (x / (n - 1)) * 2 - 1, y / (n - 1)
	width = np.sin(np.clip(v, 0, 1) * math.pi * 0.85 + 0.25) * 0.7 * (0.35 + 0.65 * v)
	d = np.abs(u + (blurred_noise(8, n, 3) - 0.5) * 0.3) / np.maximum(width, 1e-3)
	flame = (1 - smoothstep(0.35, 1.0, d)) * smoothstep(0.0, 0.12, v) * (1 - smoothstep(0.93, 1.0, v))
	png("textures/vfx/flame.png", white(flame))

	# smoke: four puffs of a blurred noise blob, in a 2 x 2 sheet
	cell = 128
	sheet = np.zeros((cell * 2, cell * 2), np.float64)
	for i in range(4):
		puff = smoothstep(1.0, 0.25, r + (blurred_noise(6, cell, 5) - 0.5) * 0.7) * 0.8
		sheet[(i // 2) * cell:(i // 2 + 1) * cell, (i % 2) * cell:(i % 2 + 1) * cell] = puff
	png("textures/vfx/smoke_sheet.png", white(sheet))
	write("textures/vfx/LICENSE.txt", "Generated by scripts/gen_demo_scene.py (no third-party art); CC0 1.0.\n")


def vfx_effects():
	"""The materials and the .wfx effects of the demo. Sprites only (ribbons and mesh particles are not drawn yet, see vfx.md)."""
	vfx_textures()
	m = {}
	for name, tex in (("glow", "glow"), ("spark", "spark"), ("flame", "flame"), ("smoke", "smoke_sheet")):
		m[name] = wmat("vfx_" + name, roughness=1.0, alpha="blend", textures={"baseColor": "../textures/vfx/%s.png" % tex})

	def curve(*keys):
		return {"curve": [list(k) for k in keys]}

	def emitter(name, material, **fields):
		e = {"name": name, "renderer": "sprite", "material": ref(m[material]), "blend": "additive", "align": "camera", "softDistance": 0.4, "intensity": 1.0,
			"maxParticles": 1024}
		e.update(fields)
		return e

	fx = {}
	fx["campfire"] = {"version": 1, "name": "Campfire", "emitters": [
		emitter("Flames", "flame", intensity=2.5, maxParticles=256,
			spawn={"looping": True, "rate": 45, "shape": "cone", "shapeSize": [0.18, 0, 0], "coneAngle": 8, "worldSpace": True},
			start={"lifetime": [0.55, 0.95], "speed": [1.0, 1.6], "size": [0.45, 0.7], "rotation": [-12, 12], "color": [1, 0.75, 0.3, 1]},
			forces={"noiseStrength": 1.2, "noiseFrequency": 1.5},
			overLife={"size": curve((0, 0.6), (0.2, 1.0), (1, 0.15)), "alpha": curve((0, 0), (0.1, 1), (0.7, 0.8), (1, 0)),
				"color": [{"time": 0, "color": [1, 0.95, 0.6, 1]}, {"time": 0.5, "color": [1, 0.5, 0.12, 1]}, {"time": 1, "color": [0.7, 0.1, 0.02, 1]}]}),
		emitter("Smoke", "smoke", blend="alpha", intensity=1.0, sorted=True, softDistance=0.8, maxParticles=128, sheet=[2, 2],
			spawn={"looping": True, "rate": 7, "shape": "cone", "shapeSize": [0.2, 0, 0], "coneAngle": 14},
			start={"lifetime": [2.6, 3.6], "speed": [0.6, 1.0], "size": [0.5, 0.8], "rotation": [0, 360], "color": [0.16, 0.15, 0.15, 0.55]},
			forces={"gravity": [0.15, 0.15, 0], "drag": 0.3, "noiseStrength": 0.8, "noiseFrequency": 0.7},
			overLife={"size": curve((0, 0.5), (1, 2.6)), "rotationSpeed": [-25, 25], "alpha": curve((0, 0), (0.2, 1), (1, 0)), "frame": curve((0, 0), (1, 0))}),
		emitter("Embers", "spark", intensity=4.0, maxParticles=128,
			spawn={"looping": True, "rate": 9, "shape": "sphere", "shapeSize": [0.2, 0, 0]},
			start={"lifetime": [1.2, 2.4], "speed": [0.6, 1.4], "size": [0.025, 0.05], "color": [1, 0.55, 0.15, 1]},
			forces={"gravity": [0, 0.8, 0], "noiseStrength": 2.0, "noiseFrequency": 1.2},
			overLife={"alpha": curve((0, 1), (0.75, 1), (1, 0))}),
	]}

	fx["spark_fountain"] = {"version": 1, "name": "Spark fountain", "emitters": [
		emitter("Sparks", "spark", intensity=5.0, maxParticles=2048,
			collision={"mode": "depth", "response": "bounce", "restitution": 0.45},
			spawn={"looping": True, "rate": 140, "shape": "cone", "shapeSize": [0.05, 0, 0], "coneAngle": 16},
			start={"lifetime": [1.8, 2.8], "speed": [4.5, 6.5], "size": [0.035, 0.06], "color": [1, 0.8, 0.35, 1]},
			forces={"gravity": [0, -9.8, 0], "drag": 0.05},
			overLife={"alpha": curve((0, 1), (0.8, 1), (1, 0)),
				"color": [{"time": 0, "color": [1, 1, 0.8, 1]}, {"time": 0.6, "color": [1, 0.6, 0.2, 1]}, {"time": 1, "color": [0.8, 0.2, 0.05, 1]}]}),
		emitter("Glow", "glow", intensity=1.2, maxParticles=64, softDistance=0.2,
			spawn={"looping": True, "rate": 12, "shape": "point"},
			start={"lifetime": [0.4, 0.6], "speed": 0.0, "size": [0.5, 0.7], "color": [1, 0.6, 0.2, 0.5]},
			overLife={"alpha": curve((0, 0), (0.3, 1), (1, 0))}),
	]}

	fx["rune_motes"] = {"version": 1, "name": "Rune motes", "emitters": [
		emitter("Motes", "glow", intensity=3.0, maxParticles=512, softDistance=0.15,
			spawn={"looping": True, "rate": 30, "shape": "box", "shapeSize": [3.2, 0.05, 3.2], "worldSpace": True},
			start={"lifetime": [2.0, 3.5], "speed": [0.25, 0.7], "size": [0.05, 0.12], "color": [1, 0.65, 0.2, 1]},
			forces={"noiseStrength": 0.6, "noiseFrequency": 0.9},
			overLife={"alpha": curve((0, 0), (0.2, 1), (0.7, 1), (1, 0)), "size": curve((0, 0.6), (0.5, 1), (1, 0.3)),
				"color": [{"time": 0, "color": [1, 0.8, 0.4, 1]}, {"time": 1, "color": [1, 0.35, 0.1, 1]}]}),
	]}

	fx["impact_sparks"] = {"version": 1, "name": "Impact sparks", "emitters": [
		emitter("Burst", "spark", intensity=6.0, maxParticles=128,
			collision={"mode": "depth", "response": "bounce", "restitution": 0.35},
			spawn={"looping": False, "duration": 0.2, "rate": 0, "bursts": [{"time": 0, "count": 48}], "shape": "sphere", "shapeSize": [0.05, 0, 0]},
			start={"lifetime": [0.6, 1.2], "speed": [2.0, 5.0], "size": [0.03, 0.055], "color": [1, 0.85, 0.5, 1]},
			forces={"gravity": [0, -9.8, 0], "drag": 0.2},
			overLife={"alpha": curve((0, 1), (0.6, 1), (1, 0))}),
		emitter("Flash", "glow", intensity=3.0, maxParticles=8, softDistance=0.1,
			spawn={"looping": False, "duration": 0.2, "rate": 0, "bursts": [{"time": 0, "count": 1}], "shape": "point"},
			start={"lifetime": 0.18, "speed": 0.0, "size": 0.9, "color": [1, 0.8, 0.5, 1]},
			overLife={"alpha": curve((0, 1), (1, 0)), "size": curve((0, 0.4), (1, 1.2))}),
	]}

	fx["footstep_dust"] = {"version": 1, "name": "Footstep dust", "emitters": [
		emitter("Dust", "smoke", blend="alpha", intensity=1.0, sorted=True, softDistance=0.3, maxParticles=64, sheet=[2, 2],
			spawn={"looping": False, "duration": 0.2, "rate": 0, "bursts": [{"time": 0, "count": 5}], "shape": "sphere", "shapeSize": [0.06, 0, 0]},
			start={"lifetime": [0.5, 0.9], "speed": [0.25, 0.6], "size": [0.12, 0.2], "rotation": [0, 360], "color": [0.55, 0.5, 0.42, 0.45]},
			forces={"gravity": [0, 0.2, 0], "drag": 1.5},
			overLife={"size": curve((0, 0.6), (1, 2.0)), "alpha": curve((0, 1), (1, 0)), "frame": curve((0, 0), (1, 0))}),
	]}

	ids = {}
	for name, data in fx.items():
		rel = "vfx/%s.wfx" % name
		write_json(rel, data)
		ids[name] = write_meta(rel, "ParticleFileImporter", 1)
	return {"effects": ids, "materials": m}


def add_vfx(s, g):
	"""The particle effects of the demo (see docs/superpowers/specs/vfx.md): a campfire (flames, smoke, embers), a spark fountain that bounces off the ground
	through the depth buffer, motes rising from the rune circle of the decals station, impact sparks spawned from a script, and (scripts/vfx_footsteps.lua on
	the warrior) dust at its footsteps."""
	def system(effect):
		return {"ParticleSystem": {"Effect": ref(g["vfx"]["effects"][effect])}}

	def script(name, **props):
		return {"script:" + g["scripts"][name]: props}

	def log(name, yaw):
		return dict(MeshRenderer={"Mesh": ref(g["shapes"], MESH["cube"]), "Materials": [ref(g["mat"]["crate"])]})

	light = {"Light": {"Kind": "Point", "Color": [1, 0.55, 0.2, 1], "Intensity": 14, "Range": 8, "InnerAngle": 20, "OuterAngle": 30, "CastsShadows": False}}
	s.add("VFX station", "World")
	s.add("Campfire log 1", "VFX station", pos=(16, 0.1, -9), rot=qaxis((0, 1, 0), 30), scale=(0.9, 0.2, 0.2), **log("1", 30))
	s.add("Campfire log 2", "VFX station", pos=(16, 0.1, -9), rot=qaxis((0, 1, 0), -40), scale=(0.9, 0.2, 0.2), **log("2", -40))
	s.add("Campfire", "VFX station", pos=(16, 0.25, -9), **system("campfire"))
	s.add("Campfire light", "Campfire", pos=(0, 0.6, 0), **dict(light, **script("light_pulse", amplitude=4, speed=9)))
	s.add("Spark fountain", "VFX station", pos=(20, 0.3, -9), **system("spark_fountain"))
	s.add("Impact sparks emitter", "VFX station", pos=(24, 0.6, -9), **script("vfx_demo", interval=2.5))
	s.add("Rune motes", "Decals station", pos=(-3, 0.35, 6.5), **system("rune_motes"))


# ------------------------------------------------------------------------------------------------------------ navigation

NAV_CENTER = -30  # x of the navigation station: west of the hall, where nothing else stands
NAV_FILE = "navmeshes/Navmesh_volume_%s.wnav" % guid("entity:Navmesh volume")[:8]  # where the editor's bake puts it (NavigationPanel::bake)
NAV_GOALS = [(NAV_CENTER - 4 + 4 * i, -8) for i in range(3)]  # (x, z) of where the agents go; editor/src/play/demo_smoke.cpp checks them


def add_navigation(s, g):
	"""The navigation station (docs/superpowers/specs/navigation.md): a wall across an arena with an open way round each end, mud over the west way (the cost
	is in editor/nav.wsettings), and three agents that walk from the north side to the south side and back, round the wall, with nav_patrol.lua. The navmesh
	is baked by the editor from this scene (wiwa_editor --scene assets/scenes/demo.wscene --nav-bake <file>) into NAV_FILE; the volume refers to it."""
	cx = NAV_CENTER
	s.add("Navigation station", "World")
	s.add("Navmesh volume", "Navigation station", pos=(cx, 2.5, 0), NavMeshVolume={"NavMesh": ref(guid("asset:" + NAV_FILE)), "Size": [24, 7, 24]})
	crate = {"MeshRenderer": {"Mesh": ref(g["shapes"], MESH["cube"]), "Materials": [ref(g["mat"]["crate"])]}}
	s.add("Nav wall", "Navigation station", pos=(cx, 1, 0), scale=(16, 2, 1), **crate)
	s.add("Nav mud", "Navigation station", pos=(cx - 10, 0.05, 0), scale=(4, 0.1, 6), NavAreaTag={"Area": "Mud"},
		MeshRenderer={"Mesh": ref(g["shapes"], MESH["cube"]), "Materials": [ref(g["mat"]["marker"])]})
	for i, (gx, gz) in enumerate(NAV_GOALS):
		colour = ("neon_cyan", "neon_magenta", "neon_orange")[i]
		name = "Nav agent %d" % (i + 1)
		props = {"a": [gx, 0, 8], "b": [gx, 0, gz]}
		s.add(name, "Navigation station", pos=(gx, 0, 8), NavAgent={"Speed": 7, "Acceleration": 30}, **{"script:" + g["scripts"]["nav_patrol"]: props})
		s.add(name + " body", name, pos=(0, 0.9, 0), scale=(0.7, 1.8, 0.7),
			MeshRenderer={"Mesh": ref(g["shapes"], MESH["cylinder"]), "Materials": [ref(g["mat"][colour])]})


def write_nav_settings():
	"""The project's navigation settings: walking through mud costs 8 times as much, which sends the agents of the navigation station round it."""
	(ROOT / "editor" / "nav.wsettings").write_text(json.dumps({"areaCost": {"Mud": 8.0}}, indent=2) + "\n", encoding="utf-8", newline="\n")
	# the record of the navmesh asset, so that the guid the volume refers to is the one the baked file gets (the file itself comes from --nav-bake)
	(ASSETS / NAV_FILE).parent.mkdir(parents=True, exist_ok=True)
	write_meta(NAV_FILE, "NavMeshImporter", 1)
	print("bake the navmesh with: wiwa_editor --scene assets/scenes/demo.wscene --nav-bake assets/" + NAV_FILE)


def patch_scene_with_nav(g):
	"""--nav-only: adds the navigation station to the scene that is already written (no downloads, no other .meta files touched)."""
	path = ASSETS / "scenes/demo.wscene"
	scene = json.loads(path.read_text(encoding="utf-8"))
	fresh = Scene()
	fresh.ids["World"] = guid("entity:World")
	add_navigation(fresh, g)
	ours = set(e["id"] for e in fresh.entities)
	scene["entities"] = [e for e in scene["entities"] if e["id"] not in ours] + fresh.entities
	write_json("scenes/demo.wscene", scene)


# -------------------------------------------------------------------------------------------------------------- behaviour trees

AI_TREE_FILE = "ai/guard.wbt"


def guard_tree():
	"""The guard of the navigation station (docs/superpowers/specs/behavior-trees.md): a reactive Selector that chases what it sees, goes to look at a noise
	it heard (a Lua condition leaf says whether there is one), and otherwise patrols between two points. Perception fills target (seen_target),
	can_see_target, last_seen_position, heard_position and heard_kind of its blackboard."""
	cx = NAV_CENTER
	def node(nid, kind, children=(), **params):
		return {"id": nid, "type": kind, "name": "", "params": params, "children": list(children)}
	nodes = [
		node("root", "Selector", ["chase", "investigate", "patrol"], reactive=True),
		node("chase", "Guard", ["follow"], key="can_see_target", op="true"),
		node("follow", "MoveTo", target="key:seen_target", stoppingDistance=1.5),
		node("investigate", "Sequence", ["heard", "go_noise", "look", "forget"]),
		node("heard", "LuaCondition", script=guid("asset:scripts/heard_check.lua")),
		node("go_noise", "MoveTo", target="key:heard_position", stoppingDistance=1.0),
		node("look", "Wait", seconds=2.0),
		node("forget", "SetBlackboard", key="heard_kind", value=""),
		node("patrol", "Sequence", ["to_a", "wait_a", "to_b", "wait_b"]),
		node("to_a", "MoveTo", target=[cx + 10, 0, -6]),
		node("wait_a", "Wait", seconds=1.0),
		node("to_b", "MoveTo", target=[cx + 10, 0, 6]),
		node("wait_b", "Wait", seconds=1.0),
	]
	blackboard = [
		{"key": "seen_target", "type": "entity", "default": None},
		{"key": "can_see_target", "type": "bool", "default": False},
		{"key": "last_seen_position", "type": "vec3", "default": [0, 0, 0]},
		{"key": "heard_position", "type": "vec3", "default": [0, 0, 0]},
		{"key": "heard_kind", "type": "string", "default": ""},
	]
	return {"version": 1, "root": "root", "blackboard": blackboard, "nodes": nodes}


def write_ai_assets():
	"""The guard's tree and its asset record (fixed guid, so the scene can refer to it before the editor has imported it)."""
	write_json(AI_TREE_FILE, guard_tree())
	return write_meta(AI_TREE_FILE, "BehaviorTreeImporter", 1)


def add_ai(s, g):
	"""The behaviour tree demo, in the navigation station: a guard (NavAgent, Perceiver, AIController) that patrols, chases the thief it sees across the
	arena and looks at the noise a noise maker makes. editor/src/play/demo_smoke.cpp checks it (the thief's place and the names here)."""
	cx = NAV_CENTER
	cylinder = lambda colour: {"MeshRenderer": {"Mesh": ref(g["shapes"], MESH["cylinder"]), "Materials": [ref(g["mat"][colour])]}}
	s.add("Guard", "Navigation station", pos=(cx + 10, 0, 8), NavAgent={"Speed": 4.5, "Acceleration": 20},
		Perceiver={"SightRange": 12, "SightAngle": 120, "HearingRadius": 15, "Team": "guards", "Interval": 0.2},
		AIController={"Tree": ref(guid("asset:" + AI_TREE_FILE))})
	s.add("Guard body", "Guard", pos=(0, 0.9, 0), scale=(0.8, 1.8, 0.8), **cylinder("toon_red"))
	s.add("Thief", "Navigation station", pos=(cx + 10, 0, -10), Perceivable={"Team": "thieves", "Kind": "thief", "AimOffset": [0, 1, 0]})
	s.add("Thief body", "Thief", pos=(0, 0.9, 0), scale=(0.7, 1.8, 0.7), **cylinder("toon_yellow"))
	s.add("Noise maker", "Navigation station", pos=(cx + 13, 0.5, 0), **{"script:" + g["scripts"]["noise_maker"]: {"interval": 3.0}},
		MeshRenderer={"Mesh": ref(g["shapes"], MESH["sphere"]), "Materials": [ref(g["mat"]["marker"])]}, **{})


def patch_scene_with_ai(g):
	"""--ai-only: adds the behaviour tree demo to the scene that is already written (no downloads, no other .meta files touched)."""
	path = ASSETS / "scenes/demo.wscene"
	scene = json.loads(path.read_text(encoding="utf-8"))
	fresh = Scene()
	fresh.ids["Navigation station"] = guid("entity:Navigation station")
	add_ai(fresh, g)
	ours = set(e["id"] for e in fresh.entities)
	scene["entities"] = [e for e in scene["entities"] if e["id"] not in ours] + fresh.entities
	write_json("scenes/demo.wscene", scene)


def patch_scene_with_nested(g):
	"""--nested-only: adds the lamp pair (a prefab that holds prefabs) to the scene that is already written, without downloading anything."""
	path = ASSETS / "scenes/demo.wscene"
	scene = json.loads(path.read_text(encoding="utf-8"))
	fresh = Scene()
	fresh.ids["Lighting station"] = guid("entity:Lighting station")
	add_nested_lamps(fresh, g)
	ours = set(e["id"] for e in fresh.entities)
	scene["entities"] = [e for e in scene["entities"] if e["id"] not in ours] + fresh.entities
	write_json("scenes/demo.wscene", scene)


def patch_scene_with_vfx(g):
	"""--vfx-only: adds the effects to the scene that is already written (no downloads): the VFX station, the motes over the rune circle, and the footstep
	script on the warrior."""
	path = ASSETS / "scenes/demo.wscene"
	scene = json.loads(path.read_text(encoding="utf-8"))
	fresh = Scene()
	fresh.ids["World"] = guid("entity:World")
	fresh.ids["Decals station"] = guid("entity:Decals station")
	add_vfx(fresh, g)
	ours = set(e["id"] for e in fresh.entities)
	entities = [e for e in scene["entities"] if e["id"] not in ours]
	entities.extend(fresh.entities)
	warrior = next(e for e in entities if e["id"] == guid("entity:Warrior (animated)"))
	warrior["components"]["script:" + g["scripts"]["vfx_footsteps"]] = {}
	scene["entities"] = entities
	write_json("scenes/demo.wscene", scene)


# ------------------------------------------------------------------------------------------------------- UI and input

FONT = None


def build_ui(font, beacon_id, labels):
	widgets = []

	def w(id, kind, name, parent=None, **fields):
		d = {"id": id, "kind": kind, "Name": name}
		if parent:
			d["parent"] = parent
		d.update(fields)
		widgets.append(d)

	font_ref = ref(font)
	w(2, "VStack", "Stats", OffsetMin=[24, 24], OffsetMax=[344, 184], Color=[0, 0, 0, 0.35], Spacing=8, Padding=[12, 12, 12, 12])
	w(3, "Text", "Title", 2, OffsetMax=[296, 36], Text="WIWA2 DEMO HALL", Font=font_ref, FontSize=30, Outline=0.15, OutlineColor=[0.05, 0.1, 0.3, 1], Shadow=True)
	w(4, "Text", "Score", 2, OffsetMax=[296, 30], Text="Score 0", Font=font_ref, Color=[1, 0.85, 0.3, 1])
	w(5, "Bar", "Health", 2, OffsetMax=[296, 22], Value=0.75, Color=[0.9, 0.25, 0.25, 1])
	w(6, "VStack", "Controls", AnchorMin=[0, 1], AnchorMax=[0, 1], OffsetMin=[24, -148], OffsetMax=[344, -24], Color=[0, 0, 0, 0.35], Spacing=10, Padding=[12, 12, 12, 12])
	w(7, "Button", "ToggleLight", 6, OffsetMax=[296, 44], Color=[0.2, 0.35, 0.6, 1], HoverColor=[0.3, 0.5, 0.85, 1], PressedColor=[0.15, 0.25, 0.45, 1])
	w(8, "Text", "Label", 7, AnchorMax=[1, 1], OffsetMin=[0, 9], OffsetMax=[0, 0], Text="Light: on", Font=font_ref, FontSize=22, Align="Center")
	w(9, "Slider", "LightIntensity", 6, OffsetMax=[296, 24], Color=[0.95, 0.6, 0.1, 1], Value=2, MaxValue=20, Step=0.5)
	w(10, "VStack", "BeaconTag", Pivot=[0.5, 1], OffsetMax=[150, 52], Color=[0, 0, 0, 0.5], Spacing=4, Padding=[6, 4, 6, 4], WorldEntity=beacon_id,
		WorldOffset=[0, 1.2, 0], WorldScaleWithDistance=True, WorldRefDistance=14, WorldMinScale=0.4, WorldMaxScale=1.5)
	w(11, "Text", "BeaconName", 10, OffsetMax=[138, 22], Text="Beacon", Font=font_ref, FontSize=18, Align="Center")
	w(12, "Bar", "BeaconBar", 10, OffsetMax=[138, 14], Value=0.3, Color=[0.2, 0.9, 1, 1], BackgroundColor=[0.9, 0.2, 0.9, 0.8])
	w(13, "VStack", "InputInfo", AnchorMin=[1, 0], AnchorMax=[1, 0], OffsetMin=[-344, 24], OffsetMax=[-24, 214], Color=[0, 0, 0, 0.35], Spacing=8, Padding=[12, 12, 12, 12])
	w(14, "Text", "Device", 13, OffsetMax=[296, 26], Text="Device: keyboard_mouse", Font=font_ref, FontSize=20)
	w(15, "Text", "Move", 13, OffsetMax=[296, 26], Text="Move: 0.00, 0.00", Font=font_ref, FontSize=20)
	w(16, "Text", "JumpKey", 13, OffsetMax=[296, 26], Text="Jump: Key.Space", Font=font_ref, FontSize=20, Color=[1, 0.85, 0.3, 1])
	w(17, "Button", "RebindJump", 13, OffsetMax=[296, 44], Color=[0.55, 0.2, 0.5, 1], HoverColor=[0.75, 0.3, 0.7, 1], PressedColor=[0.4, 0.15, 0.35, 1])
	w(18, "Text", "Label", 17, AnchorMax=[1, 1], OffsetMin=[0, 9], OffsetMax=[0, 0], Text="Rebind Jump", Font=font_ref, FontSize=22, Align="Center")
	w(19, "Text", "Hint", AnchorMin=[0.5, 1], AnchorMax=[0.5, 1], OffsetMin=[-320, -52], OffsetMax=[320, -16], Text="Keys 1-7: jump between the stations",
		Font=font_ref, FontSize=24, Align="Center", Color=[0.85, 0.95, 1, 1], Outline=0.2, OutlineColor=[0, 0, 0, 1])
	next_id = 20
	for text, entity_id in labels:
		w(next_id, "Text", "Label %d" % (next_id - 19), Pivot=[0.5, 1], OffsetMax=[360, 36], Text=text, Font=font_ref, FontSize=28, Align="Center",
			Color=[0.9, 0.95, 1, 1], Outline=0.25, OutlineColor=[0, 0, 0, 1], WorldEntity=entity_id, WorldOffset=[0, 0, 0],
			WorldScaleWithDistance=True, WorldRefDistance=18, WorldMinScale=0.5, WorldMaxScale=2.0)
		next_id += 1
	# a ViewImage: the wizard portrait that the "Wizard portrait" ViewHost of the scene draws (views.md), at the top right
	w(next_id, "ViewImage", "WizardPortrait", AnchorMin=[1, 1], AnchorMax=[1, 1], OffsetMin=[-264, -264], OffsetMax=[-24, -24], ViewName="wizard_portrait")
	next_id += 1
	rel = "ui/demo_hud.wuipanel"
	write_json(rel, {"version": 1, "referenceResolution": [1920, 1080], "match": 0.5, "nextId": next_id, "widgets": widgets})
	return write_meta(rel, "UiPanelImporter", 1)


def build_input():
	def button(name, *controls):
		return {"name": name, "type": "Button", "bindings": [{"control": c} for c in controls]}
	gameplay = {"name": "Gameplay", "consume": False, "actions": [
		{"name": "Move", "type": "Axis2D", "bindings": [{"up": "Key.W", "down": "Key.S", "left": "Key.A", "right": "Key.D"}, {"x": "Pad.LeftX", "y": "Pad.LeftY", "deadzone": 0.2}]},
		{"name": "Look", "type": "Axis2D", "bindings": [{"x": "Mouse.DeltaX", "y": "Mouse.DeltaY", "scale": 0.1}, {"x": "Pad.RightX", "y": "Pad.RightY", "deadzone": 0.2}]},
		button("Jump", "Key.Space", "Pad.A"), button("Fire", "Mouse.Left", "Pad.RightTrigger")]}
	nav = {"name": "UI", "consume": False, "actions": [
		{"name": "NavUp", "type": "Button", "bindings": [{"control": "Key.Up"}, {"control": "Pad.DPadUp"}, {"control": "Pad.LeftY"}]},
		{"name": "NavDown", "type": "Button", "bindings": [{"control": "Key.Down"}, {"control": "Pad.DPadDown"}, {"control": "Pad.LeftY", "invert": True}]},
		{"name": "NavLeft", "type": "Button", "bindings": [{"control": "Key.Left"}, {"control": "Pad.DPadLeft"}, {"control": "Pad.LeftX", "invert": True}]},
		{"name": "NavRight", "type": "Button", "bindings": [{"control": "Key.Right"}, {"control": "Pad.DPadRight"}, {"control": "Pad.LeftX"}]},
		button("Submit", "Key.Enter", "Pad.A"), button("Cancel", "Key.Escape", "Pad.B")]}
	rel = "input/demo.winput"
	write_json(rel, {"version": 1, "maps": [gameplay, nav]})
	return write_meta(rel, "InputMapImporter", 1)


# ------------------------------------------------------------------------------------------------------------------ scene

def qaxis(axis, degrees):
	h = math.radians(degrees) / 2
	s = math.sin(h)
	return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h)]


def qmul(a, b):
	ax, ay, az, aw = a
	bx, by, bz, bw = b
	return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def euler(x=0, y=0, z=0):
	return qmul(qaxis((0, 1, 0), y), qmul(qaxis((1, 0, 0), x), qaxis((0, 0, 1), z)))


IDENTITY = [0, 0, 0, 1]


class Scene:
	def __init__(self):
		self.entities = []
		self.ids = {}

	def add(self, name, parent=None, pos=(0, 0, 0), rot=None, scale=(1, 1, 1), **components):
		assert name not in self.ids, "duplicate entity name " + name
		eid = guid("entity:" + name)
		self.ids[name] = eid
		comps = {"Name": {"Value": name}, "Transform": {"Position": list(pos), "Rotation": list(rot or IDENTITY), "Scale": list(scale)}}
		comps.update(components)
		entity = {"id": eid, "components": comps}
		if parent:
			entity["parent"] = self.ids[parent]
		self.entities.append(entity)
		return eid


BOX = {"Shape": "Box", "Size": [0.5, 0.5, 0.5], "Offset": [0, 0, 0]}
BALL = {"Shape": "Sphere", "Size": [0.5, 0.5, 0.5], "Offset": [0, 0, 0]}


def build_scene(g):
	"""g: dict of guids: shapes, mat (name -> guid), scripts (name -> guid), ui, models (name -> (guid, clips)), lamp, bouncy, env, ..."""
	s = Scene()

	def mesh(shape, material):
		return {"MeshRenderer": {"Mesh": ref(g["shapes"], MESH[shape]), "Materials": [ref(g["mat"][material])]}}

	def script(name, **props):
		return {"script:" + g["scripts"][name]: props}

	def merge(*parts):
		out = {}
		for part in parts:
			out.update(part)
		return out

	def light(kind, color, intensity, rng=10, inner=20, outer=30, shadows=False):
		return {"Light": {"Kind": kind, "Color": list(color), "Intensity": intensity, "Range": rng, "InnerAngle": inner, "OuterAngle": outer, "CastsShadows": shadows}}

	def body(motion="Dynamic", **extra):
		return {"RigidBody": dict({"Motion": motion}, **extra)}

	# --- the camera, the sun, the HUD -----------------------------------------------------------------------
	s.add("Camera", pos=(0, 4.5, 17), rot=qaxis((1, 0, 0), -8),
		Camera={"Projection": "Perspective", "FovY": 55, "Near": 0.1, "Far": 400, "OrthoSize": 10, "Primary": True}, **script("camera_rig"))
	s.add("Audio listener", "Camera", AudioListener={"Active": True})
	s.add("Audio demo", pos=(-3, 1.5, 3), AudioEmitter={"Event": "Amb_Hall_loop", "PlayOnStart": True, "AttenuationScale": 4.0}, **script("audio_demo", event="Beacon_Blip", every=4))
	s.add("Sun", pos=(0, 20, 0), rot=euler(-45, 35, 0), **light("Directional", (1, 0.96, 0.9, 1), 3.5, shadows=True))
	s.add("Hud", UICanvas={"Panel": ref(g["ui"]), "SortOrder": 0, "Visible": True}, **script("demo_ui"))
	s.add("Demo light", pos=(-5, 3, 6), **light("Point", (0.6, 0.8, 1, 1), 2, rng=14))
	# a 3D widget of the HUD: the wizard turning in a view of its own, shown by the ViewImage "WizardPortrait" of the HUD
	s.add("Wizard portrait", ViewHost={"Name": "wizard_portrait", "Model": ref(g["models"]["Wizard"][0]), "Target": [0, 1, 0], "Distance": 5.5, "Yaw": 20, "Pitch": 8,
		"Fov": 35, "SpinSpeed": 40, "Width": 256, "Height": 256, "Profile": "Preview", "Transparent": True, "Continuous": True})
	s.add("Beacon", pos=(-3, 1.5, 3), scale=(0.3, 0.3, 0.3), **mesh("sphere", "hologram"))

	# --- ground: textured, with the physics ground under every station --------------------------------------
	s.add("World")
	s.add("Ground", "World", scale=(120, 1, 120), **mesh("plane_tiled", "ground"))
	s.add("Physics demo")
	s.add("Ground collider", "Physics demo", pos=(0, -0.5, 0), scale=(120, 1, 120), Collider=BOX)

	# --- station 1: lighting and post ------------------------------------------------------------------------
	s.add("Lighting station", "World")
	s.add("Podium", "Lighting station")
	s.add("Podium base", "Podium", pos=(0, 0.2, 0), scale=(5, 0.4, 5), **mesh("cylinder", "podium"))
	s.add("Gold sphere", "Podium", pos=(-1.4, 1.2, 0), scale=(1.6, 1.6, 1.6), **mesh("sphere", "gold"))
	s.add("Steel cylinder", "Podium", pos=(1.4, 1.2, 0), scale=(1.0, 1.6, 1.0), **mesh("cylinder", "steel"))
	s.add("Blue ring", "Podium", pos=(0, 3.6, 0), rot=qaxis((1, 0, 0), 90), scale=(2.4, 2.4, 2.4), **merge(mesh("torus", "ring_blue"), script("spinner", degrees_per_second=90)))
	s.add("Podium spot", "Podium", pos=(0, 7, 5), rot=qaxis((1, 0, 0), -53), **merge(light("Spot", (1, 0.85, 0.6, 1), 60, rng=18, inner=14, outer=24, shadows=True), script("light_pulse", amplitude=25, speed=6)))
	for i, (name, x, z) in enumerate((("L4", -6, -4), ("R4", 6, -4), ("L8", -6, -8), ("R8", 6, -8))):
		s.add("Column " + name, "Lighting station", pos=(x, 2.5, z), scale=(1, 5, 1), **merge(mesh("cube", "column"), {"Collider": BOX}))
	for i, (x, z, y) in enumerate(((3.2, -3, 0.4), (4.4, -3.6, 0.4), (3.8, -3.3, 1.2))):
		s.add("Crate " + "ABC"[i], "Lighting station", pos=(x, y, z), scale=(0.8, 0.8, 0.8), **merge(mesh("cube", "crate"), {"Collider": BOX}, body() if i else {}))
	neon = (("Neon cyan", "neon_cyan", (0.1, 0.9, 1, 1), (5.2, 1.6, 3.2), 0.0), ("Neon magenta", "neon_magenta", (1, 0.1, 0.8, 1), (-5.2, 1.6, 3.2), 2.1), ("Neon orange", "neon_orange", (1, 0.5, 0.05, 1), (0, 1.6, -5.5), 4.2))
	for name, material, color, position, phase in neon:
		s.add(name, "Lighting station", pos=position, scale=(0.8, 0.8, 0.8),
			**merge(mesh("sphere", material), light("Point", color, 8, rng=6), script("bobber", phase=phase), script("light_pulse", amplitude=3, speed=4)))
	for i, x in enumerate((-9, 9, 0)):
		z = -2 if x else -11
		s.add("Street lamp %d" % (i + 1), "Lighting station", pos=(x, 0, z), PrefabInstance={"Prefab": ref(g["lamp"])})
	add_nested_lamps(s, g)

	# --- station 2: materials and shaders --------------------------------------------------------------------
	s.add("Materials station", "World")
	s.add("Backdrop", "Materials station", pos=(0, 5, -28), scale=(40, 10, 1), **mesh("cube", "backdrop"))
	s.add("PBR grid", "Materials station")
	for r in range(5):
		for m in range(3):
			s.add("PBR r%d m%d" % (r, m), "PBR grid", pos=((r - 2) * 1.3, 0.7 + m * 1.3, -16), scale=(1.1, 1.1, 1.1), **mesh("sphere", "pbr_r%d_m%d" % (r, m)))
	specials = (("Carpaint sphere", "sphere", "carpaint", (-9, 0.9, -11), 1.5), ("Cloth sphere", "sphere", "cloth", (-6.5, 0.9, -11), 1.5),
		("Glass cube", "cube", "glass", (-3.5, 0.9, -11), 1.4), ("Red sphere behind glass", "sphere", "plastic_red", (-3.5, 0.7, -12.2), 1.0),
		("Double sided leaf", "plane", "double_sided", (3.5, 1.6, -11), 2.0), ("Hologram cylinder", "cylinder", "hologram", (6, 1.1, -11), 2.0),
		("Brick cube", "cube", "column", (9, 0.9, -11), 1.6))
	for name, shape, material, position, size in specials:
		rot = qaxis((1, 0, 0), 90) if name == "Double sided leaf" else None
		s.add(name, "Materials station", pos=position, rot=rot, scale=(size, size, size), **mesh(shape, material))
	s.add("Toon showcase", "Materials station")
	for name, shape, material, x, size in (("Toon sphere", "sphere", "toon_red", -9, 1.5), ("Toon ring", "torus", "toon_teal", -6.5, 1.8),
			("Toon cone", "cone", "toon_yellow", -4, 1.6), ("Toon puffy cube", "cube", "toon_puffy", -1.5, 1.4)):
		s.add(name, "Toon showcase", pos=(x - 3, 0.9, -6.5), rot=qaxis((1, 0, 0), 90) if shape == "torus" else None, scale=(size, size, size), **mesh(shape, material))
	s.add("Toon fill light", "Toon showcase", pos=(-8, 4, -4), **light("Point", (1, 0.9, 0.8, 1), 25, rng=12))
	s.add("Warm point light", "Materials station", pos=(3, 3, -8), **light("Point", (1, 0.6, 0.3, 1), 30, rng=12))

	# --- station 3: models and animation ---------------------------------------------------------------------
	s.add("Animation station", "World")
	# the warrior plays an animation graph (scripts/gen_demo_graph.py writes it; run that first): the Animator is given here so that the prefab expander does not add its own,
	# with the same skeleton reference the prefab's skinned node holds (the legacy local id 1 of the model, which the catalog redirects)
	s.add("Warrior (animated)", "Animation station", pos=(0, 0, 9), PrefabInstance={"Prefab": ref(g["models"]["Warrior"][0])},
		Animator={"Skeleton": ref(g["models"]["Warrior"][0], 1), "Graph": ref(guid("asset:models/characters/Warrior_animation/warrior.wgraph")), "PlayOnStart": False},
		AudioEmitter={"Event": "", "PlayOnStart": False, "AttenuationScale": 1.0},
		**merge(script("animation_graph_demo", period=0.4, attack_every=0.15), script("decal_footprints"), script("vfx_footsteps")))
	s.add("Warrior head socket", "Warrior (animated)", AnimationSocket={"Joint": "Head", "OffsetPosition": [0, 0.3, 0], "OffsetRotation": IDENTITY})
	s.add("Warrior head ornament", "Warrior head socket", scale=(0.18, 0.18, 0.18), **mesh("sphere", "gold"))
	lineup = (("Wizard", -2.5, 11), ("Ranger", 2.5, 11), ("Cleric", -5, 12), ("Yeti", 5, 12), ("Panda", -7.5, 12.5), ("YellowDragon", 7.5, 12.5))
	for name, x, z in lineup:
		guid_model, clips = g["models"][name]
		wanted = (CHARACTERS.get(name) or MONSTERS.get(name))
		s.add(name, "Animation station", pos=(x, 0, z), PrefabInstance={"Prefab": ref(guid_model)}, **script("clip_player", clip=pick_clip(clips, wanted)))

	# --- station 4: physics ----------------------------------------------------------------------------------
	s.add("Ramp", "Physics demo", pos=(10.2, 1.1, 1.5), rot=qaxis((0, 0, 1), -20), scale=(4, 0.3, 3), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Back wall", "Physics demo", pos=(14.6, 0.6, 1.5), scale=(0.3, 1.2, 3.4), **merge(mesh("cube", "plain"), {"Collider": BOX}))
	s.add("Side wall 0", "Physics demo", pos=(11.2, 0.6, 0), scale=(7, 1.2, 0.2), **merge(mesh("cube", "plain"), {"Collider": BOX}))
	s.add("Side wall 3", "Physics demo", pos=(11.2, 0.6, 3), scale=(7, 1.2, 0.2), **merge(mesh("cube", "plain"), {"Collider": BOX}))
	for i, (x, y, z) in enumerate(((13.6, 0.4, 1.0), (13.6, 0.4, 2.0), (13.6, 1.25, 1.5), (12.7, 0.4, 1.5))):
		s.add("Crate %d" % (i + 1), "Physics demo", pos=(x, y, z), scale=(0.8, 0.8, 0.8), **merge(mesh("cube", "crate"), {"Collider": BOX}, body(ReportContacts=(i == 0))))
	s.add("Hopper", "Physics demo", pos=(8.9, 3.2, 1.5), scale=(0.8, 0.8, 0.8), **merge(mesh("sphere", "plastic_red"), {"Collider": BALL}, body(), {"AudioEmitter": {"Event": "", "PlayOnStart": False, "AttenuationScale": 1.0}}, script("physics_hopper", interval=3, speed=6)))
	for i, (x, y, z) in enumerate(((8.6, 5, 1), (9.2, 7, 2), (9.6, 9, 1.5))):
		s.add("Bouncy ball %d" % (i + 1), "Physics demo", pos=(x, y, z), scale=(0.6, 0.6, 0.6),
			**merge(mesh("sphere", "gold"), {"Collider": dict(BALL, Material=ref(g["bouncy"]))}, body()))
	s.add("Input demo", "Physics demo", pos=(4, 0.4, 6), scale=(0.8, 0.8, 0.8), **merge(mesh("sphere", "carpaint"), {"Collider": BALL}, body(MassOverride=1), script("input_demo", speed=4, jump_speed=6)))
	s.add("Trigger zone", "Physics demo", pos=(6.5, 1, 1.5), scale=(2, 2, 3), **merge({"Collider": dict(BOX, IsTrigger=True)}, light("Point", (0.3, 1, 0.5, 1), 0, rng=7), script("trigger_zone", glow=25)))

	# --- station 5: joints ------------------------------------------------------------------------------------
	s.add("Joints demo")
	s.add("Door post", "Joints demo", pos=(-14.5, 1.2, 8), scale=(0.2, 2.4, 0.2), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Door", "Joints demo", pos=(-13.65, 1.1, 8), scale=(1.5, 2.0, 0.12), **merge(mesh("cube", "crate"), {"Collider": BOX}, body(),
		{"Joint": {"Type": "Hinge", "Connected": s.ids["Door post"], "AnchorA": [-0.75, 0, 0], "AnchorB": [0.1, -0.1, 0], "Axis": [0, 1, 0], "LimitMin": -100, "LimitMax": 100}},
		script("joint_motor_swing", speed=1.2, period=3, max_force=3000)))
	s.add("Slider rail", "Joints demo", pos=(-12, 1.0, 5.5), scale=(5, 0.1, 0.1), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Slider cart", "Joints demo", pos=(-12, 1.5, 5.5), scale=(0.6, 0.6, 0.6), **merge(mesh("cube", "carpaint"), {"Collider": BOX}, body(),
		{"Joint": {"Type": "Slider", "Connected": s.ids["Slider rail"], "AnchorA": [0, 0, 0], "AnchorB": [0, 0.5, 0], "Axis": [1, 0, 0], "NormalAxis": [0, 1, 0], "LimitMin": -2, "LimitMax": 2}},
		script("joint_motor_swing", speed=1.5, period=2.5, max_force=5000)))
	s.add("Pendulum beam", "Joints demo", pos=(-10, 5.2, 8), scale=(1.2, 0.2, 0.2), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Pendulum bob", "Joints demo", pos=(-9, 3.2, 8), scale=(0.8, 0.8, 0.8), **merge(mesh("sphere", "gold"), {"Collider": BALL}, body(),
		{"Joint": {"Type": "Distance", "Connected": s.ids["Pendulum beam"], "AnchorA": [0, 0, 0], "AnchorB": [0, 0, 0], "LimitMin": 2, "LimitMax": 2, "SpringFrequency": 1.2, "SpringDamping": 0.15}}))
	s.add("Bridge post L", "Joints demo", pos=(-15, 3, 10.5), scale=(0.3, 0.3, 0.6), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	previous = "Bridge post L"
	for i in range(5):
		anchor_b = [0.15, 0, 0] if i == 0 else [0.5, 0, 0]
		s.add("Bridge link %d" % i, "Joints demo", pos=(-14.35 + i, 3, 10.5), scale=(1.0, 0.12, 0.5), **merge(mesh("cube", "crate"), {"Collider": BOX}, body(),
			{"Joint": {"Type": "Hinge", "Connected": s.ids[previous], "AnchorA": [-0.5, 0, 0], "AnchorB": anchor_b, "Axis": [0, 0, 1]}}))
		previous = "Bridge link %d" % i
	s.add("Bridge post R", "Joints demo", pos=(-9.7, 3, 10.5), scale=(0.3, 0.3, 0.6), **merge(mesh("cube", "steel"), {"Collider": BOX},
		{"Joint": {"Type": "Hinge", "Connected": s.ids[previous], "AnchorA": [-0.15, 0, 0], "AnchorB": [0.5, 0, 0], "Axis": [0, 0, 1]}}))
	s.add("Weak beam", "Joints demo", pos=(-8.5, 5.2, 6.5), scale=(1.0, 0.2, 0.2), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Weak box", "Joints demo", pos=(-8.5, 4.8, 6.5), scale=(0.5, 0.5, 0.5), **merge(mesh("cube", "plastic_red"), {"Collider": BOX}, body(),
		{"Joint": {"Type": "Fixed", "Connected": s.ids["Weak beam"], "AnchorA": [0, 0.25, 0], "AnchorB": [0, -0.15, 0], "BreakForce": 2500}}, script("joint_breaker")))
	s.add("Wrecker", "Joints demo", pos=(-5.8, 4.7, 6.5), scale=(0.7, 0.7, 0.7), **merge(mesh("sphere", "steel"), {"Collider": BALL}, body(), script("joint_wrecker")))

	# --- station 6: character controller ---------------------------------------------------------------------
	s.add("Character demo")
	cc = {"CharacterController": {"Offset": [0, 0.9, 0]}}
	s.add("Hero", "Character demo", pos=(19, 0, 1.5), **merge(cc, script("character_demo", speed=4, jump_speed=6)))
	s.add("Hero body", "Hero", pos=(0, 0.9, 0), scale=(0.8, 1.8, 0.8), **mesh("cylinder", "toon_teal"))
	s.add("Patrol", "Character demo", pos=(19, 0, 5.5), **merge(cc, script("character_demo", speed=2, jump_speed=5, autopilot=True, leg=4)))
	s.add("Patrol body", "Patrol", pos=(0, 0.9, 0), scale=(0.8, 1.8, 0.8), **mesh("cylinder", "toon_red"))
	s.add("Low step", "Character demo", pos=(21.5, 0.1, 3.5), scale=(1, 0.2, 7.4), **merge(mesh("cube", "plain"), {"Collider": BOX}))
	s.add("Tall step", "Character demo", pos=(23.2, 0.3, 1.2), scale=(1.5, 0.6, 2.4), **merge(mesh("cube", "plain"), {"Collider": BOX}))
	s.add("Walkable ramp", "Character demo", pos=(26.648, 1.413, 5.5), rot=qaxis((0, 0, 1), 30), scale=(6, 0.2, 5), **merge(mesh("cube", "steel"), {"Collider": BOX}))
	s.add("Steep ramp", "Character demo", pos=(25.0866, 1.682, -1.2), rot=qaxis((0, 0, 1), 60), scale=(4, 0.2, 2), **merge(mesh("cube", "plastic_red"), {"Collider": BOX}))
	s.add("Sliding platform", "Character demo", pos=(28, 0.15, -4.5), scale=(2.4, 0.3, 2.4), **merge(mesh("cube", "gold"), {"Collider": BOX}, body("Kinematic"), script("slide_platform", amplitude=2.5, speed=0.6)))

	# --- station 7: decals -----------------------------------------------------------------------------------
	add_decals(s, g)

	# --- station 8: particle effects -------------------------------------------------------------------------
	add_vfx(s, g)
	add_navigation(s, g)
	add_ai(s, g)

	# --- station labels (world-anchored text in the HUD) -----------------------------------------------------
	labels = []
	for text, position in (("1  Lighting and post", (0, 6.5, 0)), ("2  Materials and shaders", (0, 4.5, -16)), ("3  Models and animation", (0, 3.6, 9)),
			("4  Physics", (11, 3, 1.5)), ("5  Joints", (-11, 6.5, 8)), ("6  Character controller", (24, 4, 1)), ("7  Scripts, input and UI", (2, 2.6, 6))):
		label_id = s.add("Label " + text[0], "World", pos=position)
		labels.append((text, label_id))

	return s, labels


def scene_settings(g):
	return {"source": "file", "environment": ref(g["env"]), "background": "sky", "ambient": 0.9, "rotation": 120,
		"scripts": [ref(g["scripts"]["scene_overlay"])],
		"exposure": {"auto": True, "compensation": -0.3, "minEv": -3, "maxEv": 3, "adaptSpeed": 2},
		"bloom": {"enabled": True, "threshold": 1, "knee": 0.5, "intensity": 0.07},
		"tonemap": {"operator": "aces", "contrast": 1.06, "saturation": 1.12, "temperature": 0.05, "vignette": 0.25},
		"ao": {"enabled": True, "radius": 1.2, "power": 1.6},
		"motionBlur": {"enabled": True}}


HEAD_NODE = node_id("Street lamp/Head")  # the lamp head, in the street lamp prefab
LEFT_LAMP_NODE = node_id("Lamp pair/Left lamp")  # the node of the lamp pair prefab that holds the left lamp


def add_nested_lamps(s, g):
	"""A lamp pair: a prefab that holds two instances of the street lamp (the right one has a blue head, an override stored in the pair prefab). The
	instance in the scene brightens the head of the left lamp, an override of a node two prefabs deep (its id is composed from the node of the left lamp)."""
	eid = s.add("Lamp pair", "Lighting station", pos=(6, 0, -8), PrefabInstance={"Prefab": ref(g["lamp_pair"])})
	s.entities[-1]["overrides"] = [set_field_override(compose_node_id(LEFT_LAMP_NODE, HEAD_NODE), "Light", "Intensity", 40.0)]
	return eid


def build_lamp_pair(g):
	"""lamp_pair.wprefab: a prefab that holds two street lamps (a nested prefab instance each)."""
	root = guid("lamp_pair:root")
	identity = {"Rotation": IDENTITY, "Scale": [1, 1, 1]}
	blue = set_field_override(HEAD_NODE, "Light", "Color", [0.3, 0.5, 1, 1])
	ents = [
		{"id": root, "components": {"Name": {"Value": "Lamp pair"}, "Transform": dict(identity, Position=[0, 0, 0])}},
		{"id": guid("lamp_pair:left"), "parent": root, "components": {"Name": {"Value": "Left lamp"}, "Transform": dict(identity, Position=[-1.6, 0, 0]),
			"PrefabInstance": {"Prefab": ref(g["lamp"])}}},
		{"id": guid("lamp_pair:right"), "parent": root, "components": {"Name": {"Value": "Right lamp"}, "Transform": dict(identity, Position=[1.6, 0, 0]),
			"PrefabInstance": {"Prefab": ref(g["lamp"])}}, "overrides": [blue]},
	]
	rel = "prefabs/lamp_pair.wprefab"
	write_json(rel, {"name": "Lamp pair", "entities": ents})
	return write_meta(rel, "PrefabImporter", 2)


def build_lamp(g):
	"""street_lamp.wprefab: a pole, an emissive head and a point light, instanced three times in the lighting station."""
	root = guid("lamp:root")
	ents = [
		{"id": root, "components": {"Name": {"Value": "Street lamp"}, "Transform": {"Position": [0, 0, 0], "Rotation": IDENTITY, "Scale": [1, 1, 1]}}},
		{"id": guid("lamp:pole"), "parent": root, "components": {"Name": {"Value": "Pole"}, "Transform": {"Position": [0, 1.6, 0], "Rotation": IDENTITY, "Scale": [0.12, 3.2, 0.12]},
			"MeshRenderer": {"Mesh": ref(g["shapes"], MESH["cylinder"]), "Materials": [ref(g["mat"]["steel"])]}}},
		{"id": guid("lamp:head"), "parent": root, "components": {"Name": {"Value": "Head"}, "Transform": {"Position": [0, 3.4, 0], "Rotation": IDENTITY, "Scale": [0.45, 0.45, 0.45]},
			"MeshRenderer": {"Mesh": ref(g["shapes"], MESH["sphere"]), "Materials": [ref(g["mat"]["neon_orange"])]},
			"Light": {"Kind": "Point", "Color": [1, 0.7, 0.35, 1], "Intensity": 18, "Range": 9, "InnerAngle": 20, "OuterAngle": 30, "CastsShadows": False}}},
	]
	rel = "prefabs/street_lamp.wprefab"
	write_json(rel, {"name": "Street lamp", "entities": ents})
	return write_meta(rel, "PrefabImporter", 1)


def main():
	if "--ai-only" in sys.argv:
		# adds only the behaviour tree demo (the guard's tree and the two scripts it uses, the guard, the thief and the noise maker) to the scene that is
		# already written, without downloading anything and without touching the .meta files of anything else
		for name in ("heard_check.lua", "noise_maker.lua"):
			write("scripts/" + name, SCRIPTS[name])
		scripts = {name[:-4]: write_meta("scripts/" + name, "ScriptImporter", 1) for name in ("heard_check.lua", "noise_maker.lua")}
		write_ai_assets()
		g = {"shapes": guid("asset:models/shapes/shapes.glb"), "scripts": scripts,
			"mat": {name: guid("asset:materials/%s.wmat" % name) for name in ("toon_red", "toon_yellow", "marker")}}
		patch_scene_with_ai(g)
		print("added the behaviour tree demo to the demo scene")
		return

	if "--nav-only" in sys.argv:
		# adds only the navigation station to the scene that is already written (its script, its navmesh asset record and nav.wsettings too), without
		# downloading anything and without touching the .meta files of anything else: the navmesh itself is baked by the editor afterwards (--nav-bake)
		write("scripts/nav_patrol.lua", SCRIPTS["nav_patrol.lua"])
		g = {"shapes": guid("asset:models/shapes/shapes.glb"), "mat": {"crate": guid("asset:materials/crate.wmat"), "marker": guid("asset:materials/marker.wmat"),
			"neon_cyan": guid("asset:materials/neon_cyan.wmat"), "neon_magenta": guid("asset:materials/neon_magenta.wmat"), "neon_orange": guid("asset:materials/neon_orange.wmat")},
			"scripts": {"nav_patrol": write_meta("scripts/nav_patrol.lua", "ScriptImporter", 1)}}
		write_nav_settings()
		patch_scene_with_nav(g)
		print("added the navigation station to the demo scene")
		return

	if "--nested-only" in sys.argv:
		# adds only the lamp pair to the scene that is already written (and its prefab), without downloading anything or touching other .meta files
		g = {"lamp": guid("asset:prefabs/street_lamp.wprefab")}
		g["lamp_pair"] = build_lamp_pair(g)
		patch_scene_with_nested(g)
		print("added the lamp pair to the demo scene")
		return

	if "--vfx-only" in sys.argv:
		# adds only the particle effects to the scene that is already written, without downloading anything
		g = {"vfx": vfx_effects(), "shapes": guid("asset:models/shapes/shapes.glb"), "mat": {"crate": guid("asset:materials/crate.wmat")}}
		g["scripts"] = build_scripts()
		patch_scene_with_vfx(g)
		print("added the particle effects to the demo scene")
		return

	if "--decals-only" in sys.argv:
		# adds only the decals to the scene that is already written, without downloading anything
		g = {"decals": decal_materials()}
		g["scripts"] = build_scripts()
		patch_scene_with_decals(g)
		print("added the decals to the demo scene")
		return

	g = {}
	write("models/shapes/shapes.glb", build_glb(), binary=True)
	g["shapes"] = write_meta("models/shapes/shapes.glb", "ModelImporter", 7, {"generatePhysicsShapes": False},
		{"mesh/%d" % i: i + 1 for i in range(len(SHAPES))})

	g["models"] = import_models()
	import_textures()
	g["mat"] = build_materials()
	g["decals"] = decal_materials()
	g["vfx"] = vfx_effects()
	g["scripts"] = build_scripts()

	write("environments/sky.hdr", download(HDRI_URL, "sky.hdr").read_bytes(), binary=True)
	g["env"] = write_meta("environments/sky.hdr", "EnvironmentImporter", 2)
	write("environments/LICENSE.txt", "kloppenheim_06_puresky_1k.hdr from Poly Haven (polyhaven.com), CC0 1.0.\n")

	write("fonts/Lato-Regular.ttf", download(FONT_URL, "Lato-Regular.ttf").read_bytes(), binary=True)
	write("fonts/OFL-Lato.txt", download(FONT_LICENSE_URL, "OFL-Lato.txt").read_bytes(), binary=True)
	font = write_meta("fonts/Lato-Regular.ttf", "FontImporter", 1, {"pixelSize": 48, "sdfRange": 8, "charset": "latin1"})

	import_audio()

	write_json("physics/bouncy.wphysmat", {"friction": 0.4, "restitution": 0.75, "restitutionCombine": "Max"})
	g["bouncy"] = write_meta("physics/bouncy.wphysmat", "PhysicsMaterialImporter", 1)

	input_map = build_input()
	g["lamp"] = build_lamp(g)
	g["lamp_pair"] = build_lamp_pair(g)

	scene, labels = build_scene(g | {"ui": "0" * 32})  # the HUD guid does not depend on its content
	g["ui"] = build_ui(font, scene.ids["Beacon"], labels)
	scene, labels = build_scene(g)

	write_json("scenes/demo.wscene", {"name": "Demo hall", "settings": scene_settings(g), "entities": scene.entities})
	write_meta("scenes/demo.wscene", "SceneImporter", 5, fixed_guid=SCENE_GUID)

	write_nav_settings()
	write_ai_assets()
	# the project's default input map is the demo's
	(ROOT / "editor" / "input.wsettings").write_text(json.dumps({"defaultMap": input_map}, indent=2) + "\n", encoding="utf-8", newline="\n")
	print("wrote %d entities, %d materials, %d scripts" % (len(scene.entities), len(g["mat"]), len(g["scripts"])))


if __name__ == "__main__":
	main()
