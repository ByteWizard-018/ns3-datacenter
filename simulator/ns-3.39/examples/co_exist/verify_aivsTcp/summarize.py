# Summary table of a results folder: summary.csv and summary.html.
import os, sys, csv, math
R = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
ai_start, period, iters, window = open(f"{R}/params.txt").read().split()
ai_start, period, iters, window = float(ai_start), float(period), int(iters), float(window)
PATTERN = {1: "disjoint", 2: "random", 3: "sync", 4: "strict disjoint"}
SHORT = 100_000 # bytes

def lines(path):
    return [l.split() for l in open(path) if l.strip()] if os.path.exists(path) else []

def cct_mean(run): # iteration 0 is warm-up
    rows = lines(f"{R}/{run}/cct.txt")
    used = rows[1:]
    return (sum(float(r[3]) for r in used) / len(used) if used else float("nan")), len(rows)

def tcp_flows(run):
    return [(float(r[0]), float(r[1]), float(r[2])) for r in lines(f"{R}/{run}/tcp-fct.txt")]

def p99_short_us(flows):
    v = sorted(f[2] / 1000 for f in flows if f[1] <= SHORT)
    return v[math.ceil(0.99 * len(v)) - 1] if v else float("nan")

def pct_in_ai(flows, run): # % of TCP bytes sent during an AllReduce
    bursts = [(float(r[1]), float(r[2])) for r in lines(f"{R}/{run}/cct.txt")]
    inside = total = 0.0
    for fin, size, fct_ns in flows:
        start, dur = fin - fct_ns / 1e9, fct_ns / 1e9
        ov = 0.0
        for a, b in bursts:
            ov += max(0.0, min(fin, b) - max(start, a))
        inside += size * ov / dur; total += size
    return 100 * inside / total if total else float("nan")

def scheduled(run):
    for l in open(f"{R}/{run}/run.log"):
        if l.startswith("TCP flows scheduled:"): return int(l.split(":")[1])
    return 0

def run_time(run): # pattern 4 only
    for l in open(f"{R}/{run}/run.log"):
        if l.startswith("STRICT_DONE t="): return f"{float(l.split('=')[1]):.3f}"
    return "-"

def drops(run):
    for l in open(f"{R}/{run}/run.log"):
        if l.startswith("DROPS tcp="): return l.split()[1].split("=")[1]
    return "-"

def pfc_events(run):
    return len(lines(f"{R}/{run}/pfc.txt"))

ai_alone, _ = cct_mean("ai-alone")
cols = ["Pattern", "Load", "AI iters done", "AI CCT (ms)", "AI slowdown", "TCP short P99 (us)",
        "TCP P99 alone (us)", "TCP P99 ratio", "TCP flows done", "PFC events", "TCP drops", "TCP bytes during AI (%)", "Run time (s)"]
rows = []
for pat in (1, 2, 3, 4):
    for load in ("0.3", "0.5"):
        both, alone = f"both_p{pat}_{load}", f"tcp-alone_p{pat}_{load}"
        if not os.path.isdir(f"{R}/{both}"): continue
        c, n = cct_mean(both)
        fb, fa = tcp_flows(both), tcp_flows(alone)
        pb, pa = p99_short_us(fb), p99_short_us(fa)
        rows.append([PATTERN[pat], load, f"{n}/{iters}", f"{c*1000:.1f}", f"{c/ai_alone:.2f}",
                     f"{pb:.0f}", f"{pa:.0f}", f"{pb/pa:.2f}", f"{len(fb)}/{scheduled(both)}",
                     pfc_events(both), drops(both), f"{pct_in_ai(fb, both):.1f}", run_time(both)])

with open(f"{R}/summary.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(cols); w.writerows(rows)

# text table
widths = [max(len(str(x)) for x in [c] + [r[i] for r in rows]) for i, c in enumerate(cols)]
line = "  ".join(str(c).ljust(widths[i]) for i, c in enumerate(cols))
print(f"AI alone: mean CCT = {ai_alone*1000:.1f} ms (iteration 0 is warm-up and not averaged)")
print(f"Fixed-clock schedule ends at {ai_start + iters * period:.3f} s (compare with the strict run time)\n"); print(line); print("-" * len(line))
for r in rows: print("  ".join(str(x).ljust(widths[i]) for i, x in enumerate(r)))

# html table
head = "".join(f"<th>{c}</th>" for c in cols)
body = "".join("<tr>" + "".join(f"<td>{x}</td>" for x in r) + "</tr>" for r in rows)
open(f"{R}/summary.html", "w").write(f"""<!doctype html><meta charset="utf-8"><title>AI vs TCP summary</title>
<style>body{{font-family:system-ui,sans-serif;margin:32px;color:#111}}table{{border-collapse:collapse}}
th,td{{padding:8px 12px;border-bottom:1px solid #ddd;text-align:right}}th{{background:#f3f2ee;text-align:center}}
td:first-child,td:nth-child(2){{text-align:left}}</style>
<h2>AI (ring AllReduce, 64 MB) vs TCP, 1 seed</h2>
<p>AI alone: mean CCT = {ai_alone*1000:.1f} ms (iteration 0 is warm-up, not averaged). Fixed-clock schedule ends at {ai_start + iters * period:.3f} s. Slowdown and P99 ratio are relative to the matching alone-run.</p>
<table><tr>{head}</tr>{body}</table>""")
