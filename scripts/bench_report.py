#!/usr/bin/env python3
"""Turns the frames of a benchmark run into a report a person can read: one self-contained HTML file (inline SVG charts, no scripts, no downloads).

    python scripts/bench_report.py result.json [-o report.html] [--baseline before.json] [--fail-on-regression 5] [--target 60]
    python scripts/bench_report.py --selftest

result.json is what `wiwa_game --scene <scene> --bench 20 --bench-out result.json` writes (schema "wiwa-bench/1", engine/src/core/bench_recorder.h):
the metadata of the run and one value per frame for the frame time, the render thread, memory, the renderer's counters, every module of the main
thread and every render pass on the GPU. scripts/bench_stress.py runs the game and calls this.

The report has: a verdict and what the run is bound by, the frame time over the run and its distribution, the hotspots (GPU passes, main thread
modules), what grows in the slow frames, the threads, memory, the scene load and levels of detail, the worst frames, and, with --baseline, every
number compared with an earlier run. Exit code: 0, or 3 when --fail-on-regression is given and the average FPS or the 1% low got worse than that
many percent against the baseline.
"""
import argparse
import html
import json
import math
import pathlib
import sys
import xml.etree.ElementTree as ET

SCHEMA = "wiwa-bench/1"
PASS_PALETTE = 6


# ------------------------------------------------------------------------------------------------------------------------- data

def percentile(values, fraction):
	"""The value that `fraction` of the samples stay under (the same rule as the game: index n * fraction of the sorted samples)."""
	if not values:
		return 0.0
	ordered = sorted(values)
	return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def mean(values):
	return sum(values) / len(values) if values else 0.0


class Run:
	"""A result file and everything the report derives from it."""

	def __init__(self, data, path=""):
		if data.get("schema") != SCHEMA:
			raise ValueError("%s is not a %s file (schema: %r)" % (path or "the data", SCHEMA, data.get("schema")))
		self.path = str(path)
		self.meta = data["meta"]
		self.series = data["series"]
		self.modules = data.get("modules", {})
		self.passes = data.get("passes", {})
		self.t = self.series["t"]
		self.frame = self.series["frameMs"]
		self.n = len(self.frame)
		if self.n < 2:
			raise ValueError("%s holds %d frames: nothing to report" % (path or "the data", self.n))
		for name, values in list(self.modules.items()) + list(self.passes.items()) + list(self.series.items()):
			if len(values) != self.n:
				raise ValueError("series %r has %d values for %d frames" % (name, len(values), self.n))

		self.seconds = self.t[-1]
		self.avg_ms = self.seconds * 1000.0 / self.n
		self.fps = 1000.0 / self.avg_ms
		self.p50 = percentile(self.frame, 0.5)
		self.p95 = percentile(self.frame, 0.95)
		self.p99 = percentile(self.frame, 0.99)
		self.p999 = percentile(self.frame, 0.999)
		self.worst = max(self.frame)
		self.low1 = 1000.0 / self.p99
		self.low01 = 1000.0 / self.p999
		self.target = float(self.meta.get("targetFps", 60.0))
		self.slow_cut = self.p95  # the slow frames: the slowest 5%
		self.slow = [i for i, v in enumerate(self.frame) if v >= self.slow_cut]

		self.pass_stats = self._stats(self.passes)
		self.module_stats = self._stats(self.modules)
		self.render_avg = mean(self.series["renderMs"])
		self.render_cpu_avg = mean(self.series["renderCpuMs"])
		self.physics_avg = mean(self.series["physicsMs"])
		self.module_sum_avg = sum(s["avg"] for s in self.module_stats.values())

	def _stats(self, group):
		rows = {}
		total = sum(mean(v) for v in group.values()) or 1.0
		for name, values in group.items():
			avg = mean(values)
			slow = mean([values[i] for i in self.slow]) if self.slow else avg
			rows[name] = {"avg": avg, "p95": percentile(values, 0.95), "max": max(values), "share": avg / total, "slow": slow, "grow": slow - avg}
		return rows

	def peak(self, key):
		return max(self.series[key])

	def lod_share(self):
		counts = [mean(self.series["lod%d" % i]) for i in range(4)]
		total = sum(counts)
		return [c / total if total else 0.0 for c in counts], counts


def load(path):
	with open(path, encoding="utf-8") as handle:
		return Run(json.load(handle), path)


# ------------------------------------------------------------------------------------------------------------------------- text

def num(value, decimals=1):
	return ("{:,.%df}" % decimals).format(value)


def ms(value):
	return num(value, 2 if value < 10 else 1) + " ms"


def pct_change(new, old):
	return (new - old) / old * 100.0 if old else 0.0


def esc(text):
	return html.escape(str(text), quote=True)


