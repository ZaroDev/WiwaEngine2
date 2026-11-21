#!/usr/bin/env python3
"""Packs a scene, runs the game on it with --bench and makes the benchmark report (docs/superpowers/documentation/benchmarking.md).

    python scripts/bench_stress.py [--scene=stress|demo] [--seconds=20] [--api=vulkan|d3d11|d3d12] [--target=60] [--config=Release]
                                   [--label=text] [--baseline=last|path.json] [--fail-on-regression=5] [--out=dir] [--open] [--no-pack]

Everything happens in a temporary folder (the project's game/assets.wpak and editor/build_settings.json stay as they are). The results go to
bench_results/<time>-<label>/ (git-ignored): result.json (the frames), report.html (the report) and run.log (the game's output).

--scene=stress (default): the stress hall (run scripts/gen_demo_scene.py and scripts/gen_stress_scene.py first).
--scene=demo: the demo hall, packed with the project's own build settings.
--baseline=last: compares with the previous run of the same scene on the same API (bench_results/<scene>-<api>-last.json, updated after every run);
a path compares with that result. --fail-on-regression=N makes the exit code 3 when the average FPS or the 1% low fell by more than N percent.

Build the editor and the game first. The first run of a scene imports its models (a minute or two); the next ones are fast.
Exit code: 0 when the average FPS reached the target (and there was no regression), 1 when it did not, 3 on a regression, 2 on a failure of the run itself.
"""
import datetime
import json
import os
import pathlib
import re
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
SCENE = FLAGS.get("scene", "stress")
LABEL = FLAGS.get("label", "")
BIN = ROOT / "bin" / ("%s-windows-x86_64" % CONFIG)
EDITOR = ROOT / "editor"
SETTINGS = EDITOR / "build_settings.json"
RESULTS = ROOT / "bench_results"


def git(*args):
	try:
		return subprocess.run(["git", "-C", str(ROOT)] + list(args), capture_output=True, text=True, timeout=20).stdout.strip()
	except (OSError, subprocess.SubprocessError):
		return ""


def scene_guid(name):
	meta = EDITOR / "assets" / "scenes" / (name + ".wscene.meta")
	if not meta.exists():
		sys.exit("%s is missing: run scripts/gen_demo_scene.py%s first" % (meta.name, " and scripts/gen_stress_scene.py" if name == "stress" else ""))
	return json.loads(meta.read_text())["guid"]


def pack(out):
	shutil.rmtree(out, ignore_errors=True)
	out.mkdir(parents=True)
	original = SETTINGS.read_text() if SETTINGS.exists() else None
	try:
		if SCENE == "stress":
			guid = scene_guid("stress")
			current = json.loads(original) if original else {"alwaysInclude": []}
			current["defaultScene"] = guid
			current["scenes"] = [guid]
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


def main():
	if SCENE not in ("stress", "demo"):
		sys.exit("--scene is stress or demo")
	work = pathlib.Path(tempfile.gettempdir()) / "wiwa_bench"
	if "no-pack" not in FLAGS:
		pack(work)

	stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
	slug = re.sub(r"[^A-Za-z0-9_-]+", "-", LABEL).strip("-")
	out = pathlib.Path(FLAGS["out"]) if FLAGS.get("out") else RESULTS / (stamp + "-" + SCENE + ("-" + slug if slug else ""))
	out.mkdir(parents=True, exist_ok=True)
	result = out / "result.json"

	commit = git("rev-parse", "--short", "HEAD")
	dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
	label = LABEL or ("%s on %s" % (SCENE, commit or "?"))
	scene_path = "assets/scenes/%s.wscene" % SCENE
	command = [str(work / "wiwa_game.exe"), "--scene", scene_path, "--bench", SECONDS, "--bench-target", TARGET, "--api=" + API, "--bench-out", str(result),
		"--bench-label", label, "--bench-meta", "commit=%s%s" % (commit, "+local changes" if dirty else ""), "--bench-meta", "scene file=" + scene_path]
	print(" ".join(command))
	with open(out / "run.log", "w", encoding="utf-8", errors="replace") as log:
		code = subprocess.run(command, cwd=work, stdout=log, stderr=subprocess.STDOUT).returncode
	if not result.exists():
		print("the game wrote no result (exit %d); its output is in %s" % (code, out / "run.log"))
		sys.exit(2)

	baseline = FLAGS.get("baseline", "")
	last = RESULTS / ("%s-%s-last.json" % (SCENE, API))
	baseline_path = last if baseline == "last" else (pathlib.Path(baseline) if baseline else None)
	report = [sys.executable, str(ROOT / "scripts" / "bench_report.py"), str(result), "-o", str(out / "report.html")]
	if baseline_path is not None:
		if baseline_path.exists():
			report += ["--baseline", str(baseline_path)]
			if FLAGS.get("fail-on-regression"):
				report += ["--fail-on-regression", FLAGS["fail-on-regression"]]
		else:
			print("no baseline yet (%s): this run becomes it" % baseline_path)
	reported = subprocess.run(report).returncode
	shutil.copy2(result, last)  # the next --baseline=last compares with this run

	print("report: %s" % (out / "report.html"))
	if "open" in FLAGS:
		os.startfile(str(out / "report.html"))
	sys.exit(3 if reported == 3 else code)


if __name__ == "__main__":
	main()
