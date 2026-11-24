#!/usr/bin/env python3
"""Draws the report figures from the results folders.
Run from this folder:

    python3 make_figures.py              # writes figures/*.pdf and figures/*.png
    python3 make_figures.py other_dir    # writes them somewhere else

"""
import os, sys, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Patch
from matplotlib.lines import Line2D

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "figures")
SHORT, LONG = 100_000, 1_000_000 # bytes
W = 6.3 # inches

# colours
BLACK, BLUE, RED, GREEN, GREY = "#000000", "#0033cc", "#cc0000", "#008000", "#999999"
COL = {"rr": BLACK, "fixed": RED, "aimd": GREEN, "wait": BLUE, "alone": GREY}
plt.rcParams.update({"font.size": 9, "axes.titlesize": 9, "axes.labelsize": 9, "legend.fontsize": 8,
                     "xtick.labelsize": 8, "ytick.labelsize": 8, "axes.grid": True, "grid.alpha": 0.3,
                     "axes.spines.top": False, "axes.spines.right": False, "savefig.bbox": "tight"})


# ---------------------------------------------------------------- reading the result files
def F(folder):
    return os.path.join(HERE, folder)

def lines(path):
    return [l.split() for l in open(path) if l.strip()] if os.path.exists(path) else []

def finished(folder, run):
    log = os.path.join(F(folder), run, "run.log")
    return os.path.exists(log) and any(l.startswith("TCP flows scheduled") for l in open(log))

def have(*folder_runs):
    return all(finished(f, r) for f, r in folder_runs)

def cct_ms(folder, run): # iteration 0 is warm-up
    rows = lines(os.path.join(F(folder), run, "cct.txt"))[1:]
    return 1000 * sum(float(r[3]) for r in rows) / len(rows) if rows else float("nan")

def slowdown(folder, run):
    return cct_ms(folder, run) / cct_ms(folder, "ai-alone")

def flows(folder, run): # (finish_s, size_bytes, fct_us)
    return [(float(r[0]), float(r[1]), float(r[2]) / 1000) for r in lines(os.path.join(F(folder), run, "tcp-fct.txt"))]

def p99(values):
    v = sorted(values)
    return v[math.ceil(0.99 * len(v)) - 1] if v else float("nan")

def short_p99_us(folder, run):
    return p99([f[2] for f in flows(folder, run) if f[1] <= SHORT])

def p99_ratio(folder, pat, load):
    return short_p99_us(folder, f"both_p{pat}_{load}") / short_p99_us(folder, f"tcp-alone_p{pat}_{load}")

def long_mean_ms(folder, run):
    v = [f[2] / 1000 for f in flows(folder, run) if f[1] > LONG]
    return sum(v) / len(v) if v else float("nan")

def pfc(folder, run):
    return len(lines(os.path.join(F(folder), run, "pfc.txt")))

def drops(folder, run):
    for l in lines(os.path.join(F(folder), run, "run.log")):
        if l and l[0] == "DROPS":
            return int(l[1].split("=")[1])
    return float("nan")


# ---------------------------------------------------------------- small drawing helpers
def save(fig, name):
    os.makedirs(OUT, exist_ok=True)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(os.path.join(OUT, f"{name}.{ext}"), dpi=200)
    plt.close(fig)
    print("wrote", name)

def skip(name):
    print("skipped", name, "(results missing)")

def kfmt(v):
    return f"{v/1000:.0f}k" if v >= 10000 else f"{v:.0f}"

def value_labels(ax, bars, fmt="{:.2f}", fs=7):
    for b in bars:
        h = b.get_height()
        if h == h: # skip NaN
            ax.annotate(fmt(h) if callable(fmt) else fmt.format(h), (b.get_x() + b.get_width() / 2, h),
                        xytext=(0, 2), textcoords="offset points", ha="center", va="bottom", fontsize=fs)

def ref_line(ax, y=1.0):
    ax.axhline(y, color=BLACK, lw=0.8)