def findings(run, base=None):
	"""What the numbers say, as sentences: (level, text) with level one of good, info, warn, bad."""
	out = []
	if run.fps >= run.target:
		out.append(("good", "Averages %s FPS, %s over the %s FPS target." % (num(run.fps), num(run.fps - run.target), num(run.target, 0))))
	else:
		out.append(("bad", "Averages %s FPS, %s under the %s FPS target (%s needed per frame, %s taken)." % (
			num(run.fps), num(run.target - run.fps), num(run.target, 0), ms(1000.0 / run.target), ms(run.avg_ms))))

	# what the run is bound by: the main thread, the render thread's CPU work, or the GPU
	frame = run.avg_ms
	if run.module_sum_avg >= 0.8 * frame:
		top = max(run.module_stats.items(), key=lambda kv: kv[1]["avg"]) if run.module_stats else None
		out.append(("warn", "Bound by the main thread: its modules take %s of the %s frame%s." % (
			ms(run.module_sum_avg), ms(frame), (", mostly %s (%s)" % (top[0], ms(top[1]["avg"]))) if top else "")))
	elif run.render_avg >= 0.85 * frame:
		gpu = sum(s["avg"] for s in run.pass_stats.values())
		if run.pass_stats and gpu >= 0.7 * run.render_avg:
			top = max(run.pass_stats.items(), key=lambda kv: kv[1]["avg"])
			out.append(("warn", "Bound by the GPU: the render passes add up to %s of the render thread's %s frame, %s of it in %s." % (
				ms(gpu), ms(run.render_avg), ms(top[1]["avg"]), top[0])))
		elif run.render_cpu_avg >= 0.5 * run.render_avg:
			out.append(("warn", "Bound by the render thread's CPU work: %s of its %s frame is spent recording and submitting (draw calls, culling)." % (
				ms(run.render_cpu_avg), ms(run.render_avg))))
		else:
			out.append(("info", "The render thread paces the frame (%s of %s) but neither its CPU work (%s) nor the passes it timed explain it." % (
				ms(run.render_avg), ms(frame), ms(run.render_cpu_avg))))
	else:
		out.append(("info", "Neither thread fills the frame (main thread modules %s, render thread %s of %s): something else waits, such as the frame limiter or vsync." % (
			ms(run.module_sum_avg), ms(run.render_avg), ms(frame))))

	if run.pass_stats:
		top = sorted(run.pass_stats.items(), key=lambda kv: -kv[1]["avg"])[:3]
		out.append(("info", "Top GPU passes: " + ", ".join("%s %s (%s%%)" % (n, ms(s["avg"]), num(s["share"] * 100, 0)) for n, s in top) + "."))
	if run.module_stats:
		top = sorted(run.module_stats.items(), key=lambda kv: -kv[1]["avg"])[:3]
		out.append(("info", "Top main thread modules: " + ", ".join("%s %s" % (n, ms(s["avg"])) for n, s in top) + "."))

	ratio = run.p99 / run.p50 if run.p50 else 1.0
	if ratio >= 1.5:
		out.append(("warn", "Uneven frames: the 1%% low (%s FPS) is %s%% below the median frame rate. The slow frames grow most in: %s." % (
			num(run.low1), num((1 - 1 / ratio) * 100, 0), growth_text(run))))
	else:
		out.append(("good", "Even frames: the 1%% low is %s FPS, within %s%% of the median." % (num(run.low1), num((1 - 1 / ratio) * 100, 0))))
	if run.worst >= 3 * run.p50:
		out.append(("warn", "A hitch of %s at %s s (median frame %s)." % (ms(run.worst), num(run.t[run.frame.index(run.worst)], 1), ms(run.p50))))

	vram = run.series["vramMb"]
	if len(vram) > 20 and vram[0] > 0:
		growth = pct_change(mean(vram[-10:]), mean(vram[:10]))
		if growth > 5.0:
			out.append(("warn", "VRAM grew %s%% over the run (%s to %s MB): look for a leak." % (num(growth, 0), num(mean(vram[:10]), 0), num(mean(vram[-10:]), 0))))
	ram = run.series["ramMb"]
	if len(ram) > 20 and ram[0] > 0:
		growth = pct_change(mean(ram[-10:]), mean(ram[:10]))
		if growth > 5.0:
			out.append(("warn", "Process memory grew %s%% over the run (%s to %s MB)." % (num(growth, 0), num(mean(ram[:10]), 0), num(mean(ram[-10:]), 0))))
	share, counts = run.lod_share()
	if sum(counts) > 0:
		out.append(("info", "Levels of detail: %s%% of the drawn primitives were full detail, %s%% coarser." % (num(share[0] * 100, 0), num((1 - share[0]) * 100, 0))))

	if base is not None:
		d = pct_change(run.fps, base.fps)
		level = "good" if d > 2 else ("bad" if d < -5 else "info")
		out.append((level, "Against the baseline: average FPS %s%s%% (%s against %s), 1%% low %s%%." % (
			"+" if d >= 0 else "", num(d), num(run.fps), num(base.fps), ("+" if run.low1 >= base.low1 else "") + num(pct_change(run.low1, base.low1)))))
	return out


def growth_text(run):
	rows = [(n, s["grow"]) for n, s in list(run.pass_stats.items()) + list(run.module_stats.items()) if s["grow"] > 0.05]
	rows.sort(key=lambda r: -r[1])
	if not rows:
		return "nothing the report can name (the cost is outside the timed passes and modules)"
	return ", ".join("%s (+%s)" % (n, ms(g)) for n, g in rows[:3])


# ------------------------------------------------------------------------------------------------------------------------- charts

def nice_max(value):
	if value <= 0:
		return 1.0
	exponent = math.floor(math.log10(value))
	for step in (1, 1.5, 2, 3, 4, 5, 6, 8, 10):
		if step * 10 ** exponent >= value:
			return step * 10 ** exponent
	return 10 ** (exponent + 1)


def ticks(top, count=4):
	return [top * i / count for i in range(count + 1)]


def decimate(xs, ys, buckets):
	"""At most 2 * buckets points that keep the peaks: the lowest and the highest sample of every run of frames."""
	if len(xs) <= buckets * 2:
		return list(zip(xs, ys))
	out = []
	size = len(xs) / buckets
	for b in range(buckets):
		lo, hi = int(b * size), max(int((b + 1) * size), int(b * size) + 1)
		window = range(lo, min(hi, len(xs)))
		low = min(window, key=lambda i: ys[i])
		high = max(window, key=lambda i: ys[i])
		for i in sorted({low, high}):
			out.append((xs[i], ys[i]))
	return out


