#!/usr/bin/env python3
"""Writes the demo game "Warrior's Crossing": editor/assets/game/** (two scenes, scripts, HUD and menus panel, input map) and their .meta files.

    python scripts/gen_demo_scene.py     # once: the models, materials, textures, sky and font the game reuses
    python scripts/gen_game.py

The game reuses the demo's assets by GUID (the GUIDs derive from the asset paths, see gen_demo_scene.guid), so gen_demo_scene.py must have run
before, and nothing here downloads. Lua sources live in scripts/game_scripts/ and are copied. The game scene, the entity names and the HUD
widget paths are used by the game smoke test (editor/src/play/game_smoke.cpp) and by scripts/game_scripts/selftest.lua: change them together.

It also writes editor/input.wsettings (the project's default input map becomes the game's, a superset of the demo's) and
editor/build_settings.json (the game's level is the default scene; the demo scene stays in the build).
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_demo_scene as demo  # noqa: E402

guid, ref, write, write_json, write_meta, Scene = demo.guid, demo.ref, demo.write, demo.write_json, demo.write_meta, demo.Scene
ROOT, MESH, IDENTITY = demo.ROOT, demo.MESH, demo.IDENTITY
LUA_SRC = ROOT / "scripts" / "game_scripts"
BOX = demo.BOX


# ------------------------------------------------------------------------------------------------------------ demo assets

def asset(rel):
	return guid("asset:" + rel)


def material(name):
	return ref(asset("materials/%s.wmat" % name))


def clip_for(model, *words):
	"""The first clip of the model whose name ends with one of the words (case-insensitive), or ''."""
	folder = "characters" if model in demo.CHARACTERS else "monsters"
	clips = demo.fbx_clips((demo.ASSETS / "models" / folder / (model + ".fbx")).read_bytes())
	for word in words:
		for clip in clips:
			if clip.lower().endswith("|" + word.lower()) or clip.lower() == word.lower():
				return clip
	return ""


def model_guid(model):
	folder = "characters" if model in demo.CHARACTERS else "monsters"
	return asset("models/%s/%s.fbx" % (folder, model))


# ------------------------------------------------------------------------------------------------------------------ scripts

def build_scripts():
	ids = {}
	for source in sorted(LUA_SRC.rglob("*.lua")):
		rel = "game/scripts/" + source.relative_to(LUA_SRC).as_posix()
		write(rel, source.read_text(encoding="utf-8").replace("\r\n", "\n"))
		ids[source.stem] = write_meta(rel, "ScriptImporter", 1)
	return ids


# ------------------------------------------------------------------------------------------------------------------ input

def build_input():
	def button(name, *controls):
		return {"name": name, "type": "Button", "bindings": [{"control": c} for c in controls]}
	gameplay = {"name": "Gameplay", "consume": False, "actions": [
		{"name": "Move", "type": "Axis2D", "bindings": [{"up": "Key.W", "down": "Key.S", "left": "Key.A", "right": "Key.D"}, {"x": "Pad.LeftX", "y": "Pad.LeftY", "deadzone": 0.2}]},
		{"name": "Look", "type": "Axis2D", "bindings": [{"x": "Mouse.DeltaX", "y": "Mouse.DeltaY", "scale": 0.1}, {"x": "Pad.RightX", "y": "Pad.RightY", "deadzone": 0.2, "scale": 2.5}]},
		button("Jump", "Key.Space", "Pad.A"), button("Fire", "Mouse.Left", "Pad.RightTrigger"),
		button("Melee", "Key.E", "Mouse.Right", "Pad.X"), button("Pause", "Key.P", "Pad.Start")]}
	nav = {"name": "UI", "consume": False, "actions": [
		{"name": "NavUp", "type": "Button", "bindings": [{"control": "Key.Up"}, {"control": "Pad.DPadUp"}, {"control": "Pad.LeftY"}]},
		{"name": "NavDown", "type": "Button", "bindings": [{"control": "Key.Down"}, {"control": "Pad.DPadDown"}, {"control": "Pad.LeftY", "invert": True}]},
		{"name": "NavLeft", "type": "Button", "bindings": [{"control": "Key.Left"}, {"control": "Pad.DPadLeft"}, {"control": "Pad.LeftX", "invert": True}]},
		{"name": "NavRight", "type": "Button", "bindings": [{"control": "Key.Right"}, {"control": "Pad.DPadRight"}, {"control": "Pad.LeftX"}]},
		button("Submit", "Key.Enter", "Pad.A"), button("Cancel", "Key.Escape", "Pad.B")]}
	rel = "game/input/game.winput"
	write_json(rel, {"version": 1, "maps": [gameplay, nav]})
	return write_meta(rel, "InputMapImporter", 1)


# ------------------------------------------------------------------------------------------------------------------ UI

class UiKit:
	"""Palette, type and the building blocks of the game's UI (docs/superpowers/specs/2026-11-09-main-menu-design.md): brass on dusk plum, Almendra SC for
	the title and headings, Lato for everything read or clicked. One kit builds the three panels (main menu, HUD with pause/dead/won, options)."""

	CLEAR = [0, 0, 0, 0]  # a stack or a panel is opaque white unless it is told otherwise
	PLUM = [0.09, 0.07, 0.125]
	BONE = [0.93, 0.89, 0.81, 1]
	BRASS = [0.85, 0.66, 0.34, 1]
	EMBER = [0.77, 0.29, 0.18, 1]
	HAZE = [0.66, 0.60, 0.74, 1]
	FULL = dict(AnchorMin=[0, 0], AnchorMax=[1, 1], OffsetMin=[0, 0], OffsetMax=[0, 0])

	def __init__(self, font, display_font):
		self.widgets = []
		self.next_id = 2
		self.f, self.fd = ref(font), ref(display_font)

	def w(self, kind, name, parent=None, **fields):
		wid = self.next_id
		self.next_id += 1
		d = {"id": wid, "kind": kind, "Name": name}
		if parent:
			d["parent"] = parent
		d.update(fields)
		self.widgets.append(d)
		return wid

	def plum(self, alpha):
		return self.PLUM + [alpha]

	@staticmethod
	def tint(color, alpha):
		return list(color[:3]) + [alpha]

	def text(self, name, parent, label, size=22, width=376, height=30, display=False, color=None, **kw):
		return self.w("Text", name, parent, OffsetMax=[width, height], Text=label, Font=self.fd if display else self.f, FontSize=size, Color=color or self.BONE, **kw)

	def row_button(self, name, parent, label, width=460, height=56, size=30, align="Left"):
		"""A menu row: text on nothing; a tint under the pointer and a brass bar (shown by menus.lua) on its left edge."""
		b = self.w("Button", name, parent, OffsetMax=[width, height], Color=self.CLEAR, HoverColor=self.tint(self.BRASS, 0.16), PressedColor=self.tint(self.BRASS, 0.32))
		self.w("Panel", "Accent", b, AnchorMin=[0, 0], AnchorMax=[0, 1], OffsetMin=[0, 6], OffsetMax=[5, -6], Color=self.BRASS, Visible=False)
		self.w("Text", "Label", b, AnchorMin=[0, 0], AnchorMax=[1, 1], OffsetMin=[24 if align == "Left" else 0, (height - size) // 2 - 2], OffsetMax=[0, 0],
			Text=label, Font=self.f, FontSize=size, Align=align, Color=self.BONE)
		return b

	def action_button(self, name, parent, label, width, height=48, size=24):
		"""A filled button in brass, for the one action of a card."""
		b = self.w("Button", name, parent, OffsetMax=[width, height], Color=self.tint(self.BRASS, 0.24), HoverColor=self.tint(self.BRASS, 0.45),
			PressedColor=self.tint(self.BRASS, 0.65))
		self.w("Text", "Label", b, AnchorMin=[0, 0], AnchorMax=[1, 1], OffsetMin=[0, (height - size) // 2 - 2], OffsetMax=[0, 0], Text=label, Font=self.f,
			FontSize=size, Align="Center", Color=self.BONE)
		return b

	def left_column(self, name, heading, heading_size, heading_height, visible, tagline=None):
		"""A screen with its menu on the left over a band that steps from dark to clear (there are no gradients)."""
		root = self.w("Panel", name, Color=self.CLEAR, Visible=visible, **self.FULL)
		for i, x in enumerate((0.46, 0.34, 0.22)):
			self.w("Panel", "Band%d" % (i + 1), root, AnchorMin=[0, 0], AnchorMax=[x, 1], OffsetMin=[0, 0], OffsetMax=[0, 0], Color=self.plum(0.28))
		box = self.w("VStack", "Box", root, AnchorMin=[0, 0], AnchorMax=[0, 0], OffsetMin=[96, 150], OffsetMax=[620, 900], Color=self.CLEAR, Spacing=10)
		self.text("Title", box, heading, heading_size, 520, heading_height, display=True, color=self.BRASS, Wrap=True, Outline=0.12, OutlineColor=[0, 0, 0, 0.8])
		if tagline:
			self.text("Tagline", box, tagline, 24, 520, 70, Wrap=True)
		return root, box

	def centered(self, name, heading, color, sub, tint_color):
		root = self.w("Panel", name, Color=tint_color, Visible=False, **self.FULL)
		box = self.w("VStack", "Box", root, AnchorMin=[0.5, 0.5], AnchorMax=[0.5, 0.5], OffsetMin=[-320, -240], OffsetMax=[320, 240], Color=self.CLEAR, Spacing=10,
			ChildAlign="Center")
		self.text("Title", box, heading, 120, 640, 140, display=True, color=color, Align="Center", Outline=0.12, OutlineColor=[0, 0, 0, 0.8])
		self.text("Sub", box, sub, 24, 640, 44, color=self.HAZE, Align="Center")
		return box

	def loading(self, label):
		"""A small bar in the lower middle of the screen: the gate of the level, the level from the main menu."""
		box = self.w("VStack", "Loading", AnchorMin=[0.5, 0.5], AnchorMax=[0.5, 0.5], OffsetMin=[-250, 120], OffsetMax=[250, 190], Color=self.plum(0.7), Spacing=6,
			Padding=[10, 8, 10, 8], Visible=False)
		self.text("LoadText", box, label, 22, 480, 28, Align="Center")
		self.w("Bar", "LoadBar", box, OffsetMax=[480, 16], Value=0.0, Color=self.BRASS, BackgroundColor=[0, 0, 0, 0.55])

	def save(self, rel):
		write_json(rel, {"version": 1, "referenceResolution": [1920, 1080], "match": 0.5, "nextId": self.next_id, "widgets": self.widgets})
		return write_meta(rel, "UiPanelImporter", 1)


def build_menu_ui(font, display_font):
	"""The main menu scene: the title column on the left (the heroes stand on the right, in the 3D scene) and the hero select cards."""
	k = UiKit(font, display_font)
	w, text = k.w, k.text
	root, box = k.left_column("Title", "Warrior's Crossing", 84, 190, True, "Cross the bridge. Face the dragon.")
	k.row_button("Play", box, "Play")
	k.row_button("Options", box, "Options")
	k.row_button("Quit", box, "Quit")
	w("Text", "Version", root, AnchorMin=[0, 1], AnchorMax=[0, 1], OffsetMin=[96, -64], OffsetMax=[500, -30], Text="Version", Font=k.f, FontSize=18, Color=k.HAZE)
	w("Text", "Controls", root, AnchorMin=[1, 1], AnchorMax=[1, 1], OffsetMin=[-860, -64], OffsetMax=[-60, -30], Text="WASD move   Space jump   Left click attack   P pause",
		Font=k.f, FontSize=18, Align="Right", Color=k.HAZE)

	# hero select: a card under each hero, the camera shows them from the front
	heroes = w("Panel", "Heroes", Color=k.CLEAR, Visible=False, **k.FULL)
	w("Text", "Header", heroes, AnchorMin=[0.5, 0], AnchorMax=[0.5, 0], OffsetMin=[-500, 44], OffsetMax=[500, 124], Text="Choose who crosses", Font=k.fd, FontSize=64,
		Align="Center", Color=k.BRASS, Outline=0.12, OutlineColor=[0, 0, 0, 0.8])
	box = w("VStack", "Box", heroes, AnchorMin=[0.5, 1], AnchorMax=[0.5, 1], OffsetMin=[-500, -420], OffsetMax=[500, -40], Color=k.CLEAR, Spacing=12, ChildAlign="Center")
	cards = w("HStack", "Cards", box, OffsetMax=[964, 290], Spacing=284, Color=k.CLEAR)
	for key, role, stats in (("Warrior", "Close-range fighter. Swings a sword, spends stamina.", (0.85, 0.2, 0.6)),
			("Wizard", "Spell caster. Fires magic bolts, spends mana.", (0.55, 0.95, 0.5))):
		card = w("VStack", key + "Card", cards, OffsetMax=[340, 290], Color=k.plum(0.8), Spacing=8, Padding=[20, 14, 20, 14])
		text("Name", card, key, 44, 300, 50, display=True)
		text("Role", card, role, 18, 300, 46, color=k.HAZE, Wrap=True)
		for label, value in zip(("Power", "Reach", "Speed"), stats):
			row = w("HStack", "Stat" + label, card, OffsetMax=[300, 22], Spacing=10, Color=k.CLEAR)
			text("Label", row, label, 18, 80, 22)
			w("Bar", "Value", row, OffsetMax=[210, 10], Value=value, Color=k.BRASS, BackgroundColor=[0, 0, 0, 0.55])
		k.action_button("Select", card, "Choose " + key, 300)
	k.row_button("Back", box, "Back", 200, 52, 26, "Center")

	k.loading("Setting out")
	return k.save("game/ui/main_menu.wuipanel")


def build_hud_ui(font, display_font):
	"""The level: the HUD (hero, health, stamina or mana, objective, boss, enemy bars) and the pause, dead and won screens."""
	k = UiKit(font, display_font)
	w, text = k.w, k.text
	hud = w("VStack", "Hud", OffsetMin=[24, 24], OffsetMax=[424, 190], Color=k.plum(0.62), Spacing=6, Padding=[14, 10, 14, 12])
	text("HeroName", hud, "Warrior", 32, 372, 38, display=True, color=k.BRASS)
	text("HpText", hud, "Health", 20, 372, 26)
	w("Bar", "Hp", hud, OffsetMax=[372, 16], Value=1.0, Color=k.EMBER, BackgroundColor=[0, 0, 0, 0.55])
	text("ResourceText", hud, "Stamina", 20, 372, 26)
	w("Bar", "Resource", hud, OffsetMax=[372, 12], Value=1.0, Color=k.BRASS, BackgroundColor=[0, 0, 0, 0.55])
	w("Text", "Objective", AnchorMin=[0.5, 0], AnchorMax=[0.5, 0], OffsetMin=[-500, 24], OffsetMax=[500, 66], Text="", Font=k.f, FontSize=28, Align="Center",
		Color=k.BONE, Outline=0.2, OutlineColor=[0, 0, 0, 1])
	boss = w("VStack", "Boss", AnchorMin=[0.5, 1], AnchorMax=[0.5, 1], OffsetMin=[-300, -104], OffsetMax=[300, -30], Color=k.plum(0.7), Spacing=6,
		Padding=[10, 8, 10, 8], Visible=False)
	text("BossName", boss, "The Yellow Dragon", 30, 580, 36, display=True, Align="Center")
	w("Bar", "BossBar", boss, OffsetMax=[580, 20], Value=1.0, Color=k.EMBER, BackgroundColor=[0, 0, 0, 0.6])
	for i in range(1, 7):
		w("Bar", "Bar%d" % i, Pivot=[0.5, 1], OffsetMax=[90, 8], Value=1.0, Color=k.EMBER, BackgroundColor=[0, 0, 0, 0.7], Visible=False)
	k.loading("The gate is opening")

	root, box = k.left_column("Pause", "Paused", 64, 80, False)
	k.row_button("Resume", box, "Resume")
	k.row_button("Options", box, "Options")
	k.row_button("Restart", box, "Restart level")
	k.row_button("MainMenu", box, "Main menu")

	box = k.centered("Dead", "You died", k.EMBER, "Your last checkpoint is waiting.", [0.12, 0.02, 0.02, 0.6])
	k.row_button("Respawn", box, "Respawn at checkpoint", 440, 56, 28, "Center")
	k.row_button("Restart", box, "Restart level", 440, 56, 28, "Center")
	k.row_button("MainMenu", box, "Main menu", 440, 56, 28, "Center")
	box = k.centered("Won", "Victory", k.BRASS, "The dragon is slain.", [0, 0, 0, 0.5])
	k.row_button("Restart", box, "Play again", 440, 56, 28, "Center")
	k.row_button("MainMenu", box, "Main menu", 440, 56, 28, "Center")
	return k.save("game/ui/game_hud.wuipanel")


def build_options_ui(font, display_font):
	"""The Options screen: a drawer on the right, the scene stays visible on the left. Shown by lib/options.lua on its own canvas."""
	k = UiKit(font, display_font)
	w, text = k.w, k.text
	root = w("Panel", "Options", Color=[0, 0, 0, 0.3], Visible=True, **k.FULL)
	box = w("VStack", "Box", root, AnchorMin=[1, 0], AnchorMax=[1, 1], OffsetMin=[-760, 0], OffsetMax=[0, 0], Color=k.plum(0.92), Spacing=12, Padding=[48, 40, 48, 32])
	text("Title", box, "Options", 56, 664, 70, display=True, color=k.BRASS)
	tabs = w("HStack", "Tabs", box, OffsetMax=[664, 48], Spacing=8, Color=k.CLEAR)
	for tab in ("Graphics", "Controls", "Game"):
		b = w("Button", tab, tabs, OffsetMax=[170, 44], Color=k.CLEAR, HoverColor=k.tint(k.BRASS, 0.14), PressedColor=k.tint(k.BRASS, 0.3))
		w("Text", "Label", b, AnchorMin=[0, 0], AnchorMax=[1, 1], OffsetMin=[0, 8], OffsetMax=[0, 0], Text=tab, Font=k.f, FontSize=26, Align="Center", Color=k.BONE)
	w("Panel", "Divider", box, OffsetMax=[664, 2], Color=k.tint(k.HAZE, 0.5))

	def tab_panel(name, visible):
		return w("VStack", name, box, OffsetMax=[664, 560], Spacing=8, Visible=visible, Color=k.CLEAR)

	def row(parent, name, label):
		r = w("HStack", name, parent, OffsetMax=[664, 52], Spacing=12, Padding=[14, 6, 14, 6], Color=[1, 1, 1, 0.05])
		text("Label", r, label, 22, 290, 40)
		return r

	def toggle(parent, name, label):
		w("Toggle", "Control", row(parent, name, label), OffsetMax=[60, 34], Value=0.0, Color=k.BRASS)

	def slider(parent, name, label, low, high, step, value):
		w("Slider", "Control", row(parent, name, label), OffsetMax=[300, 28], MinValue=low, MaxValue=high, Step=step, Value=value, Color=k.BRASS)

	def choice(parent, name, label, current):
		b = w("Button", "Control", row(parent, name, label), OffsetMax=[250, 40], Color=[1, 1, 1, 0.1], HoverColor=k.tint(k.BRASS, 0.3), PressedColor=k.tint(k.BRASS, 0.5))
		w("Text", "Label", b, AnchorMin=[0, 0], AnchorMax=[1, 1], OffsetMin=[0, 8], OffsetMax=[0, 0], Text=current, Font=k.f, FontSize=20, Align="Center", Color=k.BONE)

	g = tab_panel("GraphicsTab", True)
	toggle(g, "Vsync", "Vertical sync")
	slider(g, "Shadows", "Shadow distance", 20, 150, 10, 60)
	toggle(g, "Bloom", "Bloom")
	toggle(g, "Ao", "Ambient occlusion")
	toggle(g, "MotionBlur", "Motion blur")
	choice(g, "AntiAliasing", "Anti-aliasing", "Off")
	slider(g, "Fov", "Field of view", 50, 90, 1, 60)

	c = tab_panel("ControlsTab", False)
	for action in ("Jump", "Fire", "Melee", "Pause"):
		choice(c, action, action, "-")
	slider(c, "Sensitivity", "Mouse sensitivity", 0.2, 3, 0.1, 1)
	toggle(c, "InvertY", "Invert Y")
	choice(c, "Reset", "Controls", "Reset to defaults")
	text("Note", c, "", 18, 640, 28, color=k.BRASS)

	gm = tab_panel("GameTab", False)
	choice(gm, "Difficulty", "Difficulty", "Normal")
	slider(gm, "CameraDistance", "Camera distance", 3, 9, 0.5, 5.5)
	toggle(gm, "Hints", "Objective hints")

	k.row_button("Back", box, "Back", 664, 52, 28, "Center")
	return k.save("game/ui/options.wuipanel")


# ------------------------------------------------------------------------------------------------------------------ scenes

def quat_euler(x=0, y=0, z=0):
	return demo.euler(x, y, z)


class Builder:
	def __init__(self, g):
		self.g = g
		self.s = Scene()

	def mesh(self, shape, mat):
		return {"MeshRenderer": {"Mesh": ref(self.g["shapes"], MESH[shape]), "Materials": [material(mat)]}}

	def script(self, name, **props):
		return {"script:" + self.g["scripts"][name]: props}

	@staticmethod
	def merge(*parts):
		out = {}
		for part in parts:
			out.update(part)
		return out

	@staticmethod
	def light(kind, color, intensity, rng=10, inner=20, outer=30, shadows=False):
		return {"Light": {"Kind": kind, "Color": list(color), "Intensity": intensity, "Range": rng, "InnerAngle": inner, "OuterAngle": outer, "CastsShadows": shadows}}

	def block(self, name, parent, pos, size, mat, shape="cube", collider=True, **extra):
		"""A scaled solid: mesh and box collider (the unit cube and the box collider both measure 1 at scale 1)."""
		comps = self.merge(self.mesh(shape, mat), {"Collider": BOX} if collider else {}, extra)
		return self.s.add(name, parent, pos=pos, scale=size, **comps)

	def trigger(self, name, parent, pos, size, **extra):
		return self.s.add(name, parent, pos=pos, scale=size, Collider=dict(BOX, IsTrigger=True), **extra)

	def character(self, name, parent, pos, model, model_scale=1.0, radius=0.5, height=1.8, scripts=None, animator=None):
		"""A walking entity: a CharacterController root and its model as the first child."""
		cc = {"CharacterController": {"Radius": radius, "Height": height, "Offset": [0, height / 2, 0]}}
		root = self.s.add(name, parent, pos=pos, **self.merge(cc, scripts or {}))
		extra = animator or {}
		self.s.add(name + " model", name, scale=(model_scale,) * 3, PrefabInstance={"Prefab": ref(model_guid(model))}, **extra)
		return root


def scene_settings(g, dusk=False):
	settings = demo.scene_settings(g | {"scripts": {"scene_overlay": asset("scripts/scene_overlay.lua")}})
	settings["scripts"] = []
	settings["motionBlur"] = {"enabled": False}
	# golden hour: a warm, low light, a touch of glow and a vignette
	settings["ambient"] = 0.55
	settings["bloom"] = {"enabled": True, "threshold": 0.9, "knee": 0.5, "intensity": 0.14}
	settings["tonemap"].update({"temperature": 0.12, "vignette": 0.38, "contrast": 1.08, "saturation": 1.15})
	if dusk:
		settings["ambient"] = 0.35
		settings["tonemap"]["temperature"] = 0.25
		settings["tonemap"]["vignette"] = 0.45
		settings["bloom"]["intensity"] = 0.2
	return settings


def build_level(g):
	b = Builder(g)
	s = b.s
	script, light, block = b.script, b.light, b.block

	s.add("Camera", pos=(0, 5, 18), rot=quat_euler(-18, 0, 0),
		Camera={"Projection": "Perspective", "FovY": 60, "Near": 0.1, "Far": 400, "OrthoSize": 10, "Primary": True}, **script("camera_follow"))
	s.add("Sun", pos=(0, 20, 0), rot=quat_euler(-24, 40, 0), **light("Directional", (1, 0.74, 0.48, 1), 3.4, shadows=True))
	s.add("Hud", UICanvas={"Panel": ref(g["hud_ui"]), "SortOrder": 0, "Visible": True})
	s.add("Options", UICanvas={"Panel": ref(g["options_ui"]), "SortOrder": 10, "Visible": True})
	s.add("GameState", **script("game_state"))
	s.add("SelfTest", **script("selftest"))
	s.add("Spawn point", pos=(0, 0.1, 12))

	# --- the player
	warrior_animator = {"Animator": {"Skeleton": ref(model_guid("Warrior"), 1), "Graph": ref(asset("models/characters/Warrior_animation/warrior.wgraph")), "PlayOnStart": False}}
	wizard_clips = dict(wizard_idle=clip_for("Wizard", "Idle"), wizard_walk=clip_for("Wizard", "Walk", "Run"), wizard_attack=clip_for("Wizard", "Spell1", "Attack"))
	s.add("Player", pos=(0, 0.1, 12), CharacterController={"Radius": 0.5, "Height": 1.8, "Offset": [0, 0.9, 0]}, **script("player", speed=6, jump_speed=8, **wizard_clips))
	s.add("Warrior model", "Player", PrefabInstance={"Prefab": ref(model_guid("Warrior"))}, **warrior_animator)
	s.add("Wizard model", "Player", scale=(0, 0, 0), PrefabInstance={"Prefab": ref(model_guid("Wizard"))})

	# --- courtyard: x -15..15, z -16.7..15
	s.add("World")
	s.add("Ground", "World", pos=(0, 0, -0.85), scale=(30, 1, 31.7), **b.mesh("plane_tiled", "ground"))
	s.add("Ground collider", "World", pos=(0, -0.5, -0.85), scale=(30, 1, 31.7), Collider=BOX)
	block("Wall west", "World", (-15.5, 1.5, -0.85), (1, 3, 31.7), "column")
	block("Wall east", "World", (15.5, 1.5, -0.85), (1, 3, 31.7), "column")
	block("Wall south", "World", (0, 1.5, 15.5), (32, 3, 1), "column")
	block("Wall north left", "World", (-9, 1.5, -17), (12, 3, 1), "column")
	block("Wall north right", "World", (9, 1.5, -17), (12, 3, 1), "column")
	for i, (x, z) in enumerate(((-8, 6), (8, 6), (-8, -6), (8, -6))):
		block("Pillar %d" % (i + 1), "World", (x, 2.5, z), (1, 5, 1), "column")
	for i, (x, z) in enumerate(((-3, 9), (3.4, 9.6), (11, 2), (-12, -12))):
		block("Crate %d" % (i + 1), "World", (x, 0.4, z), (0.8, 0.8, 0.8), "crate")
	for i, (x, z) in enumerate(((-12, 10), (12, 10), (-12, -14), (12, -14))):
		s.add("Torch %d" % (i + 1), "World", pos=(x, 2.2, z), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.6, 0.25, 1), 16, rng=11), {}))
		s.entities[-1]["components"]["Transform"]["Scale"] = [0.35, 0.35, 0.35]
	for side, x in (("W", -14.9), ("E", 14.9)):  # banners: the colour of the cloth of the menu rug
		for i, z in enumerate((9, -2, -10)):
			block("Banner %s%d" % (side, i + 1), "World", (x, 2.0, z), (0.1, 2.2, 1.1), "cloth", collider=False)

	# --- the bridge: z -17 .. -25.9 over a pit; planks are hinged bodies
	s.add("Bridge")
	block("Bridge post A", "Bridge", (0, -0.15, -17.0), (4, 0.3, 0.6), "steel")
	previous = "Bridge post A"
	links = 8
	for i in range(links):
		z = -17.8 - i
		anchor_b = [0, 0, -0.3] if i == 0 else [0, 0, -0.5]
		joint = {"Joint": {"Type": "Hinge", "Connected": s.ids[previous], "AnchorA": [0, 0, 0.5], "AnchorB": anchor_b, "Axis": [1, 0, 0], "BreakForce": 12000}}
		block("Bridge link %d" % i, "Bridge", (0, -0.15, z), (4, 0.3, 1.0), "crate", RigidBody={"Motion": "Dynamic", "MassOverride": 10, "LinearDamping": 0.3, "AngularDamping": 0.5}, **joint, **script("bridge_plank"))
		previous = "Bridge link %d" % i
	z_end = -17.8 - (links - 1) - 0.8
	block("Bridge post B", "Bridge", (0, -0.15, z_end), (4, 0.3, 0.6), "steel",
		Joint={"Type": "Hinge", "Connected": s.ids[previous], "AnchorA": [0, 0, 0.3], "AnchorB": [0, 0, -0.5], "Axis": [1, 0, 0]})
	for side in (-1, 1):
		block("Bridge rail %s" % ("L" if side < 0 else "R"), "Bridge", (side * 2.15, 0.5, -21.3), (0.15, 1.0, 9.0), "steel", collider=True)

	for i, z in enumerate((-18.5, -24.0)):  # lanterns: the bridge is lit at dusk
		for side, x in (("L", -2.15), ("R", 2.15)):
			s.add("Bridge lantern %s%d" % (side, i + 1), "Bridge", pos=(x, 1.25, z), scale=(0.3, 0.3, 0.3),
				**b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.6, 0.3, 1), 10, rng=6)))

	# --- beyond the bridge: landing, pit crossed by a sliding platform, the way to the gate
	s.add("Far side")
	block("Landing", "Far side", (0, -0.5, -29.45), (8, 1, 7.1), "column")
	block("Approach", "Far side", (0, -0.5, -48.5), (8, 1, 23), "column")
	block("Sliding platform", "Far side", (0, -0.15, -35), (4, 0.3, 4), "gold", RigidBody={"Motion": "Kinematic"}, **script("bridge_slider"))
	for side in (-1, 1):
		block("Landing rail %s" % ("L" if side < 0 else "R"), "Far side", (side * 4.15, 0.5, -29.45), (0.3, 1.0, 7.1), "steel")
		block("Approach rail %s" % ("L" if side < 0 else "R"), "Far side", (side * 4.15, 0.5, -48.5), (0.3, 1.0, 23), "steel")
	block("Gate door", "Far side", (0, 3, -47), (8, 6, 0.6), "podium", RigidBody={"Motion": "Kinematic"}, **script("door", group="approach", needs_arena=True))
	block("Courtyard gate", "World", (0, 3, -16.2), (6, 6, 0.6), "podium", RigidBody={"Motion": "Kinematic"}, **script("door", group="courtyard"))
	for side in (-1, 1):
		block("Gate pillar %s" % ("L" if side < 0 else "R"), "Far side", (side * 4.4, 2.5, -47), (1.2, 5, 1.2), "column")
	s.add("Gate", "Far side", pos=(0, 2, -43), scale=(8, 4, 3), Collider=dict(BOX, IsTrigger=True), **script("gate"))
	s.add("Gate lamp", "Far side", pos=(0, 4, -45), **light("Point", (0.5, 0.8, 1, 1), 25, rng=12))
	block("Beacon", "Far side", (0, 4.9, -47), (0.4, 0.4, 0.4), "hologram", shape="sphere", collider=False)
	for side, x in (("L", -4.4), ("R", 4.4)):
		s.add("Gate flame " + side, "Far side", pos=(x, 5.5, -47), scale=(0.5, 0.5, 0.5), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.5, 0.2, 1), 26, rng=10)))

	# --- checkpoints and zones (triggers)
	s.add("Checkpoints")
	b.trigger("Checkpoint 2", "Checkpoints", (0, 0.1, -29), (6, 3, 3), **script("checkpoint"))
	b.trigger("Checkpoint 3", "Checkpoints", (0, 0.1, -41), (6, 3, 2), **script("checkpoint"))
	s.add("Zones")
	b.trigger("Zone courtyard", "Zones", (0, 3, -0.85), (30, 6, 31.7), **script("zone", zone="courtyard"))
	b.trigger("Zone bridge", "Zones", (0, 3, -21.5), (6, 6, 9), **script("zone", zone="bridge"))
	b.trigger("Zone approach", "Zones", (0, 3, -43), (8, 6, 34), **script("zone", zone="approach"))

	# --- projectile pool: parked spheres, moved by projectiles.lua
	s.add("Projectiles", **script("projectiles", prefix="Projectile", owner="player", count=12, speed=24, life=2.5, damage=25))
	for i in range(1, 13):
		s.add("Projectile %d" % i, "Projectiles", pos=(0, -200, 0), scale=(0, 0, 0), **b.mesh("sphere", "neon_cyan"))
	s.add("Fireballs", **script("projectiles", prefix="Fireball", owner="boss", count=10, speed=13, life=3.5, damage=14, size=0.7))
	for i in range(1, 11):
		s.add("Fireball %d" % i, "Fireballs", pos=(0, -200, 0), scale=(0, 0, 0), **b.mesh("sphere", "neon_orange"))

	# --- drop pools: parked orbs, put where an enemy dies by drops.lua
	for manager, prefix, kind, amount, mat in (("Hearts", "Heart", "health", 25, "plastic_red"), ("Orbs", "Orb", "resource", 40, "gold")):
		s.add(manager, **script("drops", prefix=prefix, kind=kind, count=6, amount=amount, life=20))
		for i in range(1, 7):
			s.add("%s %d" % (prefix, i), manager, pos=(0, -200, 0), scale=(0, 0, 0), **b.mesh("sphere", mat))

	# --- enemies
	enemies = (
		("Enemy 1", "Yeti", (-6, 0.1, 2), "courtyard", 1), ("Enemy 2", "Yeti", (7, 0.1, -3), "courtyard", 2),
		("Enemy 3", "Panda", (-7, 0.1, -8), "courtyard", 3), ("Enemy 4", "Panda", (6, 0.1, -10), "courtyard", 4),
		("Enemy 5", "Yeti", (-2, 0.1, -52), "approach", 5), ("Enemy 6", "Panda", (2, 0.1, -55), "approach", 6))
	for name, model, pos, group, bar in enemies:
		b.enemy(name, model, pos, group, bar)

	# --- the sky and the demo environment
	return s


def build_main_menu(g):
	"""The main menu scene: the two heroes on a rug in lantern light, the bridge behind them, the gate and the dragon over it. Nothing here is solid."""
	b = Builder(g)
	s = b.s
	script, light, block = b.script, b.light, b.block

	s.add("Camera", pos=(0, 2.6, 8.6), rot=quat_euler(-13, 11, 0), Camera={"Projection": "Perspective", "FovY": 55, "Near": 0.1, "Far": 400, "OrthoSize": 10, "Primary": True},
		**script("menu_camera"))
	s.add("Sun", pos=(0, 20, 0), rot=quat_euler(-24, 40, 0), **light("Directional", (1, 0.74, 0.48, 1), 3.4, shadows=True))
	s.add("Menu", UICanvas={"Panel": ref(g["menu_ui"]), "SortOrder": 0, "Visible": True})
	s.add("Options", UICanvas={"Panel": ref(g["options_ui"]), "SortOrder": 10, "Visible": True})
	s.add("MenuState", **script("menu_state"))
	s.add("SmokeSignal", **script("menu_selftest"))

	# the heroes, on a rug, between two braziers; a warm light from the front, a cool one from behind
	for name, model, x in (("Stage Warrior", "Warrior", -1.4), ("Stage Wizard", "Wizard", 1.4)):
		s.add(name, pos=(x, 0, 2.0), PrefabInstance={"Prefab": ref(model_guid(model))}, **script("stage_hero", clip=clip_for(model, "Idle")))
	block("Stage rug", None, (0, 0.02, 2), (4.8, 0.04, 3.4), "cloth", collider=False)
	s.add("Stage key light", pos=(-3, 3.2, 6), rot=quat_euler(-23.7, -36.9, 0), **light("Spot", (1, 0.78, 0.5, 1), 90, rng=16, inner=18, outer=36))
	s.add("Stage rim light", pos=(2.6, 2.6, -1.2), **light("Point", (0.45, 0.55, 1, 1), 28, rng=9))
	for side, x in (("L", -3.9), ("R", 3.9)):
		block("Stage pillar " + side, None, (x, 0.55, 0.8), (0.5, 1.1, 0.5), "podium", collider=False)
		s.add("Stage flame " + side, pos=(x, 1.3, 0.8), scale=(0.4, 0.4, 0.4), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.55, 0.25, 1), 24, rng=9)))

	# the courtyard floor and walls with banners and torches
	s.add("Ground", pos=(0, 0, 6.5), scale=(26, 1, 17), **b.mesh("plane_tiled", "ground"))
	for side, x in (("W", -12.5), ("E", 12.5)):
		block("Wall " + side, None, (x, 1.5, 4), (1, 3, 15), "column", collider=False)
		for i, z in enumerate((9, 3, -3)):
			block("Banner %s%d" % (side, i + 1), None, (x - (0.55 if x > 0 else -0.55), 2.0, z), (0.1, 2.2, 1.1), "cloth", collider=False)
	for i, (x, z) in enumerate(((-8, 6), (8, 6), (-8, -1), (8, -1))):
		block("Pillar %d" % (i + 1), None, (x, 2.5, z), (1, 5, 1), "column", collider=False)
		s.add("Torch %d" % (i + 1), pos=(x, 5.3, z), scale=(0.35, 0.35, 0.35), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.6, 0.25, 1), 18, rng=11)))

	# the bridge into the distance, the gate on the far side and the dragon on top of it
	block("Bridge post A", None, (0, -0.15, -2.3), (4, 0.3, 0.6), "steel", collider=False)
	for i in range(8):
		block("Plank %d" % i, None, (0, -0.15, -3.1 - i), (4, 0.3, 1.0), "crate", collider=False)
	block("Bridge post B", None, (0, -0.15, -11.7), (4, 0.3, 0.6), "steel", collider=False)
	for side, x in (("L", -2.15), ("R", 2.15)):
		block("Bridge rail " + side, None, (x, 0.5, -7), (0.15, 1.0, 9.4), "steel", collider=False)
		s.add("Bridge lantern " + side, pos=(x, 1.25, -4), scale=(0.3, 0.3, 0.3), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.6, 0.3, 1), 10, rng=6)))
	block("Far landing", None, (0, -0.5, -18), (14, 1, 12), "column", collider=False)
	for side, x in (("L", -4.4), ("R", 4.4)):
		block("Gate pillar " + side, None, (x, 2.5, -22), (1.2, 5, 1.2), "column", collider=False)
		s.add("Gate flame " + side, pos=(x, 5.5, -22), scale=(0.5, 0.5, 0.5), **b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.5, 0.2, 1), 26, rng=10)))
	block("Gate lintel", None, (0, 5.4, -22), (10.4, 0.8, 1.2), "column", collider=False)
	s.add("Dragon", pos=(0, 5.8, -22), PrefabInstance={"Prefab": ref(model_guid("YellowDragon"))}, **script("stage_hero", clip=clip_for("YellowDragon", "Flying"), size=2.2))
	s.add("Dragon glow", pos=(0, 7, -26), **light("Point", (1, 0.3, 0.15, 1), 60, rng=22))
	block("Chasm floor", None, (0, -12, -10), (90, 1, 70), "plain", collider=False)
	return s


def enemy_stats(model):
	if model == "Panda":
		return dict(max_hp=90, speed=2.4, damage=14, reach=2.0, aggro=11)
	if model == "YellowDragon":
		return dict(max_hp=600, speed=3.4, damage=24, reach=3.2, aggro=60)
	return dict(max_hp=50, speed=4.2, damage=8, reach=1.8, aggro=12)


def _enemy(self, name, model, pos, group, bar, boss=False, dormant=False, scale=1.0):
	stats = enemy_stats(model)
	props = dict(stats, group=group, bar=bar, boss=boss, dormant=dormant, model_scale=scale, bar_height=2.4 * scale,
		clip_idle=clip_for(model, "Idle", "Flying"), clip_move=clip_for(model, "Walk", "Flying"),
		clip_attack=clip_for(model, "Bite_Front", "Attack"), clip_death=clip_for(model, "Death", "Die"))
	height = 1.8 * scale
	self.character(name, None, pos, model, model_scale=scale, radius=0.5 * scale, height=height, scripts=self.script("enemy", **props))


Builder.enemy = _enemy


def build_arena(g):
	"""The boss arena, loaded additively behind the gate (z -60 .. -100). Names are unique across both scenes."""
	b = Builder(g)
	s = b.s
	light, block = b.light, b.block
	s.add("Arena")
	s.add("Arena ground", "Arena", pos=(0, 0, -80), scale=(40, 1, 40), **b.mesh("plane_tiled", "ground"))
	s.add("Arena ground collider", "Arena", pos=(0, -0.5, -80), scale=(40, 1, 40), Collider=BOX)
	block("Arena wall west", "Arena", (-20.5, 2.5, -80), (1, 5, 40), "column")
	block("Arena wall east", "Arena", (20.5, 2.5, -80), (1, 5, 40), "column")
	block("Arena wall back", "Arena", (0, 2.5, -100.5), (42, 5, 1), "column")
	block("Arena wall front left", "Arena", (-12, 2.5, -60.5), (16, 5, 1), "column")
	block("Arena wall front right", "Arena", (12, 2.5, -60.5), (16, 5, 1), "column")
	for i, (x, z) in enumerate(((-10, -72), (10, -72), (-10, -88), (10, -88))):
		block("Arena pillar %d" % (i + 1), "Arena", (x, 3, z), (1.6, 6, 1.6), "podium")
		s.add("Arena brazier %d" % (i + 1), "Arena", pos=(x, 6.4, z), scale=(0.7, 0.7, 0.7),
			**b.merge(b.mesh("sphere", "neon_orange"), light("Point", (1, 0.4, 0.15, 1), 30, rng=14)))
	s.add("Arena light", "Arena", pos=(0, 9, -80), **b.merge(light("Point", (1, 0.15, 0.1, 1), 30, rng=40), b.script("boss_light", amplitude=14, speed=2.2)))
	b.trigger("Arena zone", "Arena", (0, 3, -80), (40, 8, 40), **b.script("zone", zone="arena"))
	b.trigger("Checkpoint 4", "Arena", (0, 0.1, -63), (8, 3, 2), **b.script("checkpoint"))
	b.enemy("Dragon", "YellowDragon", (0, 0.1, -90), "boss", 0, boss=True, scale=1.7)
	b.enemy("Enemy 7", "Yeti", (-9, 0.1, -92), "minions", 0, dormant=True)
	b.enemy("Enemy 8", "Yeti", (9, 0.1, -92), "minions", 0, dormant=True)
	return s


# ------------------------------------------------------------------------------------------------------------------ main

DISPLAY_FONT_URL = "https://github.com/google/fonts/raw/main/ofl/almendrasc/AlmendraSC-Regular.ttf"
DISPLAY_FONT_LICENSE_URL = "https://github.com/google/fonts/raw/main/ofl/almendrasc/OFL.txt"


def build_display_font(fallback):
	"""Almendra SC (OFL) for the title and the screen headings; Lato (the fallback) if it cannot be downloaded."""
	try:
		write("game/fonts/AlmendraSC-Regular.ttf", demo.download(DISPLAY_FONT_URL, "AlmendraSC-Regular.ttf").read_bytes(), binary=True)
		write("game/fonts/OFL-AlmendraSC.txt", demo.download(DISPLAY_FONT_LICENSE_URL, "OFL-AlmendraSC.txt").read_bytes(), binary=True)
	except OSError as error:
		print("warning: the display font could not be downloaded (%s), the menus use Lato" % error)
		return fallback
	return write_meta("game/fonts/AlmendraSC-Regular.ttf", "FontImporter", 1, {"pixelSize": 64, "sdfRange": 8, "charset": "latin1"})


def main():
	if not (demo.ASSETS / "models" / "characters" / "Warrior.fbx").exists():
		sys.exit("run scripts/gen_demo_scene.py first: the game reuses its models, materials, sky and font")

	g = {"shapes": asset("models/shapes/shapes.glb"), "env": asset("environments/sky.hdr")}
	g["scripts"] = build_scripts()
	font = asset("fonts/Lato-Regular.ttf")
	display = build_display_font(font)
	g["hud_ui"] = build_hud_ui(font, display)
	g["menu_ui"] = build_menu_ui(font, display)
	g["options_ui"] = build_options_ui(font, display)
	input_map = build_input()

	level = build_level(g)
	write_json("game/scenes/level.wscene", {"name": "Warrior's Crossing", "settings": scene_settings(g), "entities": level.entities})
	level_guid = write_meta("game/scenes/level.wscene", "SceneImporter", 5)

	menu = build_main_menu(g)
	write_json("game/scenes/main_menu.wscene", {"name": "Main menu", "settings": scene_settings(g), "entities": menu.entities})
	menu_guid = write_meta("game/scenes/main_menu.wscene", "SceneImporter", 5)

	arena = build_arena(g)
	write_json("game/scenes/boss_arena.wscene", {"name": "Boss arena", "settings": scene_settings(g, dusk=True), "entities": arena.entities})
	arena_guid = write_meta("game/scenes/boss_arena.wscene", "SceneImporter", 5)

	(ROOT / "editor" / "input.wsettings").write_text(json.dumps({"defaultMap": input_map}, indent=2) + "\n", encoding="utf-8", newline="\n")
	(ROOT / "editor" / "build_settings.json").write_text(json.dumps({"alwaysInclude": [], "defaultScene": menu_guid,
		"scenes": [menu_guid, level_guid, arena_guid, demo.SCENE_GUID]}, indent=2) + "\n", encoding="utf-8", newline="\n")
	print("wrote %d + %d entities, %d scripts" % (len(level.entities), len(arena.entities), len(g["scripts"])))


if __name__ == "__main__":
	main()