# ---------------------------------------------------------------- Fig 1: topology (a drawing)
def fig_topology():
    fig, ax = plt.subplots(figsize=(W, 3.0))
    ax.axis("off")
    def box(x, y, w, h, fc, txt="", fs=7):
        ax.add_patch(Rectangle((x - w / 2, y - h / 2), w, h, fc=fc, ec="none"))
        if txt:
            ax.text(x, y, txt, color="white", ha="center", va="center", fontsize=fs)
    spines = [2.0, 4.0, 6.0, 8.0]
    used = {0: 1.6, 1: 4.4}
    others = [6.1 + i * 0.27 for i in range(14)]
    for sx in spines:
        for ox in others:
            ax.plot([sx, ox], [3.75, 2.6], color="#dddddd", lw=0.4, zorder=0)
        for lx in used.values():
            ax.plot([sx, lx], [3.75, 2.65], color=BLACK, lw=0.8, zorder=0)
    for i, sx in enumerate(spines):
        box(sx, 3.9, 1.0, 0.35, BLACK, f"Spine {i}")
    for r, lx in used.items():
        box(lx, 2.5, 1.0, 0.35, BLACK, f"Leaf {r}")
        for k in range(16):
            x = lx - 1.2 + k * 0.16
            ax.add_patch(Rectangle((x, 1.1), 0.13, 0.4, fc=BLUE if k < 8 else RED, ec="none"))
            ax.plot([lx, x + 0.065], [2.32, 1.5], color="#bbbbbb", lw=0.4, zorder=0)
        first = 0 if r == 0 else 16
        ax.text(lx - 0.6, 0.95, f"servers {first}-{first + 7}", ha="center", va="top", fontsize=7, color=BLUE)
        ax.text(lx + 0.68, 0.95, f"servers {first + 8}-{first + 15}", ha="center", va="top", fontsize=7, color=RED)
    for ox in others:
        ax.add_patch(Rectangle((ox - 0.11, 2.35), 0.22, 0.3, fc="#cccccc", ec="none"))
    ax.text(others[0] + 1.75, 2.05, "leaves 2-15 (idle in these runs)", ha="center", fontsize=7, color="#666666")
    ax.text(0.0, 3.2, "4 x 25 Gbps uplinks\nper leaf = 100 Gbps", ha="left", va="center", fontsize=7)
    ax.text(3.0, 0.4, "16 x 25 Gbps server links per leaf = 400 Gbps (4:1 oversubscription)", ha="center", fontsize=7)
    ax.legend(handles=[Patch(color=BLUE), Patch(color=RED)], labels=["AI ranks", "TCP servers"],
              loc="lower right", frameon=False, fontsize=7)
    ax.set_xlim(0, 10); ax.set_ylim(0.2, 4.2)
    save(fig, "fig01_topology")


# ---------------------------------------------------------------- Fig 2: timing patterns (a drawing)
def fig_patterns():
    rows = lines(os.path.join(F("results"), "params.txt"))
    if not rows:
        return skip("fig02_patterns")
    start, period, iters, slot = float(rows[0][0]), float(rows[0][1]), int(rows[0][2]), float(rows[0][3])
    names = ["Disjoint", "Random", "Sync", "Strict disjoint"]
    fig, ax = plt.subplots(figsize=(W, 2.4))
    for y, name in enumerate(names):
        for i in range(iters):
            ax.add_patch(Rectangle((start + i * period, y + 0.08), slot, 0.36, color=BLUE))
        if name == "Random":
            win = [(start, start + iters * period)]
        elif name == "Sync":
            win = [(start + i * period, start + i * period + slot) for i in range(iters)]
        else:
            win = [(start + i * period + slot, start + (i + 1) * period) for i in range(iters)]
        for a, b in win:
            ax.add_patch(Rectangle((a, y + 0.52), b - a, 0.36, color=RED))
    ax.set_yticks([y + 0.48 for y in range(len(names))], names)
    ax.set_xlim(0, start + iters * period); ax.set_ylim(0, len(names))
    ax.set_xlabel("Simulated time (s)")
    ax.grid(False)
    ax.legend(handles=[Patch(color=BLUE), Patch(color=RED)], labels=[f"AllReduce slot ({slot * 1000:.0f} ms)", "TCP flows may start"],
              loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, frameon=False)
    save(fig, "fig02_patterns")