class Chart:
	"""An SVG with a plot area, a left axis and a bottom axis; the series are drawn by the caller through x() and y()."""

	def __init__(self, width, height, xmax, ymax, xlabel="", ylabel="", title="", xmin=0.0, left=52, right=12, top=10, bottom=34):
		self.w, self.h, self.xmin, self.xmax, self.ymax = width, height, xmin, xmax, ymax
		self.l, self.r, self.t, self.b = left, right, top, bottom
		self.parts = []
		self.xlabel, self.ylabel, self.title = xlabel, ylabel, title

	def x(self, value):
		span = (self.xmax - self.xmin) or 1.0
		return self.l + (value - self.xmin) / span * (self.w - self.l - self.r)

	def y(self, value):
		return self.h - self.b - min(value, self.ymax) / (self.ymax or 1.0) * (self.h - self.t - self.b)

	def add(self, markup):
		self.parts.append(markup)

	def axes(self, yfmt=None, xfmt=lambda v: num(v, 0), xticks=5):
		yfmt = yfmt or (lambda v: num(v, 0 if self.ymax >= 10 else 1))
		out = []
		for value in ticks(self.ymax, 4):
			y = self.y(value)
			out.append('<line class="grid" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>' % (self.l, y, self.w - self.r, y))
			out.append('<text class="tick" x="%.1f" y="%.1f" text-anchor="end">%s</text>' % (self.l - 6, y + 3.5, esc(yfmt(value))))
		for i in range(xticks + 1):
			value = self.xmin + (self.xmax - self.xmin) * i / xticks
			x = self.x(value)
			out.append('<text class="tick" x="%.1f" y="%.1f" text-anchor="middle">%s</text>' % (x, self.h - self.b + 15, esc(xfmt(value))))
		if self.xlabel:
			out.append('<text class="axis" x="%.1f" y="%.1f" text-anchor="middle">%s</text>' % ((self.l + self.w - self.r) / 2, self.h - 3, esc(self.xlabel)))
		if self.ylabel:
			out.append('<text class="axis" transform="rotate(-90)" x="%.1f" y="12" text-anchor="middle">%s</text>' % (-(self.t + self.h - self.b) / 2, esc(self.ylabel)))
		return "".join(out)

	def render(self):
		return ('<svg viewBox="0 0 %d %d" role="img" aria-label="%s" preserveAspectRatio="xMidYMid meet"><title>%s</title>%s%s</svg>'
			% (self.w, self.h, esc(self.title), esc(self.title), self.axes(), "".join(self.parts)))


def path(points, chart):
	return "M" + " L".join("%.1f %.1f" % (chart.x(x), chart.y(y)) for x, y in points)


def line_chart(series, title, ylabel, xmax, ymax=None, refs=(), width=900, height=250, xlabel="time (s)"):
	"""series: [(css class, xs, ys, dashed)]; refs: [(value, label)] horizontal reference lines."""
	top = ymax or nice_max(max((max(ys) for _, _, ys, _ in series if ys), default=1.0) * 1.05)
	chart = Chart(width, height, xmax, top, xlabel, ylabel, title)
	for value, label in refs:
		if value <= top:
			chart.add('<line class="ref" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/><text class="reflabel" x="%.1f" y="%.1f" text-anchor="end">%s</text>'
				% (chart.l, chart.y(value), width - chart.r, chart.y(value), width - chart.r - 4, chart.y(value) - 4, esc(label)))
	for css, xs, ys, dashed in series:
		points = decimate(xs, ys, 450)
		chart.add('<path class="line %s%s" d="%s"/>' % (css, " dashed" if dashed else "", path(points, chart)))
	return chart.render()


def histogram(run, base, width=440, height=250):
	top_ms = max(run.p999 * 1.15, run.p99 * 1.3, 1.0)
	if base is not None:
		top_ms = max(top_ms, base.p999 * 1.15)
	bins = 40
	step = top_ms / bins

	def count(values):
		c = [0] * (bins + 1)
		for v in values:
			c[min(int(v / step), bins)] += 1
		return [x / len(values) * 100.0 for x in c]
	mine = count(run.frame)
	other = count(base.frame) if base is not None else None
	ymax = nice_max(max(mine + (other or [])) * 1.1)
	chart = Chart(width, height, top_ms, ymax, "frame time (ms)", "% of frames", "Frame time distribution")
	bar = (width - chart.l - chart.r) / bins * 0.86
	for i, value in enumerate(mine):
		x = chart.x(i * step)
		cls = "bar over" if (i * step) > 1000.0 / run.target else "bar"
		chart.add('<rect class="%s" x="%.1f" y="%.1f" width="%.1f" height="%.1f"><title>%s to %s ms: %s%% of the frames</title></rect>' % (
			cls, x, chart.y(value), bar, max(chart.y(0) - chart.y(value), 0), num(i * step, 1), num((i + 1) * step, 1), num(value, 2)))
	if other is not None:
		points = [(i * step + step / 2, v) for i, v in enumerate(other)]
		chart.add('<path class="line base dashed" d="%s"/>' % path(points, chart))
	for value, label in ((run.p50, "p50"), (run.p99, "p99")):
		if value <= top_ms:
			chart.add('<line class="marker" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/><text class="reflabel" x="%.1f" y="%.1f">%s</text>' % (
				chart.x(value), chart.t, chart.x(value), chart.y(0), chart.x(value) + 3, chart.t + 10, label))
	return chart.render()


