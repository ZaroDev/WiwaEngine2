#!/usr/bin/env python3
"""Writes the demo hall: editor/assets/** (scene, scripts, materials, UI, input, models, textures) and their .meta files.

    python scripts/gen_demo_scene.py [cache_dir]

Needs Pillow (to pack the ORM textures). Downloads the free assets it uses into cache_dir (default: .demo_cache, git-ignored) once.
Every asset gets a GUID derived from its path, so running this again changes nothing the scene points at. The .meta files are
written here (they are git-ignored, so `git add -f` them) because the scene refers to the GUIDs and the mesh local ids.

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
CACHE = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".demo_cache"

SCENE_GUID = "2a6ca957fed3526d8d53224e96ff22f1"  # editor/build_settings.json names it


def guid(key):
	return hashlib.md5(key.encode()).hexdigest()


def ref(g, local=0):
	return {"guid": g, "localId": local}


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


def download(url, name):
	CACHE.mkdir(parents=True, exist_ok=True)
	path = CACHE / name
	if not path.exists():
		print("downloading", url)
		request = urllib.request.Request(url, headers={"User-Agent": "wiwa2-demo-generator"})
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
  panel:find("InputInfo/RebindJump"):on("click", function(me) me:start_rebind() end)
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


def build_scripts():
	ids = {}
	for rel, text in SCRIPTS.items():
		path = "scripts/" + rel
		write(path, text)
		ids[rel.split("/")[-1][:-4]] = write_meta(path, "ScriptImporter", 1)
	return ids


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
	s.add("Warrior (animated)", "Animation station", pos=(0, 0, 9), PrefabInstance={"Prefab": ref(g["models"]["Warrior"][0])}, **script("animation_demo", period=0.5, fade=0.1))
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
	s.add("Hopper", "Physics demo", pos=(8.9, 3.2, 1.5), scale=(0.8, 0.8, 0.8), **merge(mesh("sphere", "plastic_red"), {"Collider": BALL}, body(), script("physics_hopper", interval=3, speed=6)))
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
	g = {}
	write("models/shapes/shapes.glb", build_glb(), binary=True)
	g["shapes"] = write_meta("models/shapes/shapes.glb", "ModelImporter", 7, {"generatePhysicsShapes": False},
		{"mesh/%d" % i: i + 1 for i in range(len(SHAPES))})

	g["models"] = import_models()
	import_textures()
	g["mat"] = build_materials()
	g["scripts"] = build_scripts()

	write("environments/sky.hdr", download(HDRI_URL, "sky.hdr").read_bytes(), binary=True)
	g["env"] = write_meta("environments/sky.hdr", "EnvironmentImporter", 2)
	write("environments/LICENSE.txt", "kloppenheim_06_puresky_1k.hdr from Poly Haven (polyhaven.com), CC0 1.0.\n")

	write("fonts/Lato-Regular.ttf", download(FONT_URL, "Lato-Regular.ttf").read_bytes(), binary=True)
	write("fonts/OFL-Lato.txt", download(FONT_LICENSE_URL, "OFL-Lato.txt").read_bytes(), binary=True)
	font = write_meta("fonts/Lato-Regular.ttf", "FontImporter", 1, {"pixelSize": 48, "sdfRange": 8, "charset": "latin1"})

	write_json("physics/bouncy.wphysmat", {"friction": 0.4, "restitution": 0.75, "restitutionCombine": "Max"})
	g["bouncy"] = write_meta("physics/bouncy.wphysmat", "PhysicsMaterialImporter", 1)

	input_map = build_input()
	g["lamp"] = build_lamp(g)

	scene, labels = build_scene(g | {"ui": "0" * 32})  # the HUD guid does not depend on its content
	g["ui"] = build_ui(font, scene.ids["Beacon"], labels)
	scene, labels = build_scene(g)

	write_json("scenes/demo.wscene", {"name": "Demo hall", "settings": scene_settings(g), "entities": scene.entities})
	write_meta("scenes/demo.wscene", "SceneImporter", 5, fixed_guid=SCENE_GUID)

	# the project's default input map is the demo's
	(ROOT / "editor" / "input.wsettings").write_text(json.dumps({"defaultMap": input_map}, indent=2) + "\n", encoding="utf-8", newline="\n")
	print("wrote %d entities, %d materials, %d scripts" % (len(scene.entities), len(g["mat"]), len(g["scripts"])))


if __name__ == "__main__":
	main()