# ---------------------------------------------------------------- Fig 3: baseline (CUBIC, round robin)
def fig_baseline():
    need = [("results", f"{k}_p{p}_{l}") for p in (1, 2, 3, 4) for l in ("0.3", "0.5") for k in ("both", "tcp-alone")]
    if not have(*need, ("results", "ai-alone")):
        return skip("fig03_baseline")
    names = ["Disjoint", "Random", "Sync", "Strict\ndisjoint"]
    fig, axes = plt.subplots(1, 2, figsize=(W, 2.6))
    for ax, metric, ylabel in ((axes[0], "ai", "AI slowdown (x)"), (axes[1], "tcp", "TCP short-flow P99 ratio (x)")):
        for j, (load, col) in enumerate((("0.3", BLUE), ("0.5", RED))):
            vals = [slowdown("results", f"both_p{p}_{load}") if metric == "ai" else p99_ratio("results", p, load) for p in (1, 2, 3, 4)]
            bars = ax.bar([i + (j - 0.5) * 0.38 for i in range(4)], vals, 0.36, color=col, label=f"TCP load {load}")
            value_labels(ax, bars)
        ref_line(ax)
        ax.set_xticks(range(4), names)
        ax.set_ylabel(ylabel)
        ax.set_ylim(0, 2.25)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, frameon=False)
    save(fig, "fig03_baseline")


# ---------------------------------------------------------------- Fig 4: CUBIC policies, incl. the waiting limit
def fig_cubic():
    cases = [("results", "RR", BLACK, ""), ("results_w2", "w=2", RED, ""), ("results_w4", "w=4", RED, ""), ("results_w8", "w=8", RED, ""),
             ("results_wait50", "D=50", BLUE, ""), ("results_wait100", "D=100", BLUE, ""), ("results_wait200", "D=200", BLUE, ""),
             ("results_wait100w2", "D=100\ncap 2", "white", "////"), ("results_wait100w4", "D=100\ncap 4", "white", "////"),
             ("results_wait100w8", "D=100\ncap 8", "white", "////")]
    cases = [c for c in cases if have((c[0], "both_p2_0.5"))]
    if len(cases) < 3:
        return skip("fig04_cubic")
    x = range(len(cases))
    fig, axes = plt.subplots(3, 1, figsize=(W, 5.6), sharex=True)
    panels = [("AI slowdown (x)", lambda f: slowdown(f, "both_p2_0.5"), "{:.2f}"),
              ("TCP short-flow\nP99 ratio (x)", lambda f: p99_ratio(f, 2, "0.5"), "{:.2f}"),
              ("PFC pause events", lambda f: pfc(f, "both_p2_0.5"), kfmt)]
    for ax, (ylabel, fn, fmt) in zip(axes, panels):
        vals = [fn(c[0]) for c in cases]
        bars = ax.bar(x, vals, 0.65, color=[c[2] for c in cases], edgecolor=[BLUE if c[3] else c[2] for c in cases],
                      hatch=None)
        for b, c in zip(bars, cases):
            b.set_hatch(c[3])
        value_labels(ax, bars, fmt)
        ax.set_ylabel(ylabel)
        ax.set_ylim(0, max(vals) * 1.2)
        if ylabel.startswith(("AI", "TCP")):
            ref_line(ax)
    axes[-1].set_xticks(list(x), [c[1] for c in cases])
    save(fig, "fig04_cubic")