def hbars(rows, unit_label, width=440, title="", row_height=24, max_rows=10):
	"""rows: [(name, avg, p95, share)] sorted by the caller; a bar for the average, a tick for the p95."""
	rows = rows[:max_rows]
	if not rows:
		return '<p class="empty">Nothing was timed.</p>'
	top = nice_max(max(max(r[2], r[1]) for r in rows) * 1.05)
	label_w, value_w = 150, 130
	plot = width - label_w - value_w
	height = row_height * len(rows) + 22
	out = ['<svg viewBox="0 0 %d %d" role="img" aria-label="%s" preserveAspectRatio="xMinYMin meet"><title>%s</title>' % (width, height, esc(title), esc(title))]
	for i, (name, avg, p95, share) in enumerate(rows):
		y = i * row_height + 4
		out.append('<text class="rowlabel" x="%d" y="%.1f" text-anchor="end">%s</text>' % (label_w - 8, y + 14, esc(name if len(name) <= 22 else name[:21] + "...")))
		out.append('<rect class="bar s%d" x="%d" y="%.1f" width="%.1f" height="14" rx="2"><title>%s: average %s, p95 %s</title></rect>' % (
			i % PASS_PALETTE, label_w, y + 3, max(avg / top * plot, 1.0), esc(name), ms(avg), ms(p95)))
		out.append('<line class="p95" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"/>' % (label_w + p95 / top * plot, y + 1, label_w + p95 / top * plot, y + 21))
		out.append('<text class="value" x="%.1f" y="%.1f">%s  %s%%</text>' % (label_w + plot + 8, y + 14, ms(avg), num(share * 100, 0)))
	out.append('<text class="tick" x="%d" y="%d">bar: average   |: 95th percentile   %s</text>' % (label_w, height - 4, esc(unit_label)))
	out.append("</svg>")
	return "".join(out)


def stacked(xs, groups, title, ylabel, xmax, width=900, height=230):
	"""Stacked area: groups [(css, ys)] bottom to top, all with the same length as xs."""
	totals = [sum(g[1][i] for g in groups) for i in range(len(xs))]
	top = nice_max(max(totals) * 1.05) if totals else 1.0
	chart = Chart(width, height, xmax, top, "time (s)", ylabel, title)
	step = max(len(xs) // 300, 1)
	idx = list(range(0, len(xs), step))
	lower = [0.0] * len(idx)
	for css, ys in groups:
		upper = [lower[k] + ys[i] for k, i in enumerate(idx)]
		forward = " L".join("%.1f %.1f" % (chart.x(xs[i]), chart.y(upper[k])) for k, i in enumerate(idx))
		back = " L".join("%.1f %.1f" % (chart.x(xs[i]), chart.y(lower[k])) for k, i in reversed(list(enumerate(idx))))
		chart.add('<path class="area %s" d="M%s L%s Z"/>' % (css, forward, back))
		lower = upper
	return chart.render()


# ------------------------------------------------------------------------------------------------------------------------- page

CSS = """
:root{--bg:#f6f7f9;--card:#fff;--ink:#1c2330;--muted:#5d6878;--line:#d9dee6;--grid:#e8ebf0;--good:#1a7f4b;--warn:#b26a00;--bad:#c62f3a;--info:#2d6cdf;
--c0:#2d6cdf;--c1:#e0701f;--c2:#1a9a6c;--c3:#a64ad4;--c4:#c9a30f;--c5:#d4467a;--base:#8a93a3;--bar:#7da3ea;--over:#e48a8f}
@media (prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1a1f28;--ink:#e6eaf1;--muted:#97a1b1;--line:#2e3543;--grid:#262c38;--good:#4cc38a;--warn:#e0a13a;--bad:#ef6671;--info:#6c9cf5;
--c0:#6c9cf5;--c1:#f0954f;--c2:#4cc38a;--c3:#c486ea;--c4:#e2c24a;--c5:#ee7aa3;--base:#7c8697;--bar:#4f78c4;--over:#b55a62}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px 56px}h1{font-size:26px;margin:0 0 4px}h2{font-size:18px;margin:0 0 4px}
.sub{color:var(--muted);margin:0 0 16px}.chips{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0 18px}
.chip{background:var(--card);border:1px solid var(--line);border-radius:999px;padding:2px 10px;font-size:13px;color:var(--muted)}.chip b{color:var(--ink);font-weight:600}
.verdict{border-radius:10px;padding:14px 16px;margin:0 0 18px;font-weight:600;border:1px solid var(--line);background:var(--card);border-left:6px solid var(--info)}
.verdict.good{border-left-color:var(--good)}.verdict.bad{border-left-color:var(--bad)}.verdict.warn{border-left-color:var(--warn)}
.verdict small{display:block;font-weight:400;color:var(--muted);margin-top:2px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-bottom:18px}
.kpi{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 14px}.kpi .v{font-size:24px;font-weight:650;font-variant-numeric:tabular-nums}
.kpi .l{color:var(--muted);font-size:13px}.kpi .d{font-size:13px;font-variant-numeric:tabular-nums}
.good{color:var(--good)}.bad{color:var(--bad)}.warn{color:var(--warn)}.info{color:var(--info)}.muted{color:var(--muted)}
section.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin-bottom:16px}
section.card p.note{margin:0 0 10px;color:var(--muted);font-size:14px}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:16px}
@media (max-width:820px){.grid2{grid-template-columns:1fr}}
ul.findings{list-style:none;margin:0;padding:0}ul.findings li{padding:5px 0 5px 22px;position:relative;border-top:1px solid var(--grid)}ul.findings li:first-child{border-top:0}
ul.findings li::before{content:"";position:absolute;left:4px;top:13px;width:9px;height:9px;border-radius:50%;background:currentColor}
ul.findings li span{color:var(--ink)}svg{width:100%;height:auto;display:block}
svg text{fill:var(--muted);font:11px system-ui,sans-serif}svg .rowlabel{fill:var(--ink);font-size:12px}svg .value{fill:var(--ink);font-size:12px}
svg .grid{stroke:var(--grid);stroke-width:1}svg .ref{stroke:var(--warn);stroke-width:1;stroke-dasharray:4 4;opacity:.7}svg .reflabel{fill:var(--warn)}
svg .marker{stroke:var(--muted);stroke-width:1;stroke-dasharray:2 3}svg .p95{stroke:var(--ink);stroke-width:2}
svg .line{fill:none;stroke-width:1.4;stroke-linejoin:round}svg .line.dashed{stroke-dasharray:5 3;stroke-width:1.3}
svg .s0{stroke:var(--c0);fill:var(--c0)}svg .s1{stroke:var(--c1);fill:var(--c1)}svg .s2{stroke:var(--c2);fill:var(--c2)}svg .s3{stroke:var(--c3);fill:var(--c3)}
svg .s4{stroke:var(--c4);fill:var(--c4)}svg .s5{stroke:var(--c5);fill:var(--c5)}svg .line.s0,svg .line.s1,svg .line.s2,svg .line.s3,svg .line.s4,svg .line.s5{fill:none}
svg .line.base{stroke:var(--base);fill:none}svg .bar{fill:var(--bar)}svg .bar.over{fill:var(--over)}svg .bar.s0{fill:var(--c0)}svg .bar.s1{fill:var(--c1)}
svg .bar.s2{fill:var(--c2)}svg .bar.s3{fill:var(--c3)}svg .bar.s4{fill:var(--c4)}svg .bar.s5{fill:var(--c5)}svg .area{stroke:none;opacity:.85}
.legend{display:flex;flex-wrap:wrap;gap:4px 16px;font-size:13px;color:var(--muted);margin:2px 0 6px}.legend i{display:inline-block;width:18px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}
.legend i.s0{background:var(--c0)}.legend i.s1{background:var(--c1)}.legend i.s2{background:var(--c2)}.legend i.s3{background:var(--c3)}.legend i.base{background:var(--base)}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{text-align:right;padding:4px 8px;border-top:1px solid var(--grid);font-variant-numeric:tabular-nums}
th:first-child,td:first-child{text-align:left}th{color:var(--muted);font-weight:500;border-top:0;border-bottom:1px solid var(--line)}
.empty{color:var(--muted)}footer{color:var(--muted);font-size:13px;margin-top:24px}
"""


def kpi(label, value, detail="", cls=""):
	return '<div class="kpi"><div class="l">%s</div><div class="v %s">%s</div><div class="d muted">%s</div></div>' % (esc(label), cls, value, detail)


def delta_html(new, old, higher_is_better, unit="", decimals=1, threshold=2.0):
	change = pct_change(new, old)
	good = (change > 0) == higher_is_better
	cls = "muted" if abs(change) < threshold else ("good" if good else "bad")
	return '<span class="%s">%s%s%%%s</span>' % (cls, "+" if change >= 0 else "", num(change, decimals), unit)


def thread_table(run):
	def row(name, values):
		return "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (esc(name), ms(mean(values)), ms(percentile(values, 0.95)), ms(max(values)))
	rows = [row("Frame (main thread, real time)", run.frame), row("Render thread frame", run.series["renderMs"]),
		row("Render thread CPU (recording)", run.series["renderCpuMs"]), row("Physics step", run.series["physicsMs"])]
	return "<table><tr><th>Thread</th><th>average</th><th>p95</th><th>max</th></tr>%s</table>" % "".join(rows)


def worst_frames(run, count=10):
	order = sorted(range(run.n), key=lambda i: -run.frame[i])[:count]
	rows = []
	for i in sorted(order):
		gpu = max(run.passes.items(), key=lambda kv: kv[1][i]) if run.passes else None
		cpu = max(run.modules.items(), key=lambda kv: kv[1][i]) if run.modules else None
		rows.append("<tr><td>%s s</td><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
			num(run.t[i], 2), i, ms(run.frame[i]),
			(esc(gpu[0]) + " " + ms(gpu[1][i])) if gpu else "-", (esc(cpu[0]) + " " + ms(cpu[1][i])) if cpu else "-"))
	return "<table><tr><th>at</th><th>frame</th><th>time</th><th>longest GPU pass</th><th>longest module</th></tr>%s</table>" % "".join(rows)


