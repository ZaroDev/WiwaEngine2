#!/usr/bin/env python3
"""Packs the stress hall and runs the game on it with --bench, in a temporary folder (the project's game/assets.wpak and editor/build_settings.json stay as they are).

    python scripts/bench_stress.py [--config=Release] [--seconds=20] [--api=vulkan|d3d11|d3d12] [--target=60] [--no-pack]

Run scripts/gen_demo_scene.py and scripts/gen_stress_scene.py first, and build the editor and the game. Exit code: that of the game (0 when the average
FPS reached the target). The first run imports Sponza and the other models (a minute or two); the next ones are fast.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FLAGS = dict((a[2:].split("=", 1) + [""])[:2] for a in sys.argv[1:] if a.startswith("--"))
CONFIG = FLAGS.get("config", "Release")
SECONDS = FLAGS.get("seconds", "20")
TARGET = FLAGS.get("target", "60")
API = FLAGS.get("api", "vulkan")
BIN = ROOT / "bin" / ("%s-windows-x86_64" % CONFIG)
EDITOR = ROOT / "editor"
SETTINGS = EDITOR / "build_settings.json"


def main():
	meta = EDITOR / "assets" / "scenes" / "stress.wscene.meta"
	if not meta.exists():
		sys.exit("run scripts/gen_stress_scene.py first")
	scene = json.loads(meta.read_text())["guid"]
	out = pathlib.Path(tempfile.gettempdir()) / "wiwa_stress_bench"

	if "no-pack" not in FLAGS:
		shutil.rmtree(out, ignore_errors=True)
		out.mkdir(parents=True)
		original = SETTINGS.read_text() if SETTINGS.exists() else None
		try:
			current = json.loads(original) if original else {"alwaysInclude": []}
			current["defaultScene"] = scene
			current["scenes"] = [scene]
			SETTINGS.write_text(json.dumps(current, indent=2) + "\n")
			result = subprocess.run([str(BIN / "wiwa_editor" / "wiwa_editor.exe"), "--pack", str(out)], cwd=EDITOR)
		finally:
			if original is None:
				SETTINGS.unlink(missing_ok=True)
			else:
				SETTINGS.write_text(original)
		if result.returncode != 0 or not (out / "assets.wpak").exists():
			sys.exit("packing failed (%d)" % result.returncode)
		shutil.copy2(BIN / "wiwa_game" / "wiwa_game.exe", out / "wiwa_game.exe")
		quality = ROOT / "game" / "render_quality.json"
		if quality.exists():
			shutil.copy2(quality, out / "render_quality.json")
		print("pack: %.1f MB" % ((out / "assets.wpak").stat().st_size / 1e6))

	command = [str(out / "wiwa_game.exe"), "--scene", "assets/scenes/stress.wscene", "--bench", SECONDS, "--bench-target", TARGET, "--api=" + API]
	print(" ".join(command))
	sys.exit(subprocess.run(command, cwd=out).returncode)


if __name__ == "__main__":
	main()