# ---------------------------------------------------------------- Fig 5 / A4: AI vs TCP tradeoff with DCTCP
FAMILIES = [("Fixed weight", ["rr", "w2", "w4", "w8"], RED, "o", "-"),
            ("AIMD weight", ["aimd50", "aimd100", "aimd200"], GREEN, "s", "--"),
            ("Waiting limit", ["wait50", "wait100", "wait150", "wait200", "wait300", "wait500"], BLUE, "D", "-")]
NAME = {"rr": "RR", "w2": "w=2", "w4": "w=4", "w8": "w=8", "aimd50": "", "aimd100": "", "aimd200": ""}
OFFSET = {"rr": (-4, 6), "w2": (2, 7), "w4": (6, 3), "w8": (6, 3), "aimd50": (6, -9), "aimd100": (6, -9), "aimd200": (6, -9),
          "wait50": (-6, -12), "wait100": (5, -11), "wait150": (-34, -3), "wait200": (-34, -3), "wait300": (-34, -3), "wait500": (-34, -3)}

def tradeoff_point(run, pat):
    folder = "results_dctcp" if run == "rr" else f"results_dctcp_{run}"
    if not have((folder, f"both_p{pat}_0.5"), (folder, f"tcp-alone_p{pat}_0.5"), (folder, "ai-alone")):
        return None
    return slowdown(folder, f"both_p{pat}_0.5"), p99_ratio(folder, pat, "0.5")

def tradeoff(pat, name):
    if tradeoff_point("rr", pat) is None:
        return skip(name)
    fig, ax = plt.subplots(figsize=(W, 3.6))
    for fam, runs, col, mk, ls in FAMILIES:
        pts = [(r, tradeoff_point(r, pat)) for r in runs]
        pts = [(r, p) for r, p in pts if p]
        if not pts:
            continue
        ax.plot([p[0] for _, p in pts], [p[1] for _, p in pts], ls, marker=mk, color=col, label=fam, ms=5)
        for r, (xv, yv) in pts:
            txt = NAME.get(r, "D=" + r.replace("wait", ""))
            if txt:
                ax.annotate(txt, (xv, yv), xytext=OFFSET.get(r, (5, 3)), textcoords="offset points", fontsize=7, color=col)
    first = tradeoff_point("aifirst", pat)
    if first:
        ax.text(0.99, 0.97, f"AI first: AI {first[0]:.2f}x, TCP {first[1]:.2f}x (above the plotted range)",
                transform=ax.transAxes, ha="right", va="top", fontsize=7, color=RED)
    ax.plot([1], [1], "+", color=BLACK, ms=10)
    ax.annotate("both unaffected", (1, 1), xytext=(6, -10), textcoords="offset points", fontsize=7)
    ax.set_xlabel("AI slowdown (x)"); ax.set_ylabel("TCP short-flow P99 ratio (x)")
    ax.set_ylim(0.5, 3.1)
    ax.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, 1.12), ncol=3)
    save(fig, name)

def fig_tradeoff():
    tradeoff(2, "fig05_tradeoff")


# ---------------------------------------------------------------- Fig 6: the waiting limit D
def fig_dsweep():
    ds = [d for d in (50, 100, 150, 200, 300, 500) if have((f"results_dctcp_wait{d}", "both_p2_0.5"))]
    if len(ds) < 3:
        return skip("fig06_dsweep")
    ai = [slowdown(f"results_dctcp_wait{d}", "both_p2_0.5") for d in ds]
    tcp = [p99_ratio(f"results_dctcp_wait{d}", 2, "0.5") for d in ds]
    refs = [(n, tradeoff_point(n, 2)) for n in ("w4", "w8")]
    fig, axes = plt.subplots(1, 2, figsize=(W, 2.6))
    for ax, vals, ylabel, k in ((axes[0], ai, "AI slowdown (x)", 0), (axes[1], tcp, "TCP short-flow P99 ratio (x)", 1)):
        ax.plot(ds, vals, "-D", color=BLUE, ms=4, label="Waiting limit")
        for (n, p), ls in zip(refs, ("--", ":")):
            if p:
                ax.axhline(p[k], color=RED, ls=ls, lw=1, label=f"fixed {NAME[n]}")
        ax.set_xlabel("Waiting limit D (us)"); ax.set_ylabel(ylabel)
        ax.set_xticks(ds, [str(d) for d in ds])
        ax.set_ylim(0.6, max(vals) * 1.15)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=3, frameon=False)
    save(fig, "fig06_dsweep")