def growth_table(run):
	rows = [(n, s) for n, s in list(run.pass_stats.items()) + list(run.module_stats.items())]
	rows = [r for r in rows if r[1]["grow"] > 0.02]
	rows.sort(key=lambda r: -r[1]["grow"])
	if not rows:
		return '<p class="empty">The slow frames are not slower in any timed pass or module.</p>'
	kind = lambda name: "GPU pass" if name in run.pass_stats else "module"
	body = "".join("<tr><td>%s <span class='muted'>(%s)</span></td><td>%s</td><td>%s</td><td class='warn'>+%s</td></tr>" % (esc(n), kind(n), ms(s["avg"]), ms(s["slow"]), ms(s["grow"])) for n, s in rows[:8])
	return "<table><tr><th>Where</th><th>all frames</th><th>slowest 5%%</th><th>grows by</th></tr>%s</table>" % body


def compare_table(run, base):
	def row(label, new, old, higher_is_better, fmt):
		return "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (label, fmt(old), fmt(new), delta_html(new, old, higher_is_better))
	f1 = lambda v: num(v, 1)
	rows = [row("Average FPS", run.fps, base.fps, True, f1), row("1% low FPS", run.low1, base.low1, True, f1), row("0.1% low FPS", run.low01, base.low01, True, f1),
		row("Average frame (ms)", run.avg_ms, base.avg_ms, False, lambda v: num(v, 2)), row("p99 frame (ms)", run.p99, base.p99, False, lambda v: num(v, 2)),
		row("Worst frame (ms)", run.worst, base.worst, False, lambda v: num(v, 2)), row("Render thread (ms)", run.render_avg, base.render_avg, False, lambda v: num(v, 2)),
		row("Peak VRAM (MB)", run.peak("vramMb"), base.peak("vramMb"), False, lambda v: num(v, 0)), row("Peak RAM (MB)", run.peak("ramMb"), base.peak("ramMb"), False, lambda v: num(v, 0)),
		row("Draw calls (peak)", run.peak("drawCalls"), base.peak("drawCalls"), False, lambda v: num(v, 0))]
	table = "<table><tr><th>Metric</th><th>baseline</th><th>this run</th><th>change</th></tr>%s</table>" % "".join(rows)

	changes = []
	for group, label in ((run.pass_stats, "GPU pass"), (run.module_stats, "module")):
		other = base.pass_stats if group is run.pass_stats else base.module_stats
		for name, stats in group.items():
			old = other.get(name, {}).get("avg", 0.0)
			if max(stats["avg"], old) >= 0.05:
				changes.append((abs(stats["avg"] - old), name, label, old, stats["avg"]))
	changes.sort(reverse=True)
	detail = "".join("<tr><td>%s <span class='muted'>(%s)</span></td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
		esc(n), l, ms(o), ms(v), ('<span class="%s">%s%s</span>' % ("bad" if v > o else "good", "+" if v >= o else "-", ms(abs(v - o))))) for _, n, l, o, v in changes[:8])
	if detail:
		table += "<h2 style='margin-top:14px'>What changed most</h2><table><tr><th>Where</th><th>baseline</th><th>this run</th><th>difference</th></tr>%s</table>" % detail
	return table


