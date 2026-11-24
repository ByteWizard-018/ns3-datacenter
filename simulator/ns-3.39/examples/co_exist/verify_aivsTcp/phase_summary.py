# Per-phase AI slowdown and TCP short-flow P99 of a run-profile.sh folder.
# Usage: python3 phase_summary.py <folder>
import os, sys, math
R = sys.argv[1]
ai_start, period, iters, window, profile = open(f"{R}/params.txt").read().split()
ai_start, period, iters = float(ai_start), float(period), int(iters)
loads = [float(x) for x in profile.split(",")]
t0 = ai_start
length = iters * period / len(loads)
SHORT = 100_000 # bytes

def phase(t):
    return max(0, min(int((t - t0) // length), len(loads) - 1))

def lines(path):
    return [l.split() for l in open(path) if l.strip()] if os.path.exists(path) else []

def p99(v):
    v = sorted(v)
    return v[math.ceil(0.99 * len(v)) - 1] if v else float("nan")

alone_rows = lines(f"{R}/ai-alone/cct.txt")[1:]
ai_alone = sum(float(r[3]) for r in alone_rows) / len(alone_rows)

def ai_per_phase(run):
    out = [[] for _ in loads]
    for r in lines(f"{R}/{run}/cct.txt")[1:]:
        out[phase(float(r[1]))].append(float(r[3]))
    return out

def tcp_per_phase(run):
    out = [[] for _ in loads]
    for r in lines(f"{R}/{run}/tcp-fct.txt"):
        fin, size, fct = float(r[0]), float(r[1]), float(r[2])
        if size <= SHORT:
            out[phase(fin - fct / 1e9)].append(fct / 1000)
    return out

alone = [p99(v) for v in tcp_per_phase("tcp-alone")]
order = ["rr", "w4", "w8", "aimd100", "wait100", "wait200"]
runs = [x for x in order if os.path.isdir(f"{R}/{x}")]
print(f"AI alone: {ai_alone*1000:.1f} ms; phases of {length:.3f} s with TCP loads {profile}\n")
hdr = ["Policy", "Phase", "TCP load", "AI slowdown", "AI iters", "TCP P99 (us)", "TCP P99 alone (us)", "TCP P99 ratio"]
print("  ".join(f"{h:<18}" for h in hdr))
for run in runs:
    ai, tcp = ai_per_phase(run), tcp_per_phase(run)
    for i, L in enumerate(loads):
        slow = sum(ai[i]) / len(ai[i]) / ai_alone if ai[i] else float("nan")
        p = p99(tcp[i])
        row = [run, i, L, f"{slow:.2f}", len(ai[i]), f"{p:.0f}", f"{alone[i]:.0f}", f"{p/alone[i]:.2f}"]
        print("  ".join(f"{str(c):<18}" for c in row))