# ---------------------------------------------------------------- Fig 7: three seeds
def fig_seeds():
    seeds = [s for s in ("results_dctcp", "results_dctcp_s11", "results_dctcp_s13") if have((s + "_wait100", "both_p2_0.5"))]
    if len(seeds) < 2:
        return skip("fig07_seeds")
    pols = [("", "Round robin", BLACK), ("_w4", "w=4", RED), ("_aimd100", "AIMD 100", GREEN), ("_wait100", "Waiting limit 100", BLUE)]
    cases = [(2, "0.3"), (2, "0.5"), (3, "0.3"), (3, "0.5")]
    fig, axes = plt.subplots(1, 2, figsize=(W, 2.8))
    for ax, metric, ylabel in ((axes[0], "ai", "AI slowdown (x)"), (axes[1], "p99", "TCP short-flow P99 (us)")):
        for k, (suffix, name, col) in enumerate(pols):
            xs, mean, lo, hi = [], [], [], []
            for i, (pat, load) in enumerate(cases):
                v = [slowdown(b + suffix, f"both_p{pat}_{load}") if metric == "ai" else short_p99_us(b + suffix, f"both_p{pat}_{load}")
                     for b in seeds if have((b + suffix, f"both_p{pat}_{load}"))]
                if v:
                    m = sum(v) / len(v)
                    xs.append(i + (k - 1.5) * 0.2); mean.append(m); lo.append(m - min(v)); hi.append(max(v) - m)
            ax.bar(xs, mean, 0.19, yerr=[lo, hi], capsize=2, color=col, label=name, error_kw={"lw": 0.8})
        ax.set_xticks(range(len(cases)), [f"{'Random' if p == 2 else 'Sync'}\n{l}" for p, l in cases])
        ax.set_ylabel(ylabel)
    ref_line(axes[0])
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=4, frameon=False)
    save(fig, "fig07_seeds")