def details_table(run):
	m = run.meta
	pairs = [("Scene", m.get("scene", "")), ("Graphics API", m.get("api", "")), ("Adapter", m.get("adapter", "")), ("Build", m.get("config", "")), ("Vsync", "on" if m.get("vsync") else "off"),
		("Measured for", "%s s (%d frames)" % (num(run.seconds), run.n)), ("Instances", num(m.get("sceneInstances", 0), 0)), ("Lights", num(m.get("sceneLights", 0), 0)),
		("Meshes / textures uploaded", "%s / %s" % (num(m.get("meshes", 0), 0), num(m.get("textures", 0), 0))), ("GPU resource cache", "%s MB" % num(m.get("cacheBytes", 0) / 1048576.0, 0)),
		("Shadow views", m.get("shadowViews", 0)), ("Date", m.get("date", ""))]
	pairs += sorted(m.get("extra", {}).items())
	return "<table>%s</table>" % "".join("<tr><td>%s</td><td>%s</td></tr>" % (esc(k), esc(v)) for k, v in pairs if str(v) != "")


def build_report(run, base=None, title=None):
	label = run.meta.get("label") or pathlib.Path(run.path).stem or "benchmark"
	title = title or "Benchmark: %s" % label
	xmax = run.t[-1]
	notes = findings(run, base)
	bad = any(level == "bad" for level, _ in notes)
	warn = any(level == "warn" for level, _ in notes)
	headline = notes[0][1]
	vclass = "bad" if bad else ("warn" if warn else "good")
	vtext = ("Target missed" if run.fps < run.target else ("Target reached" if not warn else "Target reached, with caveats"))

	chips = "".join('<span class="chip">%s <b>%s</b></span>' % (esc(k), esc(v)) for k, v in (
		("scene", run.meta.get("scene", "?")), ("api", run.meta.get("api", "?")), ("gpu", run.meta.get("adapter", "?")), ("build", run.meta.get("config", "?")),
		("instances", num(run.meta.get("sceneInstances", 0), 0)), ("lights", num(run.meta.get("sceneLights", 0), 0)), ("date", run.meta.get("date", "?"))))

	def k(value, ref, higher):
		return delta_html(value, ref, higher) + " vs baseline" if base is not None else ""
	cards = "".join([
		kpi("Average FPS", num(run.fps), (k(run.fps, base.fps, True) if base else "target %s" % num(run.target, 0)), "good" if run.fps >= run.target else "bad"),
		kpi("1% low FPS", num(run.low1), (k(run.low1, base.low1, True) if base else "p99 frame %s" % ms(run.p99))),
		kpi("0.1% low FPS", num(run.low01), (k(run.low01, base.low01, True) if base else "p99.9 frame %s" % ms(run.p999))),
		kpi("Median frame", ms(run.p50), (k(run.p50, base.p50, False) if base else "p95 %s" % ms(run.p95))),
		kpi("Worst frame", ms(run.worst), "at %s s" % num(run.t[run.frame.index(run.worst)], 1)),
		kpi("Peak VRAM", num(run.peak("vramMb"), 0) + " MB", "RAM %s MB" % num(run.peak("ramMb"), 0)),
	])

	fps_refs = [(1000.0 / run.target, "%s FPS" % num(run.target, 0)), (1000.0 / (run.target / 2), "%s FPS" % num(run.target / 2, 0))]
	timeline_series = [("s0", run.t, run.frame, False)]
	legend = '<div class="legend"><span><i class="s0"></i>this run</span>%s</div>'
	if base is not None:
		timeline_series.append(("base", [x * (xmax / base.t[-1]) for x in base.t], base.frame, True))
		legend = legend % '<span><i class="base"></i>baseline (stretched to this run\'s length)</span>'
	else:
		legend = legend % ""
	timeline = line_chart(timeline_series, "Frame time over the run", "ms", xmax, refs=fps_refs, ymax=nice_max(min(run.worst * 1.05, max(run.p999 * 2, 1000.0 / run.target * 2.2))))

	gpu_rows = sorted(((n, s["avg"], s["p95"], s["share"]) for n, s in run.pass_stats.items()), key=lambda r: -r[1])
	cpu_total = sum(s["avg"] for s in run.module_stats.values()) or 1.0
	cpu_rows = sorted(((n, s["avg"], s["p95"], s["avg"] / cpu_total) for n, s in run.module_stats.items()), key=lambda r: -r[1])

	memory = line_chart([("s0", run.t, run.series["vramMb"], False)], "VRAM over the run", "MB", xmax, width=440, height=200) + "<p class='note'>VRAM used by the process (video memory)</p>"
	memory += line_chart([("s1", run.t, run.series["ramMb"], False)], "Process memory over the run", "MB", xmax, width=440, height=200) + "<p class='note'>Process memory (RAM)</p>"

	counters = line_chart([("s0", run.t, run.series["drawCalls"], False), ("s1", run.t, run.series["visibleInstances"], False)], "Draw calls and visible instances", "count", xmax, height=220)
	lods = stacked(run.t, [("s%d" % (3 - i if i else 2), run.series["lod%d" % i]) for i in range(4)], "Primitives drawn per level of detail", "primitives", xmax)
	lod_legend = '<div class="legend"><span><i class="s2"></i>full detail</span><span><i class="s1"></i>level 1</span><span><i class="s0"></i>level 2</span><span><i class="s3"></i>level 3</span></div>'
	lods = lods.replace('class="area s3"', 'class="area s1"', 1) if False else lods

	sections = [
		'<section class="card"><h2>What the numbers say</h2><ul class="findings">%s</ul></section>' % "".join('<li class="%s"><span>%s</span></li>' % (lv, esc(tx)) for lv, tx in notes),
		'<section class="card"><h2>Frame time over the run</h2><p class="note">Every frame, peaks kept. The dashed lines are the frame times of %s and %s FPS; spikes above them are the frames that were felt.</p>%s%s</section>' % (
			num(run.target, 0), num(run.target / 2, 0), legend, timeline),
		'<div class="grid2"><section class="card"><h2>Distribution</h2><p class="note">How the frames spread. Red bars are slower than the target.</p>%s</section>'
		'<section class="card"><h2>Threads</h2><p class="note">Where each thread spends its time.</p>%s</section></div>' % (histogram(run, base), thread_table(run)),
		'<div class="grid2"><section class="card"><h2>GPU hotspots</h2><p class="note">Render passes, by the time they take on the GPU.</p>%s</section>'
		'<section class="card"><h2>Main thread hotspots</h2><p class="note">Modules, by the time they take on the main thread.</p>%s</section></div>' % (
			hbars(gpu_rows, "ms per frame", title="GPU time per render pass"), hbars(cpu_rows, "ms per frame", title="Main thread time per module")),
		'<section class="card"><h2>What grows in the slow frames</h2><p class="note">The slowest 5%% of the frames (%s or more) against all frames: the passes and modules that take longer when the frame is slow are where to look first.</p>%s</section>' % (
			ms(run.slow_cut), growth_table(run)),
		'<div class="grid2"><section class="card"><h2>Memory</h2>%s</section>'
		'<section class="card"><h2>Worst frames</h2><p class="note">The ten longest frames and what took the longest in each.</p>%s</section></div>' % (memory, worst_frames(run)),
		'<section class="card"><h2>The scene</h2><p class="note">What the renderer drew while the camera moved.</p>%s%s%s</section>' % (counters, lods, lod_legend),
	]
	if base is not None:
		sections.insert(1, '<section class="card"><h2>Against the baseline</h2><p class="note">%s</p>%s</section>' % (
			esc("Baseline: %s (%s FPS average, %s)." % (base.meta.get("label") or base.path, num(base.fps), base.meta.get("date", ""))), compare_table(run, base)))
	sections.append('<section class="card"><h2>The run</h2>%s</section>' % details_table(run))

	return ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>%s</title><style>%s</style></head>'
		'<body><main><h1>%s</h1><p class="sub">%s</p><div class="chips">%s</div>'
		'<div class="verdict %s">%s<small>%s</small></div><div class="cards">%s</div>%s'
		'<footer>Made by scripts/bench_report.py from %s (%s). Charts are inline SVG: this file needs nothing else.</footer></main></body></html>') % (
		esc(title), CSS, esc(title), esc("%s frames over %s s on %s, %s." % (num(run.n, 0), num(run.seconds), run.meta.get("adapter", "an unknown GPU"), run.meta.get("api", "?"))), chips,
		vclass, esc(vtext), esc(headline), cards, "".join(sections), esc(pathlib.Path(run.path).name or "the data"), SCHEMA)


