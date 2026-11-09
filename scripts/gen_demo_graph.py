#!/usr/bin/env python3
"""Writes the animation graph of the demo warrior (editor/assets/models/characters/Warrior_animation/warrior.wgraph).

The graph: parameters speed (float), upper (float, the weight of the upper body layer) and attack (trigger).
Base layer: Idle, and Move (a blend space of Idle / Walk / Run on speed), with a short crossfade into Move and a longer one back.
Upper layer (override, from the Abdomen up): None, and Attack (the sword attack, played once when the trigger fires).
The guids are the ones of the models' .meta files (the files the model importer extracted).
Run from the repository root: python scripts/gen_demo_graph.py
"""
import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
FOLDER = ROOT / "editor" / "assets" / "models" / "characters" / "Warrior_animation"


def guid_of(path):
    return json.loads(pathlib.Path(str(path) + ".meta").read_text())["guid"]


def ref(path):
    return {"guid": guid_of(path)}


def clip(name):
    return ref(FOLDER / "clips" / f"CharacterArmature_{name}.wanim")


def state(name, body, x, y, loop=True, speed=1.0, speed_parameter=None):
    result = {"name": name, "body": body, "loop": loop, "speed": speed, "position": [x, y]}
    if speed_parameter:
        result["speedParameter"] = speed_parameter
    return result


def transition(to, conditions, duration, source=None, exit_time=None):
    result = {"to": to, "duration": duration, "conditions": conditions}
    if source:
        result["from"] = source
    if exit_time is not None:
        result["exitTime"] = exit_time
    return result


graph = {
    "version": 1,
    "name": "warrior",
    "skeleton": ref(FOLDER / "Skin 0.wskel"),
    "parameters": [
        {"name": "speed", "type": "float", "default": 0},
        {"name": "upper", "type": "float", "default": 0},
        {"name": "attack", "type": "trigger", "default": 0},
    ],
    "layers": [
        {
            "name": "Base",
            "mode": "base",
            "weight": 1,
            "mask": {"root": "", "falloff": 0, "joints": []},
            "machine": {
                "default": "Idle",
                "states": [
                    state("Idle", {"clip": clip("Idle")}, 0, 0),
                    state("Move", {"blend": {"x": "speed", "y": "", "samples": [
                        {"clip": clip("Idle"), "x": 0, "y": 0},
                        {"clip": clip("Walk"), "x": 1, "y": 0},
                        {"clip": clip("Run"), "x": 3, "y": 0},
                    ]}}, 220, 0),
                ],
                "transitions": [
                    transition("Move", [{"parameter": "speed", "op": ">", "value": 0.1}], 0.15, source="Idle"),
                    transition("Idle", [{"parameter": "speed", "op": "<=", "value": 0.1}], 0.3, source="Move"),
                ],
            },
        },
        {
            "name": "Upper",
            "mode": "override",
            "weight": 1,
            "weightParameter": "upper",
            "mask": {"root": "Abdomen", "falloff": 0, "joints": []},
            "machine": {
                "default": "None",
                "states": [
                    state("None", {"clip": clip("Idle")}, 0, 0),
                    state("Attack", {"clip": clip("Sword_Attack")}, 220, 0, loop=False),
                ],
                "transitions": [
                    transition("Attack", [{"parameter": "attack", "op": "fired", "value": 0}], 0.1, source="None"),
                    transition("None", [], 0.2, source="Attack", exit_time=0.95),
                ],
            },
        },
    ],
}

out = FOLDER / "warrior.wgraph"
out.write_text(json.dumps(graph, indent="\t"), encoding="utf-8")
# the guid gen_demo_scene.py gives the file (derived from its path), so the scene can point at it; .meta files are git-ignored: git add -f it
meta = {"guid": hashlib.md5(b"asset:models/characters/Warrior_animation/warrior.wgraph").hexdigest(), "importer": "GraphFileImporter", "version": 1, "importSettings": {}, "subAssets": {}}
(FOLDER / "warrior.wgraph.meta").write_text(json.dumps(meta, indent=2, sort_keys=True), encoding="utf-8")
print("wrote", out)