# ---------------------------------------------------------------- Fig 8: TCP load changes during the run
def phase_values(folder, run):
    p = lines(os.path.join(F(folder), "params.txt"))[0]
    start, period, iters, loads = float(p[0]), float(p[1]), int(p[2]), [float(x) for x in p[4].split(",")]
    length = iters * period / len(loads)
    phase = lambda t: max(0, min(int((t - start) // length), len(loads) - 1))
    alone = cct_ms(folder, "ai-alone")
    ai, tcp = [[] for _ in loads], [[] for _ in loads]
    for r in lines(os.path.join(F(folder), run, "cct.txt"))[1:]:
        ai[phase(float(r[1]))].append(1000 * float(r[3]) / alone)
    for fin, size, fct in flows(folder, run):
        if size <= SHORT:
            tcp[phase(fin - fct / 1e6)].append(fct)
    return loads, [sum(a) / len(a) if a else float("nan") for a in ai], [p99(t) for t in tcp]

def fig_profile():
    folders = [f for f in ("results_dctcp_profile", "results_dctcp_s11_profile", "results_dctcp_s13_profile")
               if have((f, "wait100"), (f, "w8"), (f, "tcp-alone"))]
    if not folders:
        return skip("fig08_profile")
    pols = [("w4", "w=4", RED, ":"), ("w8", "w=8", RED, "-"), ("aimd100", "AIMD 100", GREEN, "--"),
            ("wait100", "Waiting limit 100", BLUE, "-"), ("wait200", "Waiting limit 200", BLUE, "--"), ("tcp-alone", "TCP alone", GREY, "-")]
    fig, axes = plt.subplots(1, 2, figsize=(W, 2.8))
    loads = None
    for run, name, col, ls in pols:
        per = [phase_values(f, run) for f in folders if have((f, run))]
        if not per:
            continue
        loads = per[0][0]
        x = list(range(len(loads)))
        axes[1].plot(x, [sum(s[2][i] for s in per) / len(per) for i in x], ls, marker="o", ms=4, color=col, label=name)
        if run != "tcp-alone":
            axes[0].plot(x, [sum(s[1][i] for s in per) / len(per) for i in x], ls, marker="o", ms=4, color=col)
    for ax in axes:
        ax.set_xticks(range(len(loads)), [f"load {l}" for l in loads])
        ax.set_xlabel("TCP load in each third of the run")
    axes[0].set_ylabel("AI slowdown (x)"); axes[1].set_ylabel("TCP short-flow P99 (us)")
    handles, labels = axes[1].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=3, frameon=False)
    save(fig, "fig08_profile")


# ---------------------------------------------------------------- Fig 9: Reverie comparison
def fig_reverie():
    sizes = [s for s in ("12MiB", "2.56MB") if have(*[(f"results_dctcp_rv_{s}{c}", "both_p2_0.5")
                                                     for c in ("_dt", "_reverie", "_dt_wait100", "_reverie_wait100")])]
    if not sizes:
        return skip("fig09_reverie")
    cells = [("_dt", "Default buffer + RR", BLACK, ""), ("_reverie", "Reverie buffer + RR", "white", "////"),
             ("_dt_wait100", "Default buffer + waiting limit", BLUE, ""), ("_reverie_wait100", "Reverie buffer + waiting limit", "white", "\\\\\\\\")]
    edge = {"_dt": BLACK, "_reverie": BLACK, "_dt_wait100": BLUE, "_reverie_wait100": BLUE}
    metrics = [("AI slowdown (x)", lambda f: slowdown(f, "both_p2_0.5"), "{:.2f}"),
               ("TCP short-flow P99 (us)", lambda f: short_p99_us(f, "both_p2_0.5"), "{:.0f}"),
               ("PFC pause events", lambda f: pfc(f, "both_p2_0.5"), kfmt),
               ("TCP packet drops", lambda f: drops(f, "both_p2_0.5"), kfmt)]
    fig, axes = plt.subplots(2, 2, figsize=(W, 4.6))
    for ax, (ylabel, fn, fmt) in zip(axes.flat, metrics):
        top = 0
        for k, (suf, name, col, hatch) in enumerate(cells):
            vals = [fn(f"results_dctcp_rv_{s}{suf}") for s in sizes]
            bars = ax.bar([i + (k - 1.5) * 0.2 for i in range(len(sizes))], vals, 0.19, color=col, edgecolor=edge[suf],
                          hatch=hatch, label=name, lw=0.8)
            value_labels(ax, bars, fmt, fs=6)
            top = max(top, max(vals))
        ax.set_xticks(range(len(sizes)), [f"buffer {s}" for s in sizes])
        ax.set_ylabel(ylabel)
        ax.set_ylim(0, (top if top > 0 else 1) * 1.2)
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, frameon=False)
    save(fig, "fig09_reverie")


# ---------------------------------------------------------------- Appendix figures
def fig_cdf():
    runs = [("results_dctcp", "tcp-alone_p2_0.5", "TCP alone", GREY), ("results_dctcp", "both_p2_0.5", "Round robin", BLACK),
            ("results_dctcp_w4", "both_p2_0.5", "w=4", RED), ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100", GREEN),
            ("results_dctcp_wait100", "both_p2_0.5", "Waiting limit 100", BLUE)]
    runs = [r for r in runs if have((r[0], r[1]))]
    if len(runs) < 2:
        return skip("figA1_cdf")
    fig, ax = plt.subplots(figsize=(W, 3.0))
    for folder, run, name, col in runs:
        v = sorted(f[2] for f in flows(folder, run) if f[1] <= SHORT)
        ax.plot(v, [(i + 1) / len(v) for i in range(len(v))], color=col, label=name, lw=1.2)
    ax.set_xscale("log")
    ticks = [30, 50, 100, 200, 500, 1000]
    ax.set_xticks(ticks, [str(t) for t in ticks]); ax.minorticks_off()
    ax.axhline(0.99, color=BLACK, lw=0.6, ls=":")
    ax.set_xlabel("Short-flow completion time (us)"); ax.set_ylabel("Fraction of flows")
    ax.legend(frameon=False, loc="lower right")
    save(fig, "figA1_cdf")

def fig_longflows():
    runs = [("results_dctcp", "RR", BLACK), ("results_dctcp_w4", "w=4", RED), ("results_dctcp_w8", "w=8", RED),
            ("results_dctcp_aimd100", "AIMD\n100", GREEN), ("results_dctcp_wait100", "D=100", BLUE),
            ("results_dctcp_wait200", "D=200", BLUE), ("results_dctcp_aifirst", "AI first", RED)]
    runs = [r for r in runs if have((r[0], "both_p2_0.5"))]
    if len(runs) < 2:
        return skip("figA2_longflows")
    fig, ax = plt.subplots(figsize=(W, 2.8))
    bars = ax.bar(range(len(runs)), [long_mean_ms(f, "both_p2_0.5") for f, _, _ in runs], 0.6, color=[c for _, _, c in runs])
    value_labels(ax, bars, "{:.1f}")
    ax.axhline(long_mean_ms("results_dctcp", "tcp-alone_p2_0.5"), color=GREY, ls="--", lw=1)
    ax.legend(handles=[Line2D([], [], color=GREY, ls="--")], labels=["TCP alone"], frameon=False, loc="upper left")
    ax.set_xticks(range(len(runs)), [n for _, n, _ in runs])
    ax.set_ylabel("Mean completion time,\nflows > 1 MB (ms)")
    save(fig, "figA2_longflows")

def fig_iterations():
    runs = [("results_dctcp", "ai-alone", "AI alone", GREY), ("results_dctcp", "both_p2_0.5", "Round robin", BLACK),
            ("results_dctcp_w4", "both_p2_0.5", "w=4", RED), ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100", GREEN),
            ("results_dctcp_wait100", "both_p2_0.5", "Waiting limit 100", BLUE)]
    runs = [r for r in runs if have((r[0], r[1]))]
    if len(runs) < 2:
        return skip("figA3_iterations")
    fig, ax = plt.subplots(figsize=(W, 2.8))
    n = 0
    for k, (folder, run, name, col) in enumerate(runs):
        rows = lines(os.path.join(F(folder), run, "cct.txt"))
        n = max(n, len(rows))
        ax.bar([int(r[0]) + (k - (len(runs) - 1) / 2) * 0.16 for r in rows], [1000 * float(r[3]) for r in rows], 0.15, color=col, label=name)
    ax.set_xticks(range(n), [f"iteration {i}" for i in range(n)])
    ax.set_ylabel("AllReduce time (ms)")
    ax.legend(frameon=False, loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=5)
    save(fig, "figA3_iterations")

def fig_tradeoff_sync():
    tradeoff(3, "figA4_tradeoff_sync")


if __name__ == "__main__":
    for make in (fig_topology, fig_patterns, fig_baseline, fig_cubic, fig_tradeoff, fig_dsweep, fig_seeds, fig_profile,
                 fig_reverie, fig_cdf, fig_longflows, fig_iterations, fig_tradeoff_sync):
        try:
            make()
        except Exception as e:
            print("FAILED", make.__name__, "->", e)
    print("figures are in", OUT)