def regressed(run, base, threshold):
	"""The key numbers that got worse than threshold percent: [(name, change%)]."""
	out = []
	for name, new, old in (("average FPS", run.fps, base.fps), ("1% low FPS", run.low1, base.low1)):
		change = pct_change(new, old)
		if change < -threshold:
			out.append((name, change))
	return out


def summary_text(run, base=None):
	lines = ["%s: %s frames, %s FPS average (%s), 1%% low %s, 0.1%% low %s, worst %s" % (
		run.meta.get("label") or "run", num(run.n, 0), num(run.fps), ms(run.avg_ms), num(run.low1), num(run.low01), ms(run.worst))]
	if base is not None:
		lines.append("baseline: %s FPS average, 1%% low %s -> average %s%%, 1%% low %s%%" % (
			num(base.fps), num(base.low1), num(pct_change(run.fps, base.fps)), num(pct_change(run.low1, base.low1))))
	return "\n".join(lines)


# ------------------------------------------------------------------------------------------------------------------------- self test

def synthetic(frames=1200, base_ms=14.0, spike_every=40, spike_ms=40.0, vram_growth=0.0, label="synthetic", gpu_scale=1.0):
	"""A made up run in the file format: a steady frame time with periodic spikes caused by one pass."""
	import random
	rng = random.Random(7)
	t, frame, render, cpu, vram, ram, draws, visible, lods = [], [], [], [], [], [], [], [], [[], [], [], []]
	passes = {"Forward": [], "Shadows": [], "Bloom": [], "AO": []}
	modules = {"Scene": [], "Scripting": [], "Animation": []}
	clock = 0.0
	for i in range(frames):
		spike = spike_every and i % spike_every == spike_every - 1
		ms_value = base_ms * (1 + rng.uniform(-0.04, 0.04)) + (spike_ms if spike else 0.0)
		clock += ms_value / 1000.0
		t.append(clock)
		frame.append(ms_value)
		render.append(ms_value * 0.97)
		cpu.append(3.0 + rng.uniform(0, 0.4))
		vram.append(1000.0 + vram_growth * i / frames * 1000.0)
		ram.append(800.0)
		draws.append(2500 + 400 * math.sin(i / 90.0))
		visible.append(1800 + 300 * math.sin(i / 90.0))
		for level, share in enumerate((0.55, 0.15, 0.2, 0.1)):
			lods[level].append(draws[-1] * share)
		passes["Forward"].append((6.0 + rng.uniform(0, 0.3)) * gpu_scale + (26.0 if spike else 0.0))
		passes["Shadows"].append(3.5 * gpu_scale)
		passes["Bloom"].append(0.8)
		passes["AO"].append(1.5)
		modules["Scene"].append(1.0)
		modules["Scripting"].append(0.6 + (2.0 if spike else 0.0))
		modules["Animation"].append(0.8)
	return {"schema": SCHEMA, "meta": {"label": label, "scene": "synthetic", "api": "vulkan", "adapter": "Test GPU", "config": "Release", "targetFps": 60.0, "vsync": False,
		"sceneInstances": 5000, "sceneLights": 300, "meshes": 40, "textures": 120, "cacheBytes": 200 << 20, "shadowViews": 6, "date": "2026-01-01T00:00:00", "extra": {"scale": "1"}},
		"series": {"t": t, "frameMs": frame, "renderMs": render, "renderCpuMs": cpu, "physicsMs": [0.4] * frames, "ramMb": ram, "vramMb": vram, "drawCalls": draws,
			"visibleInstances": visible, "lod0": lods[0], "lod1": lods[1], "lod2": lods[2], "lod3": lods[3]},
		"modules": modules, "passes": passes}


