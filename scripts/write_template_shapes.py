"""Writes launcher/templates/minimal/assets/models/shapes/shapes.glb: the shapes model a new project starts with (the editor's 3D templates
and the starter scene's ground plane use it). The same model as the demo's, from gen_demo_scene.build_glb. Run it again only when that changes."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import gen_demo_scene  # noqa: E402

out = ROOT / "launcher" / "templates" / "minimal" / "assets" / "models" / "shapes" / "shapes.glb"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(gen_demo_scene.build_glb())
print(f"wrote {out} ({out.stat().st_size} bytes)")