def selftest():
	failures = []

	def check(condition, what):
		if not condition:
			failures.append(what)

	# the percentile rule is the game's
	check(percentile(list(range(1, 101)), 0.99) == 100 and percentile(list(range(1, 101)), 0.5) == 51, "percentile index rule")
	check(percentile([], 0.5) == 0.0, "percentile of nothing")

	good = Run(synthetic(), "good.json")
	slow = Run(synthetic(base_ms=19.0, label="slow", gpu_scale=1.6, vram_growth=0.2), "slow.json")
	check(abs(good.fps - 1000.0 / good.avg_ms) < 1e-6 and 60 < good.fps < 75, "average FPS %s" % good.fps)
	check(good.worst > 40 and good.low1 < good.fps, "spikes pull the 1%% low under the average (%s, %s)" % (good.low1, good.fps))
	check(any("Uneven frames" in t for _, t in findings(good)), "the spikes are reported as uneven frames")
	check("Forward" in growth_text(good), "the spiking pass is named: %s" % growth_text(good))
	check(any(lv == "bad" for lv, _ in findings(slow)) and any("VRAM grew" in t for _, t in findings(slow)), "the slow run misses the target and leaks")
	check(regressed(slow, good, 5.0) and not regressed(good, good, 5.0), "regression detection")

	page = build_report(slow, good)
	check("<script" not in page, "no scripts in the report")
	for section in ("What the numbers say", "Against the baseline", "GPU hotspots", "Main thread hotspots", "What grows in the slow frames", "Worst frames", "Memory", "The scene"):
		check(section in page, "section %r" % section)
	svgs = [chunk for chunk in page.split("<svg")[1:]]
	check(len(svgs) >= 8, "charts present (%d svgs)" % len(svgs))
	for index, chunk in enumerate(svgs):
		markup = "<svg" + chunk.split("</svg>")[0] + "</svg>"
		try:
			ET.fromstring(markup)
		except ET.ParseError as error:
			failures.append("svg %d is not well formed XML: %s" % (index, error))
	check(build_report(good).count("Against the baseline") == 0, "no baseline section without a baseline")

	# a file of another kind, and an empty one, are refused with a reason
	for bad_data, why in (({"schema": "other"}, "schema"), (dict(synthetic(frames=1)), "frames")):
		try:
			Run(bad_data, "bad.json")
			failures.append("accepted a bad file (%s)" % why)
		except ValueError:
			pass
	broken = synthetic()
	broken["passes"]["Forward"].pop()
	try:
		Run(broken, "broken.json")
		failures.append("accepted a series of the wrong length")
	except ValueError:
		pass

	if failures:
		print("bench_report selftest FAILED:")
		for failure in failures:
			print("  -", failure)
		return 1
	print("bench_report selftest passed")
	return 0


def main():
	parser = argparse.ArgumentParser(description="Makes an HTML report from a wiwa benchmark result.")
	parser.add_argument("result", nargs="?", help="the JSON written by wiwa_game --bench-out")
	parser.add_argument("-o", "--output", help="the report (default: next to the result, .html)")
	parser.add_argument("--baseline", help="an earlier result to compare with")
	parser.add_argument("--fail-on-regression", type=float, metavar="PERCENT", help="exit with 3 when the average FPS or the 1%% low is worse than the baseline by more than this")
	parser.add_argument("--title")
	parser.add_argument("--selftest", action="store_true")
	args = parser.parse_args()
	if args.selftest:
		sys.exit(selftest())
	if not args.result:
		parser.error("a result file is needed")

	run = load(args.result)
	base = load(args.baseline) if args.baseline else None
	output = pathlib.Path(args.output) if args.output else pathlib.Path(args.result).with_suffix(".html")
	output.write_text(build_report(run, base, args.title), encoding="utf-8")
	print(summary_text(run, base))
	print("report:", output)
	if base is not None and args.fail_on_regression is not None:
		worse = regressed(run, base, args.fail_on_regression)
		if worse:
			print("REGRESSION: " + ", ".join("%s %s%%" % (n, num(c)) for n, c in worse))
			sys.exit(3)


if __name__ == "__main__":
	main()
